/*
  punch list:
  vortex off: wait for run/reset change of state?
  mac diagnostic printout byte order
  configurable values to and from EEPROM, or store a string to run through the clirp.
  phase per ring of leds, an array of offsets
  sets of Patterns and use pattern for lever feedback, and test selector for pattern in clirp.
  done: and it was! [check whether a "wait for init" is actually needed in esp_now init]

  Note: 800 kbaud led update rate, 24 bits per LED, 33+k leds/second @400 LEDS 83 fps. 12 ms per update plus a little for the reset.
  10Hz is enough for special FX work, 24Hz is movie theater rate.

*/
#include "ezOTA.h" //setup wifi and allow for firmware updates over wifi
EzOTA flasher;
//esp stuff pollutes the global namespace with macros for some asm language operations.
#undef cli

const unsigned numStations = 6;
#define ForStations(si) for(unsigned si=0; si<numStations; ++si)

/// flags editable by cli:
const unsigned numSpams = 5;
bool spam[numSpams]; //5 ranges, can add more and they are not actually prioritized
#define TRACE spam[4]
#define BUG3 spam[3]
#define BUG2 spam[2]
#define EVENT spam[1]
#define URGENT spam[0]

// time from first pull to restart
unsigned fuseSeconds = 3 * 60; // 3 minutes
//time from solution to vortex halt, for when the operator fails to turn it off.
unsigned resetTicks = 87 * 1000;
unsigned REFRESH_RATE_MILLIS = 100; //100 is 10 Hz, for 400 LEDs 83Hz is pushing your luck.
// worker/remote pin allocations:
const unsigned LED_PIN = 13; // drives the chain of programmable LEDs
#define LEDStringType WS2811, LED_PIN, GRB

struct StripConfiguration {
  unsigned perRevolutionActual = 100;//no longer a guess
  unsigned perRevolutionVisible = 89;//from 2024 haunt code
  unsigned perStation = perRevolutionVisible / 2;
  unsigned numStrips = 4;
  unsigned total = perRevolutionActual * numStrips;
} VortexFX; // not const until wiring is confirmed, so that we can play with config to confirm wiring.

#include "ledString.h" //FastLED stuff, uses LEDStringType

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

/////////////////////////
// debug cli
#include "sui.h" //Command Line Interpreter, Reverse Polish expressions.
SUI dbg(Serial, Serial);

#include "simplePin.h"
#include "simpleTicker.h"
//debug trace flags
struct CliState {
  unsigned colorIndex = 0; // might use ~0 to diddle "black"
  unsigned leverIndex = ~0;//enables diagnostic spew on selected lever
  unsigned patternIndex = 0; //at the moment we have just one.
  SimpleOutputPin onBoard{2};//Wroom LED
  //  unsigned spewWrapper = 0;
  Ticker pulser;
  bool onTick() {
    if (!pulser.isRunning()) {
      onBoard << true;
      pulser.next(250);
      return true;
    } else if (pulser.done()) {
      pulser.next(onBoard.toggle() ? 2500 : 1500);
      return true;
    }
    return false;
  }
} clistate;

////////////////////////////////////////////
// Boss/main pin allocations:
#include "simpleDebouncedPin.h"

struct LeverSet: Printable {
    /**
       Each lever
    */
    struct Lever: Printable {
      // latched version of "presently"
      bool solved = 0;
      // debounced input
      DebouncedInput presently;

      unsigned pinNumber() const {
        return presently.pin.number;
      }
      // check up on bouncing.
      bool onTick() { // implements latched edge detection
        if (presently.onTick()) {   // if just became stable
          solved |= presently;
          return true;
        }
        return false;
      }

      // restart puzzle. If a lever sticks we MUST fix it. We cannot fake one.
      void restart() {
        solved = presently;
      }

      void setup(MilliTick bouncer) {
        presently.filter(bouncer);
      }

      Lever(unsigned pinNumber) : presently{pinNumber, false} {}

      size_t printTo(Print& p) const override {
        return p.print(solved ? "ON" : "off");
      }
    };//// end Lever class

    std::array<Lever, numStations> lever; // using std::array over traditional array to get initializer syntax that we can type

  public:
    enum class Event {
      NonePulled,  // none on
      FirstPulled, // some pulled when none were pulled
      SomePulled,  // nothing special, but not all off
      LastPulled,  // all on
    };

