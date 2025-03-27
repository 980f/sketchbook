#pragma once


#include "block.h"
#include "stripper.h"

////////////////////////////////////////////
//enumerate your names, with numRelays as the last entry
enum RelayChannel { // index into relay array.
  VortexMotor,
  Lights_1,
  Lights_2,
  DoorRelease,
  numRelays    // marker, leave in last position.
};


struct RelayQuad {
  // boss side:
  SimpleOutputPin channel[numRelays] = {26, 22, 33, 32};//, 23}; //25 was broken on 'BDAC: all relays in a group to expedite "all off"

  SimpleOutputPin & operator [](unsigned enumeratedIndex) {
    if (enumeratedIndex < countof(channel)) {
      return channel[enumeratedIndex];
    } else {
      return clistate.onBoard;
    }
  }

  void setup() {
    for (unsigned ri = numRelays; ri-- > 0;) {
      channel[ri].setup();
    }
  }

} relay;


DebouncedInput Run {4, true, 1250}; //pin to enable else reset the puzzle. Very long debounce to give operator a chance to regret having changed it.

////////////////////////////////////////////////////////
// Boss config
const unsigned numStations = 6;
#define ForStations(si) for(unsigned si=0; si<numStations; ++si)
// time from first pull to restart
unsigned fuseSeconds = 3 * 60; // 3 minutes
unsigned REFRESH_RATE_MILLIS = 5 * 1000; //100 is 10 Hz, for 400 LEDs 83Hz is pushing your luck.
//time from solution to vortex halt, for when the operator fails to turn it off.
unsigned resetTicks = 87 * 1000;
unsigned frameRate = 16;

struct ColorSet: Printable {
  //to limit power consumption:
  uint8_t MAX_BRIGHTNESS = 80; // todo: debate whether worker limits received values or trusts the boss.

  CRGB Color[numStations] = {
    0xFF0000,
    0x00FF00,
    0x0000FF,
    0xFF00FF,
    0xFFFF00,
    0x00FFFF,
  };

  /** @returns reference to a color, using #0 for invalid indexes */
  CRGB &operator[](unsigned index) {
    return Color[index < numStations ? index : 0];
  }

  void all(CRGB same) {
    ForStations(si) {
      Color[si] = LedStringer::Off;
    }
  }

  static size_t printColorSet(const char *prefix, const CRGB *color, Print &stream) {
    size_t length = 0;
    if (prefix) {
      length += stream.print(prefix);
      length += stream.print(":");
    }
    ForStations(si) {
      length += stream.printf("\t[%u]=%06X", si, color[si].as_uint32_t());
    }
    length += stream.println();
    return length;
  }

  size_t printTo(Print &stream) const override {
    return printColorSet(" Colors", Color, stream);
  }
} station;

ColorSet background;

#include "leverSet.h"

///////////////////////////////////////////////////////////////////////
LedStringer::Pattern pattern(unsigned si, unsigned style = 0) { //station index
  LedStringer::Pattern p;
  switch (style) {
    case 0: //half ring
      p.offset = ((si / 2) + 1) * VortexFX.perRevolutionActual; //which ring, skipping first
      if (si & 1) {
        p.offset += VortexFX.perRevolutionVisible / 2; //half of the visible
      }

      //set this many in a row,must be at least 1
      p.run = (VortexFX.perRevolutionVisible / 2) + (si & 1); //halfsies, rounded
      //every this many, must be greater than or the same as run
      p.period = p.run;
      //this number of times, must be at least 1
      p.sets = 1;
      //Runner will apply this modulus to its generated numbers
      p.modulus = VortexFX.perRevolutionActual;
      break;
    case 1: //
      p.offset = si;
      p.run = 1;
      p.period = numStations;
      p.sets = VortexFX.total;
      p.modulus = 0;
      break;
  }
  return p;
}

#include "remoteGpio.h"
RemoteGPIO::Message levers2;//also levers :)  //static for debug convenience, should be member of Boss

struct Boss : public BroadcastNode {
    LeverSet lever;
    Ticker timebomb; // if they haven't solved the puzzle by this amount they have to partially start over.
    Ticker autoReset; //ensure things shut down if the operator gets distracted
    Ticker refreshRate; //sendall occasionally to deal with any intermittency.
    Ticker holdoff; //maximum frame rate, to keep from overrunning worker and maybe losing updates.
    bool updateAllowed = true;//latch for holdoff.done()
    bool needsUpdate[numStations];
    DebouncedInput forceSolved;//(23, false, 1750); //large delay for dramatic effect, and so button can be dropped before the action occurs.

    void refreshLeds() {
      refreshRate.next(REFRESH_RATE_MILLIS);
      ForStations(si) {
        needsUpdate[si] = true;
      }
      stringState.showem = true; //available to optimize when we know many need to be set.
    }

    void startHoldoff() {
      if (frameRate) {
        updateAllowed = false;
        holdoff.next(1000 / frameRate);
      } else {
        updateAllowed = true;
      }
    }

    void sendMessage(const DesiredState &msg) {
      auto block = msg.outgoing();
      if (TRACE) {
        Serial.printf("sendMessage: %u, %.4s  (%p)\n", block.size, &block.content, &block.content);
        if (BUG3) {
          dumpHex(block, Serial);//#yes, making a Packet just to tear it apart seems like extra work, but it provides an example of use and a compile time test of source integrity.
        }
      }
      send_message(block);
    }

    bool leverState[numStations];//paces sending
    //for pacing sending station updates
    unsigned lastStationSent = 0;
    bool remoteSolved = false;

