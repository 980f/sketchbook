#pragma once

/**
  business logic of QN2025 vortex special effects.

*/

#include "block.h"
#include "vortexLighting.h" //common parts of coordinator and light manager devices

////////////////////////////////////////////
//enumerate your names, with numRelays as the last entry
enum RelayChannel { // index into relay array.
  Audio,
  VortexMotor,
  SpareNC,
  DoorRelease,
  numRelays    // marker, leave in last position.
};

struct RelayQuad {
  // boss side:
  SimpleOutputPin channel[numRelays] = {26, 22, 33, 32};//, 23}; //25 was broken on 'BDAC: all relays in a group to expedite "all off": actually D25 has a generic problem with wifi as input, perhaps as output as well.

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

  void all(bool on) {
    for (unsigned ri = numRelays; ri-- > 0;) {
      channel[ri] << on;
    }
  }

} relay;

////////////////////////////////////////////////////////
// Boss config
const unsigned numStations = 6;
#define ForStations(si) for(unsigned si=0; si<numStations; ++si)


struct ColorSet: Printable {

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



#include "remoteGpio.h"
#include "leverSet.h"

template <unsigned Size> struct PermutationSet: Printable {
  unsigned scramble[Size];
  unsigned operator[](unsigned raw) {
    if (raw < Size) {
      return scramble[raw];
    }
    return raw;//GIGO
  }

  PermutationSet(const std::initializer_list<unsigned> &list) {
    unsigned index = 0;
    for (auto item : list) {
      scramble[index++] = item;
    }
  }

  size_t printTo(Print &stream) const override {
    unsigned length = 0;
    for (unsigned index = 0; index < Size; ++index) {
      length += stream.printf("\t[%d]=%d", index, scramble[index]);
    }
    return length;
  }

};

//todo: the local lever inputs need their own, move this into leverset once that is a base class, or pushed io instead of polled.
///////////////////////////////////////////////////////////////////////
/** outside the class so that we can configure before instantiating an instance*/
struct BossConfig: Printable {
  // time from first pull to restart
  MilliTick fuseTicks = Ticker::Never; //Ticker::PerMinutes(3);
  MilliTick refreshPeriod = Ticker::PerSeconds(5); //100 is 10 Hz, for 400 LEDs 83Hz is pushing your luck.
  
  //time from solution to vortex halt (and audio off, and door reenable), for when the operator fails to turn it off.
  MilliTick resetTicks = Ticker::PerSeconds(61);
  //time from solution to vortex/door open, so that audio track can fully cue up:
  MilliTick audioLeadinTicks = 7990; //experimental
  
  unsigned frameRate = 16;
  ColorSet foreground;
  PermutationSet<numStations> gpioScramble {0, 2, 3, 5, 4, 1};//empirically determined for remote GPIO
  CRGB overheadLights = CRGB{60, 60, 70};
  unsigned overheadStart = VortexFX.perRevolutionActual;
  unsigned overheadWidth = 3;
  //and finally to check if EEPROM is valid:
  uint16_t checker = 0x980F;

  bool isValid() const {
    return checker == 0x980F; //really simple, someday do CRC and check that.
  }

  size_t printTo(Print &stream) const override {
    return stream.printf("t:%d, z:%d, y:%d, x:%d, f:%u, \n\t~c:%06X, k:[%u,]%u\n", fuseTicks, refreshPeriod, resetTicks, audioLeadinTicks, frameRate, overheadLights.as_uint32_t(), overheadStart , overheadWidth)
           + stream.print(foreground)
           + stream.printf("\nremote gpio order:\n")
           + stream.print(gpioScramble)
           //and finally to check if EEPROM is valid:
           + stream.printf("\nChecker:%u\n", checker )
           ;
  }
};

BossConfig cfg;

struct Boss : public VortexCommon {

    enum class Puzzle {
      Idle,
      Partial,
      StartDrill,
      Drilling,
      Done
    } puzzle = Puzzle::Idle;