    Event onTick() {
      // update, and note major events
      unsigned prior = numSolved();
      for (unsigned index = numStations; index-- > 0;) {
        bool changed = lever[index].onTick();
        if (changed && clistate.leverIndex == index) {
          dbg.cout("lever", index, " just became: ", lever[index].presently, " latched: ", lever[index].solved);
        }
      }

      unsigned someNow = numSolved();
      if (someNow == prior) { // no substantial change
        return someNow ? Event::SomePulled : Event::NonePulled;
      }
      // something significant changed
      if (someNow == numStations) {
        return Event::LastPulled; // takes priority over FirstPulled when simultaneous
      }
      if (prior == 0) {
        return Event::FirstPulled;
      }
      return Event::SomePulled; // a different number but nothing special.
    }

    bool& operator[](unsigned index) {
      return lever[index].solved;
    }

    void restart() {
      dbg.cout("Lever::Restart");
      ForStations(index) {
        lever[index].restart();
      }
    }

    unsigned numSolved() const {
      unsigned sum = 0;
      ForStations(index) {
        //        if (lever[index].solved) {
        //          ++sum;
        //        }
        sum += lever[index].solved;
      }
      return sum;
    }

    void listPins(Print &stream) const {
      dbg.cout("Lever logical pin assignments");
      auto namedoesntmatter = dbg.cout.stackFeeder();
      ForStations(index) {
        stream.printf("\t%u:D%u", index, lever[index].pinNumber());
      }
      dbg.cout.endl();
    }

    void setup(MilliTick bouncer) {
      ForStations(index) {
        lever[index].presently.filter(bouncer);
      }
    }

    LeverSet() : lever{16, 17, 5, 18, 19, 21} {}//SET PIN ASSIGNMENTS FOR LEVERS HERE

    size_t printTo(Print& stream) const override {
      size_t length = 0;
      length += stream.print("Levers:");
      ForStations(index) {
        length += stream.print("\t");
        length += stream.print(index);
        length += stream.print(": ");
        length += stream.print(lever[index]);
      }
      length += stream.println();
      return length;
    }
};

// boss side:
SimpleOutputPin relay[] = {26, 25, 33, 32, 22, 23}; // all relays in a group to expedite "all off"

enum RelayChannel { // index into relay array.
  //there are spare outputs declared in relay[] but without an enum to pick them
  VortexMotor,
  Lights_1,
  Lights_2,
  DoorRelease, // to be replaced with text that is in comments at this time
  numRelays    // marker, leave in last position.
};

SimplePin IamBoss = {15}; // pin to determine what this device's role is (Boss or worker) instead of comparing MAC id's and declare the MAC ids of each end.
//23 is a corner pin: SimplePin IamReal = {23}; // pin to determine that this is the real system, not the single processor development one.
DebouncedInput Run = {4, true, 1250}; //pin to enable else reset the puzzle. Very long debounce to give operator a chance to regret having changed it.

#include "macAddress.h"
// known units, until we implement a broadcast based protocol.
std::array knownEsp32 = {//std::array can deduce type and count, but given type would not deduce count.
  MacAddress{0xD0, 0xEF, 0x76, 0x5C, 0x7A, 0x10},  //remote worker
  //  MacAddress{0xD0, 0xEF, 0x76, 0x58, 0xDB, 0x98}   //probably toasted
};

#include "nowDevice.h"
/////////////////////////////////////////
// Structure to convey data

struct DesiredState : public NowDevice::Message {
  /** this is added to the offset of each light */
  uint8_t startMarker = 1; //also version number

  /// body
#if MinesVersion==2
  unsigned sequenceNumber = 0;//could use start or endmarker, but leaving the latter 0 for systems that send ascii strings.
  CRGB color;
  Pattern pattern;
  bool showem = false;
  ///////////////////////////
  size_t printTo(Print &stream) {
    size_t length = 0;
    length += stream.printf("Sequence#:%u\t", sequenceNumber);
    length += stream.printf("Show em:%x\t", showem);
    length += stream.printf("Color:%06X\n", color);
    length += stream.print("Pattern:\t") stream.print(pattern);
    return length;
  }

#else
  unsigned vortexAngle; // 0 to 89 for 0 to 359 degrees of rotation.
  unsigned whichPattern = 0;
  unsigned sequenceNumber = 0;
  ColorSet color;
  ///////////////////////////////////
  // accessors
  CRGB &operator [](unsigned si) {
    return color[si];
  }

  size_t printTo(Print &stream) {
    size_t length = 0;
    length += stream.printf("Angle:%d\t", vortexAngle);
    length += stream.printf("Pattern:%u\t", whichPattern);
    length += stream.printf("Sequence#:%u\n", sequenceNumber);
    length += stream.print(color);
    return length;
  }

  void reset() {
    if (BUG2) {
      Serial.printf("Reset colors of %p\n", this);
    }
    color.all(LedStringer::Off);
  }
#endif
  /// end body
  /////////////////////////////
  uint8_t endMarker;//value ignored

