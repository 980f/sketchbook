#pragma once

/**
    vortex lights interface
*/
#define BroadcastNode_WIFI_CHANNEL 6
#include "broadcastNode.h"
using Packet = BroadcastNode::Packet;
using Body = BroadcastNode::Body;
/////////////////////////////////////////
// Structure to convey data
//Worker config:
#define LEDStringType WS2811, LED_PIN, GRB


constexpr struct StripConfiguration {
  unsigned perRevolutionActual = 100;//no longer a guess
  unsigned perRevolutionVisible = 89;//from 2024 haunt code
  unsigned perStation = perRevolutionVisible / 2;
  unsigned numStrips = 4;
  unsigned total = perRevolutionActual * numStrips;
} VortexFX; // not const until wiring is confirmed, so that we can play with config to confirm wiring.


#include "ledString.h" //FastLED stuff, uses LEDStringType
//the pixel memory: //todo: go back to dynamically allocated to see if that was fine.
CRGB pixel[VortexFX.total];

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
    bool showem = true;

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

    Packet outgoing() const {
      return Packet {(&endMarker - reinterpret_cast<const uint8_t *>(prefix)), *reinterpret_cast<const uint8_t *>(prefix)};
    }

    /** expect the whole object including prefix */
    bool isValidMessage(const Packet& msg) const {
      auto expect = outgoing();
      if (TRACE) {
        Serial.printf("Incoming: %u %s\n", msg.size, &msg.content);
        Serial.printf("Expectin: %u %s\n", expect.size, &expect.content);

      }
      return msg.size >= expect.size && 0 == memcmp(&msg.content, &expect.content, sizeof(prefix));
    }

    Body incoming()  {
      return Body {(&endMarker - &startMarker), startMarker};
    }

    /** for efficiency this presumes you got a true from isValidMessage*/
    bool parse(const Packet &packet)  {
      auto buffer = incoming();
      memcpy(&buffer.content, &packet.content + sizeof(prefix), buffer.size);
      dataReceived = true;
      return true;//no further qualification at this time
    }

    bool accept(const Packet &packet) {
      if (isValidMessage(packet)) {
        return parse(packet);
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
