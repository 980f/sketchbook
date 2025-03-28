#pragma once

#include "block.h"
#include "vortexLighting.h"

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
};

ColorSet foreground;
//ColorSet background; //need some lights to stay on so the room has some illumination.

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
    case 1: //rainbow
      p.offset = si;
      p.run = 1;
      p.period = numStations;
      p.sets = VortexFX.total / numStations;
      p.modulus = 0;
      break;

  }
  return p;
}

#include "remoteGpio.h"
#include "leverSet.h"

struct Boss : public VortexCommon {
    //puzzle state drive by physical inputs
    bool leverState[numStations];//paces sending

    bool remoteSolved = false;
    bool remoteReset = false;

    LeverSet lever;
    RemoteGPIO::Message levers2;//also levers :)  //static for debug convenience, should be member of Boss

    DebouncedInput forceSolved;//(23, false, 1750); //large delay for dramatic effect, and so button can be dropped before the action occurs.
    //timing
    Ticker timebomb; // if they haven't solved the puzzle by this amount they have to partially start over.
    Ticker autoReset; //ensure things shut down if the operator gets distracted
    Ticker refreshRate; //sendall occasionally to deal with any intermittency in LED connection

    /*light management
       holdoff and updateAllowed keeps us from overrunning lighting worker
       Background paints the "slightly broken" lighting, so that tunnel is not too dark
       needsupdate flags solved stations that might need their associate lighting updated
    */
    Ticker holdoff; //maximum frame rate, to keep from overrunning worker and maybe losing updates.
    bool updateAllowed = true;//latch for holdoff.done()

    struct BackgroundIlluminator {
      unsigned inProgress = 0; //state machine
      VortexLighting::Message msg;

      //config:
      unsigned every = 7; //tunable by debug, chould be const or NV
      unsigned steps = 1;

      void erase() {
        inProgress = steps;
      }

      /* //only call when updateAllowed is true
          @returns true if its message should be sent.
      */
      bool check() {
        if (inProgress > 0) {
          ++msg.sequenceNumber;
          msg.pattern.offset = 3 * inProgress;
          msg.pattern.run = 1;
          msg.pattern.period = every;
          msg.pattern.modulus = 0;
          msg.pattern.sets = VortexFX.total / msg.pattern.period;
          msg.showem = true;//jam this guy until we find a performance issue
          return true;
        }
        return false;
      }
      bool doneIf(bool succeeded) {
        if (!inProgress) {
          return true;//been done, or never started
        }
        if (--inProgress) {
          return false;//not done yet, expect check() to be called soon
        }
        return true;//just got done
      }
    } backgrounder;

    bool needsUpdate[numStations];
    unsigned lastStationSent = 0;//counter for needsUpdate

    // end lighting group
    //////////////////////////////////

    void refreshLeds() {
      backgrounder.erase();
      ForStations(si) {
        needsUpdate[si] = lever[si];//only rewrite those that are on, the rest are left in background state.
      }
      refreshRate.next(REFRESH_RATE_MILLIS);
    }

    void startHoldoff() {
      if (frameRate) {
        updateAllowed = false;
        holdoff.next(1000 / frameRate);
      } else {
        updateAllowed = true;
      }
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

    void leverAction(LeverSet::Event event) {
      switch (event) {
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

    /* check levers*/
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

    VortexLighting::Message echoAck;//not expecting these as of QN2025 first night
    void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {
      if (levers2.accept(Packet{len, *data})) { //trusting network to frame packets, and packet to be less than one frame
        if (TRACE) {
          Serial.println("Got GPIO message:");
          levers2.printTo(Serial);
          if (BUG3) {
            dumpHex(len, data, Serial);
          }
        }
      } else if (echoAck.accept(Packet{len, *data})) {//todo: different object to receive incoming lighting requests.
        //defer to loop, Serial prints are a burden on the onReceive stuff.
      } else {
        if (TRACE) {
          Serial.printf("Boss at %p doesn't know what the following does:\n", this);
        }
        BroadcastNode::onReceive(data, len, broadcast);
      }
    }

    void onSent(bool succeeded) override {
      if (backgrounder.doneIf(succeeded)) {
        needsUpdate[lastStationSent] = !succeeded;//if failed still needs to be updated, else it has been updated.
      }
    }


  public:

    Boss(): forceSolved(23, false, 1750) {}

    void setup() {
      lever.setup(50); // todo: proper source for lever debounce time
      relay.setup();
      timebomb.stop(); // in case we call setup from debug interface
      autoReset.stop();
      refreshLeds(); //include background 'erase'
      spew = true;//bn debug flag
      if (!BroadcastNode::begin(true)) {
        Serial.println("Broadcast begin failed");
      }
      Serial.println("Boss Setup Complete");
    }

    void loop() {
      // local levers are tested on timer tick, since they are debounced by it.
      if (flagged(levers2.dataReceived)) { // message received
        if (TRACE) {
          Serial.println("Processing remote gpio");
        }
        unsigned prior = lever.numSolved();
        //for levers2 stuff the solved bits in the initial leverStates.
        ForStations(each) {
          if (levers2[each]) {//set on true, leave as is on false.
            if (TRACE) {
              Serial.printf("Setting lever %u\n", each);
            }
            lever[each] = true;
          }
        }

        leverAction(lever.computeEvent(prior, lever.numSolved()));

        if (changed(remoteReset, levers2[numStations])) {
          checkRun(remoteReset);
        }

        if (changed(remoteSolved, levers2[numStations + 1])) {
          if (remoteSolved) {
            onSolution();
          }
        }
      }

      if (flagged(echoAck.dataReceived)) {
        if (TRACE) {
          Serial.println("Boss received S41L message");
          echoAck.printTo(Serial);
        }
      }

      if (updateAllowed) { //can't send another until prior is handled, this needs work.
        if (backgrounder.check()) {
          sendMessage(backgrounder.msg); //the ack from the espnow layer clears the needsUpdate at the same time as 'messageOnWire' is set.
          startHoldoff();
        } else if (refreshColors()) { //updates "needsUpdate" flags, returns whether at least one does.
          ForStations(justcountem) {//"justcountem" is belt and suspenders, not trusting refreshColors to tell us the truth.
            if (++lastStationSent >= numStations) {
              lastStationSent = 0;
            }
            if (needsUpdate[lastStationSent]) {
              if (leverState[lastStationSent]) {
                command.color = foreground[lastStationSent];
                command.pattern = pattern(lastStationSent, clistate.patternIndex);
              }
              command.showem = true; //todo: only with last one.
              ++command.sequenceNumber;//mostly to see if connection is working
              sendMessage(command); //the ack from the espnow layer clears the needsUpdate at the same time as 'messageOnWire' is set.
              startHoldoff();
              break;//onSent gets us to the next station to update
            }
          }
        }
        //else we will eventually get back to loop() and to here to review what needs to be sent.
      }
    }


    void onTick(MilliTick now) {
      if (holdoff.done()) {
        updateAllowed = true;
      }
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

      leverAction(lever.onTick(now));

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