    //puzzle state drive by physical inputs,
    //state change input trackers, not the actual state itself.
    bool leverState[numStations];//paces sending
    //state for sending lighting changes
    bool needsUpdate[numStations];
    unsigned lastStationSent = 0;//counter for needsUpdate
    bool remoteSolved = false;
    bool remoteReset = false;
    unsigned solved = 0;

    enum class Event {
      NonePulled,  // none on
      FirstPulled, // some pulled when none were pulled
      SomePulled,  // nothing special, but not all off
      LastPulled,  // all on
    };

    //physical inputs, first local, then remote
    LeverSet lever;
    DebouncedInput Run {4, true, 1250}; //pin to enable else reset the puzzle. Very long debounce to give operator a chance to regret having changed it.
    DebouncedInput forceSolved {23, false, 1750};//pin to declare puzzle solved. Very long debounce to give operator a chance to regret having changed it.
    RemoteGPIO::Message levers2;//'also levers'. //todo: send debounce values to GPIO device.
    RemoteGPIO::Content &remoteLever{levers2.m};

    //timing
    Ticker timebomb; // if they haven't solved the puzzle by this amount they have to partially start over.
    Ticker autoReset; //ensure things shut down if the operator gets distracted
    Ticker audioLeadin; //give time for audio to start and run to machine moving noises.
    Ticker refreshRate; //sendall occasionally to deal with any intermittency in LED connection

    /*light management
       holdoff and updateAllowed keeps us from overrunning lighting worker
       Background paints the "slightly broken" lighting, so that tunnel is not too dark
       needsupdate flags solved stations that might need their associate lighting updated
    */
    Ticker holdoff; //maximum frame rate, to keep from overrunning worker and maybe losing updates.
    bool updateAllowed = true;//latch for holdoff.done()

    /** using some LED's to light interior of vortex */
    struct BackgroundIlluminator {
      unsigned inProgress = 0; //state machine
      Message wrapper;
      Command &command {wrapper.m};

      /** start the erasure procedure */
      void erase() {
        wrapper.tag[0] = 'B';
        wrapper.tag[1] = 0;
        inProgress = 2;
      }

      void nightlight() {
        inProgress = 1;
      }
      /* only call when updateAllowed is true
          @returns true if its message should be sent.
      */
      bool check() {
        switch (inProgress) {
          case 2:     //all off
            command.setAll(LedStringer::Off);
            break;
          case 1:   //configured as one chunk per ring.
            command.color = cfg.overheadLights;
            command.pattern.offset = cfg.overheadStart;
            command.pattern.run = cfg.overheadWidth ? cfg.overheadWidth : 1; //chasing down erroneous 0's
            command.pattern.period = VortexFX.perRevolutionActual;//one group per ring
            command.pattern.sets = 3;//3 rings.
            break;
          default:
            return false;
        }
        ++command.sequenceNumber;
        command.pattern.modulus = 0;//broken, ensure it is not used
        command.showem = true;//jam this guy until we find a performance issue
        wrapper.tag[1] = '0' + inProgress;
        if (BUG3) {
          Serial.printf("BGND: step %d\t", inProgress);
          command.printTo(Serial);
        }
        return true;//relies upon doneIf being called by send acknowledgment to decrement inProgress
      }

      bool doneIf(bool succeeded) {
        if (!inProgress) {
          return true;//been done, or never started
        }
        if (succeeded) {
          --inProgress;
        }
        return false;
      }
    } backgrounder;

    //////////////////////////////////
    LedStringer::Pattern pattern(unsigned si, unsigned style = 1) { //station index
      LedStringer::Pattern p;
      si = cfg.gpioScramble[si];
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
          break;
        case 1: //rainbow
          p.offset = VortexFX.perRevolutionActual + si;//omit first ring, offset others a bit just to see the effect
          p.run = 1;
          p.period = numStations;
          p.sets = (VortexFX.total - VortexFX.perRevolutionActual) / numStations;
          //try to preserve the nightlights:
          if(si<cfg.overheadWidth){//then there is an overlap
            p.offset += p.period;
            --p.sets;
          }
          break;
      }
      //defeat broken logic: was getting applied to offset as well as the rest of the pattern computation and that is just useless.
      p.modulus = 0;
      return p;
    }
    