  /** format for delivery, content is copied but not immediately so using stack is risky. */
  Block<uint8_t> incoming() override {
    return Block<uint8_t> {(&endMarker - &startMarker), startMarker};
  }

  Block<const uint8_t> outgoing() const override {
    return Block<const uint8_t> {(&endMarker - &startMarker), startMarker};
  }

};

// command to remote
DesiredState stringState; // zero init: vortex angle 0, all stations black.
// what remote is doing, or locally copied over when command would have been sent.
DesiredState echoState; //last sent?
///////////////////////////////////////////////////////////////////////
LedStringer::Pattern pattern(unsigned si, unsigned style) { //station index
  LedStringer::Pattern p;
  switch (style) {
    case 0:
      p.offset = (si / 2) * VortexFX.perRevolutionActual; //which ring
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
  }
  return p;
}
///////////////////////////////////////////////////////////////////////
// the guy who receives commands as to which lights should be active.
// failed due to errors in FastLED's macroed namespace stuff: using FastLed_ns;
struct Worker : public NowDevice {
  LedStringer leds;
  void setup(bool justTheString = true) {
    leds.setup(VortexFX.total);//leds are dynamically allocated until we know for sure the structure of the vortex.
    if (!justTheString) {
      //if EL's are restored they get setup here.
      NowDevice::setup(stringState); // call after local variables setup to ensure we are immediately ready to receive.
    }
    LedStringer::spew = &Serial;
    Serial.println("Worker Setup Complete");
  }

#if MinesVersion==2

  void loop() {
    if (flagged(dataReceived)) { // message received
      if (TRACE) {
        Serial.printf("Seq#:%u\n", stringState.sequenceNumber);
      }
      leds.setPattern(stringState.color, stringState.pattern);
      if (stringState.showem) {
        leds.show();
      }
    }
  }

#else



  void doStation(unsigned index) {
    if (TRACE) {
      Serial.printf("Station %u: ", index);
    }
    auto p = pattern(index, clistate.patternIndex);
    if (TRACE) {
      p.printTo(Serial);
    }
    leds.setPattern(stringState[index], p);
  }


  void loop() {
    if (flagged(dataReceived)) { // message received
      if (TRACE) {
        Serial.printf("Seq#:%u\n", stringState.sequenceNumber);
      }
      //        ForStations(index) {
      //          doStation(index);
      //        }
      leds.show();
    }
  }
#endif


  // this is called once per millisecond, from the arduino loop().
  // It can skip millisecond values if there are function calls which take longer than a milli.
  void onTick(MilliTick ignored) {
    // not yet animating anything, so nothing to do here.
    // someday: fireworks on 4th ring
  }
} remote;

////////////////////////////////////////////////////////
/// maincontroller.ino:
// needed c++20 using enum LeverSet::Event ;
struct Boss : public NowDevice {
    LeverSet lever;
    Ticker timebomb; // if they haven't solved the puzzle by this amount they have to partially start over.
    Ticker autoReset; //ensure things shut down if the operator gets distracted
    Ticker refreshRate; //sendall occasionally to deal with any intermittency.
    bool needsUpdate[numStations];
    bool leverState[numStations];//paces sending
    //for pacing sending station updates
    unsigned lastStationSent = 0;

  public:
    void setup() {
      lever.setup(50); // todo: proper source for lever debounce time
      timebomb.stop(); // in case we call setup from debug interface
      autoReset.stop();
      refreshRate.next(REFRESH_RATE_MILLIS);
      ForStations(si) {
        needsUpdate[si] = true; //send all on power up
      }
      NowDevice::setup(echoState); // must do this before we do any other esp_now calls.
      auto peerError = NowDevice::addPeer(knownEsp32[0], BUG2);
      reportError(peerError, "Failed to add peer");//FYI only outputs on error
      //todo: periodically retry adding peer.
      Serial.println("Boss Setup Complete");
    }

    void loop() {
      // levers are tested on timer tick, since they are debounced by it.
      if (flagged(dataReceived)) { // message received
        //no-one we know is sending us messages!
      }

      if (refreshColors()) {
        if (BUG3 && !messageOnWire) { //can't send another until prior is handled, this needs work.
#if MinesVersion==2
          ForStations(justcountem) {
            if (++lastStationSent >= numStations) {
              lastStationSent = 0;
            }
            if (needsUpdate[lastStationSent]) {
              stringState.color = leverState[lastStationSent] ? station[lastStationSent] : LedStringer::Off;
              stringState.pattern = pattern(si, cliState.patternIndex);
              ++stringState.sequenceNumber;//mostly to see if connection is working
              sendMessage(stringState);
              break;
            }

          }
#else
          sendMessage(stringState);
#endif
        }
        //else we will eventually get here and think to try again.
      }
    }

