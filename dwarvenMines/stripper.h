#pragma once
#define BroadcastNode_WIFI_CHANNEL 6

// vortex lights
/////////////////////////////////////////
// Structure to convey data


struct VortexLighting {
  // command to set some lights.
  struct Message {
    const unsigned char  prefix[4] = {'S', '4', '1', 'L'};
    uint8_t startMarker = 0;//simplifies things if same type as endMarker, and as zero is terminating null for prefix
    //3 spare bytes here if we don't PACK
    /// body
    unsigned sequenceNumber = 0;//could use start or endmarker, but leaving the latter 0 for systems that send ascii strings.
    CRGB color;
    LedStringer::Pattern pattern;
    bool showem = false;

    /// end body
    /////////////////////////////
    uint8_t endMarker;//value ignored, not sent

    //local state.
    bool dataReceived = false;
    ///////////////////////////
    size_t printTo(Print &stream) {
      size_t length = 0;
      length += stream.println(reinterpret_cast<const char *>(prefix));
      length += stream.printf("Sequence#:%u\t", sequenceNumber);
      length += stream.printf("Show em:%x\t", showem);
      length += stream.printf("Color:%06X\n", color.as_uint32_t());
      //    length += stream.print("Pattern:\t") stream.print(pattern);
      length += pattern.printTo(stream);
      return length;
    }
    /////////////////////////
    /** format for delivery, content is copied but not immediately so using stack is risky.
        we check the prefix but skip copying it since we const'd it.
    */

    Block<const uint8_t> outgoing() const {
      return Block<const uint8_t> {(&endMarker - reinterpret_cast<const uint8_t *>(prefix)), *reinterpret_cast<const uint8_t *>(prefix)};
    }

    /** expect the whole object including prefix */
    bool isValidMessage(const Block< const uint8_t> msg) const {
      auto expect = outgoing();
      if (TRACE) {
        Serial.printf("Incoming: %u %s\n", msg.size, &msg.content);
        Serial.printf("Expectin: %u %s\n", expect.size, &expect.content);

      }
      return msg.size >= expect.size && 0 == memcmp(&msg.content, &expect.content, sizeof(prefix));
    }

    Block<uint8_t> incoming()  {
      return Block<uint8_t> {(&endMarker - &startMarker), startMarker};
    }

    /** for efficiency this presumes you got a true from isValidMessage*/
    bool parse(const Block< const uint8_t> &msg)  {
      auto buffer = incoming();
      memcpy(&buffer.content, &msg.content + sizeof(prefix), buffer.size);
      dataReceived = true;
      return true;//no further qualification at this time
    }

    bool accept(const Block< const uint8_t> &msg) {
      if (isValidMessage(msg)) {
        return parse(msg);
      }
      return false;
    }

  };
  Message command;
  LedStringer leds;

  void setup() {
    LedStringer::spew = &Serial;
    leds.setup(VortexFX.total, pixel);
  }

  void act() {
    command.showem = true; // jammit
    if (EVENT) {
      command.printTo(Serial);
    }
    leds.setPattern(command.color, command.pattern);
    if (flagged(command.showem)) {
      leds.show();
    }
  }

  void loop() {
    if (flagged(command.dataReceived)) { // message received
      act();
    }
  }
};

///////////////////////////////////////////////////////////////////////
// the guy who receives commands as to which lights should be active.

#include "broadcastNode.h"

struct VortexCommon: public VortexLighting, BroadcastNode {

  VortexCommon(): BroadcastNode(BroadcastNode_Triplet) {  }
  
  void sendMessage(const Message &msg) {
    auto block = msg.outgoing();
    if (TRACE) {
      Serial.printf("sendMessage: %u, %.4s  (%p)\n", block.size, &block.content, &block.content);
      if (BUG3) {
        dumpHex(block, Serial);//#yes, making a Packet just to tear it apart seems like extra work, but it provides an example of use and a compile time test of source integrity.
      }
    }
    send_message(block);
  }
};

struct Stripper : public VortexCommon {
  
  struct ELgroup {
    static constexpr unsigned numEls = 8;
    SimpleOutputPin ELPin[numEls] = {4, 16, 17, 18, 19, 21, 22, 23};
    /** floating inputs into the control board are a bad idea. */
    void shutup() {//expedite and ensure the unused relays don't click and clack due to noise.
      for (unsigned index = numEls; index-- > 0;) {
        ELPin[index] << false;
        ELPin[index].setup();
      }
    }

    SimpleOutputPin &operator [](unsigned which) {
      if (which < numEls) {
        return ELPin[which];
      } else {
        return ELPin[numEls - 1]; //last relay gets to be abused by bad code.
      }
    }

  } EL;

  void setup() {
    VortexLighting::setup();
    EL.shutup();
    BroadcastNode::begin(true);
    Serial.println("Worker Setup Complete");
  }

  void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {
    if (TRACE) {
      Serial.printf("Stripper::onReceive() as %p \n", this);
    }
    if (command.accept(Packet{len, *data})) { //trusting network to frame packets, and packet to be less than one frame
      if (TRACE) {
        command.printTo(Serial);
        if (BUG3) {
          dumpHex(len, data, Serial);
        }
      }
    } else {
      Serial.println("Not my type of packet");
      BroadcastNode::onReceive(data, len, broadcast);
    }
  }

  void loop() {
    VortexLighting::loop();
  }

  // this is called once per millisecond, from the arduino loop().
  // It can skip millisecond values if there are function calls which take longer than a milli.
  void onTick(MilliTick ignored) {
    // not yet animating anything, so nothing to do here.
  }
};
