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
  unsigned numStrips = 4;
  unsigned total = perRevolutionActual * numStrips;
} VortexFX;


#include "ledString.h" //FastLED stuff, uses LEDStringType
//the pixel memory: //todo: go back to dynamically allocated to see if that was fine. Did this, got a reboot loop for my trouble.
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

      //formats command, you still need to call sendMessage() if you are the boss or act() if worker
      void setAll(CRGB color) {
        color = color;
        pattern.offset = 0;
        pattern.run = VortexFX.total ;//all, in case the unused ones peek out from hiding.
        pattern.period = VortexFX.total ;
        pattern.sets = 1;
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
      Serial.printf("sendMessage: %u, %.*s  (%p)\n", block.size, sizeof(Message::prefix), &block.content, &block.content);
      if (BUG3) {
        dumpHex(block, Serial);
      }
    }
    send_message(block);
  }
};