    void onSend(bool failed) {
      needsUpdate[lastStationSent] = failed;//if failed still needs to be updated, else it has been updated.
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
      stringState.reset();
      //todo:0 reset levers totally
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
        refreshRate.next(REFRESH_RATE_MILLIS);
        ForStations(si) {
          needsUpdate[si] = true;
        }
      }

      switch (lever.onTick()) {
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
        if (Run.pin) {
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

    }
} primary;
//////////////////////////////////////////////////////////////////////////////////////////////
using ThisApp = NowDevice; //<DesiredState>;
// at the moment we are unidirectional, need to learn more about peers and implement bidirectional pairing by function ID. These are set in the related setup() calls.
ThisApp *ThisApp::receiver = nullptr;
ThisApp *ThisApp::sender = nullptr;

unsigned ThisApp::setupCount = 0;
ThisApp::SendStatistics ThisApp::stats{0, 0, 0};

//////////////////////////////////////////////////////////////////////////////////////////////


#define tweakColor(which) station[clistate.colorIndex].which = param; \
  stringState[clistate.colorIndex] = station[clistate.colorIndex];\
  dbg.cout("color[",clistate.colorIndex,"] = 0x",HEXLY(station[clistate.colorIndex].as_uint32_t()));

bool cliValidStation(unsigned param, const unsigned char key) {
  if (param < numStations) {
    return true;
  }
  dbg.cout("Invalid station : ", param, ", valid values are [0..", numStations, "] for command ", key);
  return false;
}

void clido(const unsigned char key, bool wasUpper) {
  unsigned param = dbg[0]; //clears on read, can only access once!
  switch (key) {
    case '.':
      switch (param) {
        case 0:
          dbg.cout("Refresh colors returned : ", primary.refreshColors());
          break;
        case 1:
          ForStations(si) {
            primary.lever[si] = true;
          }
          primary.onSolution();
          dbg.cout("simulated solution");
          break;
        case ~0u:
          primary.resetPuzzle();
          break;
        case 2:
          primary.lever.restart();
          Serial.printf("After simulated bombing out there are %d levers active\n", primary.lever.numSolved());
          break;
      }
      break;
    case '=':
      stringState.sequenceNumber = param ? param : 1 + stringState.sequenceNumber;
      primary.sendMessage(stringState);
      dbg.cout("Sending sequenceNumber", stringState.sequenceNumber);
      break;

    case 'a':
      if (dbg.numParams() > 1) {
        VortexFX.perRevolutionActual = param;
      }
      if (dbg.numParams() > 1) {
        VortexFX.perRevolutionVisible = dbg[1];
      }
      dbg.cout("Ring config: Visible:", VortexFX.perRevolutionVisible , "\t Actual:", VortexFX.perRevolutionActual);
      break;
    case 'b':
      tweakColor(b);
      break;
    case 'g':
      tweakColor(g);
      break;
    case 'r':
      tweakColor(r);
      break;

    case 'c': // select a color to diddle
      if (cliValidStation(param, key)) {
        clistate.colorIndex = param;
        dbg.cout("Selecting station ", clistate.colorIndex, ", present value is 0x", HEXLY(station[clistate.colorIndex].as_uint32_t()));
      }
      break;

    case 'd'://keystroke echo, lower is on, upper is off
      switch (param) {
        case ~0u: LedStringer::spew = wasUpper ? &Serial : nullptr; break;
        default:
          if (param < sizeof(spam) / sizeof(spam[0])) {
            spam[param] = wasUpper;
          } else {
            Serial.printf("Known debug flags are 0..%u, or ~ for LedStringer\n", numSpams);
          }
          break;
      }
      break;
    case 'l': // select a lever to monitor
      if (cliValidStation(param, key)) {
        clistate.leverIndex = param;
        dbg.cout("Selecting station ", clistate.leverIndex, " lever for diagnostic tracing");
      }
      break;

    case 'm':
      Serial.println(WiFi.macAddress());
      break;
    case 'n':
      NowDevice::debugLevel = param;
      break;
    case 'o':
      switch (param) {
        case 0:
          clistate.onBoard << wasUpper;
          Serial.printf("LED : %x\n", bool(clistate.onBoard));
          break;
        case ~0u:
          clistate.onBoard.toggle();
          Serial.printf("LED : %x\n", bool(clistate.onBoard));
          break;
        default:
          Serial.printf("output %u not yet debuggable\n", param);
          break;
      }
      break;
    case 'p':
      pinMode(param, OUTPUT);
      digitalWrite(param, wasUpper);
      break;
    case 's'://simulate a lever solution
      if (cliValidStation(param, key)) {
        clistate.leverIndex = param;
        primary.lever[clistate.leverIndex] = true;
        Serial.printf("Lever[ %d] latch triggered, reports : % x\n", clistate.leverIndex, primary.lever[clistate.leverIndex]);
        dbg.cout("There are now ", primary.lever.numSolved(), " activated");
      }
      break;
    case 't':
      if (wasUpper) {
        primary.timebomb.next(param);
      }
      Serial.printf("Timebomb due: %u, in : %d, Now : %u\n", primary.timebomb.due, primary.timebomb.remaining(), Ticker::now);
      break;
    case 'u': //unsolve
      if (cliValidStation(param, key)) {
        clistate.leverIndex = param;
        primary.lever[clistate.leverIndex] = false;
        Serial.printf("Lever[%u] latch cleared, reports : %x\n", primary.lever[clistate.leverIndex]);
        dbg.cout("There are now ", primary.lever.numSolved(), " activated");
      }
      break;

    case 'w':
      if (cliValidStation(param, key)) {
        remote.doStation(param);
        remote.leds.show();
      } else {
        dbg.cout("You must precede ", key, " with a number less than ", numStations);
      }
      break;
    case 'z': //set refresh rate
      REFRESH_RATE_MILLIS = param ? param : Ticker::Never;
      break;
    case 26: //ctrl-Z
      Serial.println("Processor restart imminent, pausing long enough for this message to be sent \n");
      for (unsigned countdown = min(param, 4u ); countdown-- > 0;) {
        delay(666);
        Serial.printf(" %u, \t", countdown);
      }
      ESP.restart();
      break;
    case ' ':
      primary.lever.printTo(dbg.cout.raw);
      Serial.print("Desired state : \t");
      //did nothing:      Serial.print(stringState);
      stringState.printTo(dbg.cout.raw);
      Serial.print("Apparent state : \t");
      echoState.printTo(Serial);
      break;
    case '*':
      primary.lever.listPins(Serial);
      Serial.print("Relay assignments : ");
      for (unsigned channel = numRelays; channel-- > 0;) {
        Serial.printf("\t % u : D % u", channel, relay[channel].number);
      }
      Serial.println();
      //add all other pins in use ...
      break;
    case '?':
      Serial.printf("usage : \n\tc: \tselect color / station to tweak color\n\tr, g, b: \talter pigment, %u(0x%2X) is bright\n\tl, s, u: \tlever trace / set / unset\n ", station.MAX_BRIGHTNESS , station.MAX_BRIGHTNESS );
      Serial.printf("\t[gpio number]p: set given gpio number to an output and set it to 0 for lower case, 1 for upper case. VERY RISKY!");
      Serial.printf("\t[millis]Z sets refresh rate in milliseconds, 0 or ~ get you 'Never'\n");
      Serial.printf("\t^Z restarts the program, param is seconds of delay, 4 secs minimum \n");
      Serial.printf("Undocumented : w!.o[Enter]dt\n");
      break;
    case '!':
      flasher.setup();
      break;
    default:
      if (key >= ' ') {
        Serial.printf("Unassigned command letter %c [%u]\n", key, key);
      } else {
        Serial.printf("Most control characters are ignored: ^%c [%u]\n", key + ' ', key);
      }
      break;
  }
}

// arduino's setup:
void setup() {
  Serial.begin(115200);

  dbg.cout.stifled = false;
  TRACE = false;
  BUG3 = false;
  BUG2 = false;
  EVENT = true;
  URGENT = true;

  //  flasher.setup();
  //  dbg.cout("OTA emabled for download but not yet for monitoring." );
  if (IamBoss) {
    Serial.println("Setting up as boss");
    primary.setup();
    remote.setup(true);//true: just LED string, for easy testing
  } else {
    Serial.println("Setting up as remote");
    remote.setup(false);
  }

  Serial.println("Entering forever loop.");

}

// arduino's loop:
void loop() {
  //  flasher.loop();//OTA firmware or file update
  dbg(clido);//process incoming keystrokes
  // time paced logic
  if (Ticker::check()) { // read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    if (IamBoss) {
      primary.onTick(Ticker::now);
    } else {
      remote.onTick(Ticker::now);
    }
    clistate.onTick();
  }
  // check event flags
  if (IamBoss) {
    primary.loop();
  } else {
    remote.loop();
  }
}
