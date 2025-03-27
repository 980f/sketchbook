#pragma once
#define BroadcastNode_WIFI_CHANNEL 6

// vortex lights
/////////////////////////////////////////
// Structure to convey data
//name is stale, is now a command rather than state.

struct DesiredState {
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
    return true;//no further qualification at this time
  }

  bool accept(const Block< const uint8_t> &msg) {
    if (isValidMessage(msg)) {
      return parse(msg);
    }
    return false;
  }

};

// command to set some lights. static for debug convenience, should be member of Worker and Boss.
DesiredState stringState;

///////////////////////////////////////////////////////////////////////
// the guy who receives commands as to which lights should be active.

#include "broadcastNode.h"
struct Stripper : public BroadcastNode {
  Stripper(): BroadcastNode(BroadcastNode_Triplet) {}
  LedStringer leds;

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

  void setup(bool justTheString = true) {
    EL.shutup();
    LedStringer::spew = &Serial;
    leds.setup(VortexFX.total, pixel);
    //if EL's are restored they get setup here.
    if (!justTheString) {
      BroadcastNode::begin(true);
    }
    Serial.println("Worker Setup Complete");
  }

  bool dataReceived = false;

 
  void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {
    if(true||TRACE){
      Serial.println("Stripper::onReceive()");
    }
    if (stringState.accept(Packet{len, *data})) { //trusting network to frame packets, and packet to be less than one frame
      if (TRACE) {
        Serial.println("Got pattern message:");
        stringState.printTo(Serial);
        if (BUG3) {
          dumpHex(len, data, Serial);
        }
      }
      dataReceived = true;
    } else {
      Serial.println("Not my type of packet");
      BroadcastNode::onReceive(data, len, broadcast);
    }
  }


  void loop() {
    if (flagged(dataReceived)) { // message received
      if (EVENT) {
        stringState.printTo(Serial);
      }
      leds.setPattern(stringState.color, stringState.pattern);
      if (flagged(stringState.showem)) {
        leds.show();
      }
    }
  }

  // this is called once per millisecond, from the arduino loop().
  // It can skip millisecond values if there are function calls which take longer than a milli.
  void onTick(MilliTick ignored) {
    // not yet animating anything, so nothing to do here.
  }
} stripper;
