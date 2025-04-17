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
#include "configurateer.h"

//initial verison hard coded 8 low active inputs
unsigned lolin32_lite[] = {18, 19, 22, 25, 26, 27, 32, 33, 23, 16, 17 };
unsigned c3_super_mini[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}; //they made this simple :) fyi: 20,21 are uart

struct RemoteGPIO: BroadcastNode {
    bool spew = false;

    static constexpr unsigned numPins = 11;//todo: make this device specific, 11 is for C3-mini reserving uart0 lines
#define ForPin(index) for(unsigned index = RemoteGPIO::numPins;index-->0;)

    /** pin conifguration. defaults match first user (QN2025 dwarevenMines), and are reasonable for arcade switches */
    struct PinFig: Printable {
      uint8_t number;
      byte mode = INPUT_PULLUP;
      bool highActive = false;
      //byte padding;
      MilliTick ticks = 50; //debounce for input, pulsewidth for output, 0 is stay on, Never is don't ever go on.
      bool isOutput() const {
        return mode == OUTPUT;
      }
      bool isInput() const {
        return !isOutput();
      }

      size_t printTo(Print& p) const override {
        return p.printf("\t%u %c%c %u ms", number, highActive ? ' ' : '~', isOutput() ? 'O' : 'I', ticks);
      }
    };
    //////////////////////////////
    /** "pin dingus", dynamically configurable input with debounce or output that is a pulse, possible a perpetual pulse. */
    struct Pingus: public DebouncedInput {
      Pingus():DebouncedInput(~0){}

      
      bool amOutput = false;
      
      //will abuse io debouncer for pulse output
      void apply(const PinFig &fig) {
        amOutput = fig.isOutput();
        pinMode(fig.number, fig.mode);
        pin.activeHigh = fig.highActive;
        DebounceDelay = fig.ticks;
      }

      bool onTick(MilliTick now) {
        if (amOutput) {
          if (bouncing.done() ) {
            pin << false;
            stable = false;
            return true;
          } else {
            return false;
          }
        } else {
          return DebouncedInput::onTick(now);
        }
      }

      void trigger(bool bee) {
        if (bee) {
          pin << true;
          bouncing.next(DebounceDelay);
        } else {
          pin << false;
          bouncing.stop();
        }
        stable = bee;
      }

      //      size_t printTo(Print& p) const override {
      //        //todo: different if output
      //        return p.print(io);
      //      }
    };

    Pingus pingus[numPins];
    Ticker periodically;
    ////////////////////////////////////////
    struct Config: Printable {
      MilliTick refreshInterval = Ticker::Never;
      PinFig pin[numPins];
      size_t printTo(Print& p) const override {
        auto length = p.printf("Refresh: %d\n", refreshInterval);
        ForPin(pi) {
          length += p.print(pin[pi]);
        }
        return length + p.println();
      }
    };

    struct ConfigMessage: public ScaryMessage<Config, 7 /* strlen("GPIOCFG") */> {
      ConfigMessage(): ScaryMessage {'G', 'P', 'I', 'O', 'C', 'F', 'G'} {}
    } confMessage;

    Config &cfg{confMessage.m};

    void setupPins() {
      ForPin(pi) {
        pingus[pi].apply(cfg.pin[pi]);
      }
    }


    ////////////////////
    struct Content {
      unsigned sequenceNumber = 0;//for debug or stutter detection
      bool value[numPins];

      bool &operator[](unsigned index) {
        static bool trash;
        if (index < numPins ) {
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

    /** to report inputs, and also output state since we have pulsers, we use this guy */
    Message toSend;
    Content &report{toSend.m};
    /** to set outputs we receive this */
    Message command;
    Content &commanded{command.m};

    void applyOutputs() {
      //update output pins from command message
      ForPin(pi) {
        if (cfg.pin[pi].isOutput()) {
          pingus[pi].trigger(commanded.value[pi]);
        }
      }
    }


    void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {
      auto packet = Packet{len, *data};
      if (confMessage.accept(packet)) {
        //all action is in loop();
      } else if (command.accept(packet)) {
        //all action is in loop();
      } else {
        //perhaps restore reporting on other traffic
      }
    }


    void onTick(MilliTick now) {
      unsigned numChanges = 0;
      ForPin(index) {
        if (pingus[index].onTick(now)) {
          ++numChanges;
        }
        report[index] = pingus[index].stable;
      }
      if (numChanges > 0) {
        toSend.shouldSend = true;
      }
      if (periodically.done()) {
        toSend.shouldSend = true;
      }
    }

    // fix only pin 25 only on those devices with the problem. Mostly just avoid it.
    //    /** this method was written to deal with "D25 stuck low" esp32 wifi bug */
    //    void forceMode(unsigned mode = INPUT_PULLUP) {
    //      ForPin(index) {
    //        SimplePin(gpio[index].pin).setup(mode);
    //      }
    //    }

    RemoteGPIO(): BroadcastNode(BroadcastNode_Triplet) {}

    bool setup(MilliTick bouncer = 50) {
      toSend.tag[0] = 'V'; //tag is presently purely for debug.
      toSend.tag[1] = '2';
      //for C3 super-mini:


      periodically.next(0);//send ASAP with initial values.
      return BroadcastNode::begin(true);
    }

    void post() {
      ++report.sequenceNumber;//for debug
      Serial.print(toSend);
      send_message(toSend.outgoing());
      periodically.next(cfg.refreshInterval);
    }

    void loop() {
      if (toSend.dirty()) {
        post();
      }
      if (confMessage.hot()) {
        //update pin configurations
        setupPins();
      }
      if (command.hot()) {
        applyOutputs();
      }
    }

};
