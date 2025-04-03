#pragma once
#include "vortexLighting.h"

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
    if (message.accept(Packet{len, *data})) { //trusting network to frame packets, and packet to be less than one frame
      //all action is in loop();
    } else {
      Serial.println("Not my type of packet");
      BroadcastNode::onReceive(data, len, broadcast);
    }
  }

  void loop() {
    if (message.dataReceived) {
      if (TRACE) {
        if (BUG3) {
          dumpHex(message.incoming(), Serial);
        }
        command.printTo(Serial);
      }
      VortexLighting::loop();
      sendMessage(message);//echo it back.
    }
  }

  // this is called once per millisecond, from the arduino loop().
  // It can skip millisecond values if there are function calls which take longer than a milli.
  void onTick(MilliTick ignored) {
    // not yet animating anything, so nothing to do here.
  }
};
