#pragma once

/**
    vortex lights interface
*/
#define BroadcastNode_WIFI_CHANNEL 6
#include "broadcastNode.h"
#include "scaryMessage.h"
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
    struct Command {
      unsigned sequenceNumber = ~0;
      CRGB color = LedStringer::Off;
      LedStringer::Pattern pattern;
      bool showem = true;
      size_t printTo(Print &stream) const {
        size_t length = stream.printf("Sequence#:%u\t", sequenceNumber);
        length += stream.printf("Show em:%x\t", showem);
        length += stream.printf("Color:%06X\n", color.as_uint32_t());
        //    length += stream.print("Pattern:\t") stream.print(pattern);
        length += pattern.printTo(stream);
        return length;
      }
    };

    struct Message: public ScaryMessage<Command> {
      Message(): ScaryMessage {'V', 'O', 'R', '1'} {}//FYI "VOR" is the name of a SciFi story from the 60's about the difficulty in establishing communication with an alien species.
    };

    Message message;
    Command & command {message.m};
    LedStringer stringer;

    void setup() {
      LedStringer::spew = &Serial;
      stringer.setup(VortexFX.total, pixel);
    }

    void act() {
      if (EVENT) {
        command.printTo(Serial);
      }
      stringer.setPattern(command.color, command.pattern);
      if (flagged(command.showem)) {
        stringer.show();
      }
    }

    void loop() {
      if (flagged(message.dataReceived)) { // message received
        act();
      }
    }
};

///////////////////////////////////////////////////////////////////////
// A user of lighting facility. Can move a bit more logic from Boss and Stripper into here.

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