    bool remoteReset = false;
    bool dataReceived = false;

    void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {
      if (levers2.accept(Packet{len, *data})) { //trusting network to frame packets, and packet to be less than one frame
        if (TRACE) {
          Serial.println("Got GPIO message:");
          levers2.printTo(Serial);
          if (BUG3) {
            dumpHex(len, data, Serial);
          }
        }
        dataReceived = true;
        //can insert here a check on a DesiredState and receive an echo if the stripper sends one.
      } else if (stringState.accept(Packet{len, *data})) {
        Serial.println("primary received S41L message"); 
        stripper.dataReceived = true;
      } else {
        if (TRACE) {
          Serial.printf("Boss doesn't know what the following does: (%p)\n", this);
        }
        BroadcastNode::onReceive(data, len, broadcast);
      }
    }

  public:

    Boss(): BroadcastNode(BroadcastNode_Triplet), forceSolved(23, false, 1750) {}

    void setup() {
      lever.setup(50); // todo: proper source for lever debounce time
      relay.setup();
      timebomb.stop(); // in case we call setup from debug interface
      autoReset.stop();
      refreshLeds();
      spew = true;//bn debug flag
      if (!BroadcastNode::begin(true)) {
        Serial.println("Broadcast begin failed");
      }
      Serial.println("Boss Setup Complete");
    }


    void loop() {
      // levers are tested on timer tick, since they are debounced by it.
      if (flagged(dataReceived)) { // message received
        if (TRACE) {
          Serial.println("Processing remote gpio");
        }
        //for levers2 stuff the solved bits in the initial leverStates.
        ForStations(each) {
          if (levers2[each]) {//set on true, leave as is on false.
            if (TRACE) {
              Serial.printf("Setting lever %u\n", each);
            }
            lever[each] = true;
          }
        }

        if (changed(remoteReset, levers2[numStations])) {
          checkRun(remoteReset);
        }

        if (changed(remoteSolved, levers2[numStations + 1])) {
          if (remoteSolved) {
            onSolution();
          }
        }
      }

      if (refreshColors()) { //updates "needsUpdate" flags, returns whether at least one does.
        if (updateAllowed) { //can't send another until prior is handled, this needs work.
          ForStations(justcountem) {//"justcountem" is belt and suspenders, not trusting refreshColors to tell us the truth.
            if (++lastStationSent >= numStations) {
              lastStationSent = 0;
            }
            if (needsUpdate[lastStationSent]) {
              if (leverState[lastStationSent]) {
                stringState.color = station[lastStationSent];
                stringState.pattern = pattern(lastStationSent, clistate.patternIndex);
              } else {
                stringState.color = background[lastStationSent];
                stringState.pattern = pattern(lastStationSent, clistate.patternIndex);
              }
              stringState.showem = true; //todo: only with last one.
              ++stringState.sequenceNumber;//mostly to see if connection is working
              sendMessage(stringState); //the ack from the espnow layer clears the needsUpdate at the same time as 'messageOnWire' is set.
              startHoldoff();
              break;
            }
          }
        }
        //else we will eventually get back to loop() and review what needs to be sent.
      }
    }

    void onSent(bool succeeded) override {
      needsUpdate[lastStationSent] = !succeeded;//if failed still needs to be updated, else it has been updated.
    }

    bool refreshColors() {
      unsigned diffs = 0;
      ForStations(si) {
        if (changed(leverState[si], lever[si])) {
          needsUpdate[si] = true;
          ++diffs;
        }
      }
      return diffs > 0;
    }

    void onSolution() {
      dbg.cout("Yippie!");
      timebomb.stop();
      relay[DoorRelease] << true;
      relay[VortexMotor] << true;
      //todo: any other bells or whistles?
      //timer for vortex auto off? perhaps one full minute just in case operator gets distracted?
      autoReset.next(resetTicks);
    }

    void resetPuzzle() {
      autoReset.stop();
      timebomb.stop();
      relay[DoorRelease] << false;
      relay[VortexMotor] << false;
      lever.restart();//todo: perhaps more thoroughly than for timebomb?
    }

    void checkRun(bool beRunning) {
      if (beRunning) {
        //allow puzzle to operate
        if (EVENT) {
          Serial.println("Puzzle running.");
        }
      } else {
        if (EVENT) {
          Serial.println("Manually resetting puzzle");
        }
        resetPuzzle();
      }
    }

    void onTick(MilliTick now) {
      if (autoReset.done()) {
        if (EVENT) {
          Serial.println("auto reset fired, resetting puzzle");
        }
        resetPuzzle();
      }

      if (timebomb.done()) {
        if (EVENT) {
          Serial.println("Timed out solving puzzle, resetting lever state");
        }
        lever.restart();
        return;
      }

      if (refreshRate.done()) {
        if (TRACE) {
          Serial.println("Periodic resend");
        }
        refreshLeds();
      }

      switch (lever.onTick(now)) {
        case LeverSet::Event::NonePulled: // none on
          break;
        case LeverSet::Event::FirstPulled: // some pulled when none were pulled
          if (EVENT) {
            Serial.println("First lever pulled");
          }
          timebomb.next(fuseSeconds * 1000);
          break;
        case LeverSet::Event::SomePulled: // nothing special, but not all off
          break;
        case LeverSet::Event::LastPulled:
          if (EVENT) {
            Serial.println("Last lever pulled");
          }
          onSolution();
          break;
      }

      if (Run.onTick()) {
        checkRun(Run.pin);
      }

      if (forceSolved.onTick()) {
        if (forceSolved) {
          onSolution();
        }
      }
    }// end tick

};
