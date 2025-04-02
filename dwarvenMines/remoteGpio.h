#pragma once

/**
  Core of remote digital input device.
  First rendition 8 digital inputs

  Not much will be configurable initially

  lolin32 lite: 18,19,23,25,26,27,32,33,22
  25 seemed unreliable due to undocumented use of pin during wifi or esp_now init, works if we re-init after startup but if an acitve source were attached it might get angry (always use a series resistor to attach an active source).

*/
#include "simpleDebouncedPin.h"
#include "simpleTicker.h" //send periodically even if no change, but also on change
#include "broadcastNode.h"
#include "scaryMessage.h"


struct RemoteGPIO: BroadcastNode {

    static constexpr unsigned numInputs = 8;
#define ForPin(index) for(unsigned index = RemoteGPIO::numInputs ;index-->0;)

    //  unsigned pinNumber[numRemoteGPIO];
    DebouncedInput gpio[numInputs] {{18}, {19}, {23}, {25}, {26}, {27}, {32}, {33}};
    Ticker periodically;
    MilliTick period = 100;

    bool spew = false;


    struct Content: Printable {
      uint8_t padding[3]; //see if we can name the padding!
      unsigned sequenceNumber = 0;//for debug or stutter detection
      bool value[numInputs];

      bool &operator[](unsigned index) {
        static bool trash;
        if (index < numInputs ) {
          return value[index];
        } else {
          return trash;
        }
      }
      ///////////////////////////
      size_t printTo(Print &stream) const override {
        size_t length = stream.printf("\tSequence#:%u\n", sequenceNumber);
        ForPin(index) {
          length += stream.printf("\t[%u]=%x", index, value[index]);
        }
        return length + stream.println();
      }

    };


    struct Message: public ScaryMessage<Content> {
      Message(): ScaryMessage {'G','P','I','O'} {}

      bool &operator[](unsigned index) {
        static bool trash;
        if (index < numInputs ) {
          return m.value[index];
        } else {
          return trash;
        }
      }
    };

    Message toSend;
    Content &report{toSend.m};

    bool shouldSend = false;//todo: add this to ScaryMessage as most implementations will find it useful.

    void onTick() {
      unsigned numChanges = 0;
      ForPin(index) {
        if (gpio[index].onTick()) {
          ++numChanges;
        }
        report[index] = bool(gpio[index]);
      }
      if (numChanges > 0) {
        shouldSend = true;
      }
      if (periodically.done()) {
        shouldSend = true;
      }
    }

    /** this method was written to deal with "D25 stuck low" esp32 wifi bug */
    void forceMode(unsigned mode = INPUT_PULLUP) {
      ForPin(index) {
        SimplePin(gpio[index].pin).setup(mode);
      }
    }

    RemoteGPIO(): BroadcastNode(BroadcastNode_Triplet) {}

    bool setup(MilliTick bouncer = 50) {
      ForPin(index) {
        gpio[index].filter(bouncer);
        gpio[index].setup(true);//true here makes the pin report that it has just changed to whatever its present value is.
      }
      periodically.next(0);//send immediately with initial values.
      return BroadcastNode::begin(true);
    }

    void post() {
      ++toSend.m.sequenceNumber;//for debug
      Serial.print(toSend);
      send_message(toSend.outgoing());
      periodically.next(period);
    }

    void loop() {
      if (flagged(shouldSend)) {
        post();
      }
    }

};