    // set lever related LED's again, in case update got lost or lighting processor spontaneously restarted.
    void refreshLeds() {
      ForStations(si) {
        needsUpdate[si] = lever[si];//only rewrite those that are on, the rest are left in background state.
      }
      refreshRate.next(cfg.refreshPeriod);
    }

    //do not overrun lighting processor, not until it develops an input queue.
    void startHoldoff() {
      holdoff.next(Ticker::forHertz(cfg.frameRate));//deal with zero frameRate
      updateAllowed = !holdoff.isRunning();
    }

    void onSolution(const char *cause) {
      puzzle = Puzzle::StartDrill;
      Serial.printf("Solved due to %s:\t at %u, delay is set to %u\n", cause, Ticker::now, cfg.audioLeadinTicks);
      timebomb.stop();
      audioLeadin.next(cfg.audioLeadinTicks);
      relay[Audio] << true; //audio needs time to get to where motor sounds start      
      relay[SpareNC] << true; //JIC
      autoReset.next(cfg.resetTicks);
    }

    void onAudioCue() {
      if (EVENT) {
        Serial.printf("Audio Done at %u\n", Ticker::now);
      }
      puzzle = Puzzle::Drilling;
      relay[DoorRelease] << true;
      relay[VortexMotor] << true;
    }

    void resetPuzzle() {
      puzzle = Puzzle::Idle;
      autoReset.stop();
      timebomb.stop();
      relay.all(false);
      lever.restart();
      solved = 0;
      backgrounder.erase();
    }

    Event computeEvent() {
      bool wereNone = solved == 0;
      if (changed(solved, lever.numSolved())) {
        if (solved == numStations) {
          return Event::LastPulled; // takes priority over FirstPulled when simultaneous
        }
        if (wereNone) {//was used to trigger timeout that is now disabled by default
          return Event::FirstPulled;
        }
        return Event::SomePulled; // a different number but nothing special.
      } else { // no substantial change
        return solved ? Event::SomePulled : Event::NonePulled;
      }
    }


