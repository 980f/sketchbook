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

    DebouncedInput gpio[numInputs] {{18, false}, {19, false}, {23, false}, {25, false}, {26, false}, {27, false}, {32, false}, {33, false}};
    Ticker periodically;

    bool spew = false;

    struct Config {
      MilliTick period = Ticker::PerSeconds(1);
      //todo: mode, pin, polarity, debounce time for inputs, pulse width for outputs
    } cfg;

    ////////////////////
    struct Content {
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
      // while this function has the same signature as that required by Printable, making this class Printable makes it virtual and the virtual table might get copied depending upon the compiler.
      size_t printTo(Print &stream) const {
        size_t length = stream.printf("\tSequence#:%u\n", sequenceNumber);
        ForPin(index) {
          length += stream.printf("\t[%u]=%x", index, value[index]);
        }
        return length + stream.println();
      }
    };
    ///////////////////

    struct Message: public ScaryMessage<Content> {
      Message(): ScaryMessage {'G', 'P', 'I', 'O'} {}
    };

    Message toSend;
    Content &report{toSend.m};

    bool shouldSend = false;//todo: add this to ScaryMessage as most implementations will find it useful.

    void onTick(MilliTick now) {
      unsigned numChanges = 0;
      ForPin(index) {
        if (gpio[index].onTick(now)) {
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
      toSend.tag[0] = 'V'; //tag is presently purely for debug.
      toSend.tag[1] = '1';
      ForPin(index) {
        gpio[index].filter(bouncer);
        gpio[index].setup(true);//true here makes the pin report that it has just changed to whatever its present value is.
      }
      periodically.next(0);//send ASAP with initial values.
      return BroadcastNode::begin(true);
    }

    void post() {
      ++report.sequenceNumber;//for debug
      Serial.print(toSend);
      send_message(toSend.outgoing());
      periodically.next(cfg.period);
    }

    void loop() {
      if (flagged(shouldSend)) {
        post();
      }
    }

};