    void leverAction(Event event) {
      switch (event) {
        case Event::NonePulled: // none on
          break;
        case Event::FirstPulled: // some pulled when none were pulled
          if (EVENT) {
            Serial.println("First lever pulled");
          }
          timebomb.next(cfg.fuseTicks);
          break;
        case Event::SomePulled: // nothing special, but not all off
          break;
        case Event::LastPulled:
          if (EVENT) {
            Serial.println("Last lever pulled");
          }
          onSolution("Lever check");
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
          if (leverState[si]) {
            needsUpdate[si] = true;
            ++diffs;
          }
        }
      }
      return diffs > 0;
    }

    VortexLighting::Message echoAck;//not expecting these as of QN2025 first night
    void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {
      Packet incoming {len, *data};
      if (TRACE) {
        Serial.printf("Incoming: %u %s\n", incoming.size, &incoming.content);
      }
      //in the below the accept sets flags for loop() to inspect.
      if (levers2.accept(incoming)) { //trusting network to frame packets, and packet to be less than one frame
        addJustReceived = true;
        //spew formerly here is on a different thread than loop() and messages got interleaved.
      } else if (echoAck.accept(incoming)) {
        addJustReceived = true;
        //defer to loop for echo testing
      } else {
        if (TRACE) {
          Serial.printf("Boss at %p doesn't know what the following does:\n", this);
        }
        BroadcastNode::onReceive(data, len, broadcast);
      }
    }

    void onSent(bool succeeded) override {
      if (EVENT) {
        if (!succeeded) {
          Serial.printf("Failed send with bg:%d, lSS:%d\n", backgrounder.inProgress, lastStationSent);
        }
      }
      if (backgrounder.doneIf(succeeded)) {//backgrounder will ignore succeeded if it isn't active and will return true.
        needsUpdate[lastStationSent] = !succeeded;//if failed still needs to be updated, else it has been updated.
      }
    }

  public:

    //    Boss() {}

    void setup() {
      VortexCommon::setup();
      message.tag[0] = 'L';
      message.tag[1] = 0;

      lever.setup(50); // todo: proper source for lever debounce time
      relay.setup();
      timebomb.stop(); // in case we call setup from debug interface
      autoReset.stop();
      audioLeadin.stop();
      ForStations(si) {
        needsUpdate[si] = true;
      }
      resetPuzzle();
      spew = true;//bn debug flag
      if (!BroadcastNode::begin(true)) {
        Serial.println("Broadcast begin failed");
      }
      Serial.printf("Background Lighting: %p\n", &backgrounder.wrapper);
      Serial.printf("Lever Lighting: %p\n", &message);
      Serial.println("Boss Setup Complete");
    }

    void applyLights(Message &light) {//not const so that tags can be diddled
      apply(light);//sends to remote
      startHoldoff();
    }
    
    void loop() {
      // local levers are tested on timer tick, since they are debounced by it.
      if (flagged(levers2.dataReceived)) { // message received
        if (TRACE) {
          Serial.println("Processing remote gpio");
          levers2.printTo(Serial);
          if (BUG3) {
            dumpHex(levers2.incoming(), Serial);
          }
        }
        //for levers2 stuff the solved bits of the local lever trackers
        ForStations(si) {
          if (remoteLever[si]) {//set on true, leave as is on false.
            if (TRACE) {
              Serial.printf("Setting lever %u\n", si);
            }
            lever[si] = true;
          }
        }

        if (changed(remoteReset, remoteLever[numStations])) {
          checkRun(remoteReset);
        }

        if (changed(remoteSolved, remoteLever[numStations + 1])) {
          if (remoteSolved) {
            onSolution("Remote Override");
          }
        }
      }

      if (flagged(echoAck.dataReceived)) {
        if (TRACE) {
          Serial.printf("Boss received lighting message tagged:%s\n", echoAck.tag);
          echoAck.printTo(Serial);
        }
      }

      leverAction(computeEvent());

      if (updateAllowed) { //can't send another until prior is handled, this needs work.
        if (backgrounder.check()) {
          applyLights(backgrounder.wrapper);
        } else {
          refreshColors(); //updates "needsUpdate" flags, returns whether at least one does.
          ForStations(justcountem) {//keeping separate flag check loop counter in case we refresh faster than we can service it.
            if (++lastStationSent >= numStations) {//todo: use the cyclic counter.
              lastStationSent = 0;
            }
            if (needsUpdate[lastStationSent]) {
              if (leverState[lastStationSent]) {
                command.color = cfg.foreground[lastStationSent];
              } else {
                command.color = LedStringer::Off;//will occasionally zilch an overhead light
              }
              command.pattern = pattern(lastStationSent, clistate.patternIndex);
              command.showem = true; //todo: only with last one.
              message.tag[1] = '0' + lastStationSent;
              ++command.sequenceNumber;
              applyLights(message); //the ack from the espnow layer clears the needsUpdate
              break;//onSent gets us to the next station to update
            }
          }
          //no stations needed an update so ...
          //spammed lever 1          backgrounder.nightlight();//hack to keep some lights on at all times.
        }
      }//end updateAllowed

    }//end loop()

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

      if (audioLeadin.done()) {
        onAudioCue();
      }

      lever.onTick(now);

      if (Run.onTick(now)) {
        checkRun(Run.pin);
      }

      if (forceSolved.onTick(now)) {
        if (forceSolved) {
          onSolution("Local override");
        }
      }
    }// end tick

};
