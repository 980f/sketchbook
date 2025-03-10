/*
  punch list:
  vortex off: wait for run/reset change of state?
  mac diagnostic printout byte order
  configurable values to and from EEPROM, or store a string to run through the clirp.
  phase per ring of leds, an array of offsets
  sets of Patterns and use pattern for lever feedback, and test selector for pattern in clirp.
  done: and it was! [check whether a "wait for init" is actually needed in esp_now init]

*/
#include <WiFi.h>
#include <esp_now.h>
//esp stuff pollutes the global namespace with macros for some asm language operations.
#undef cli

//replace with cheaptricks now that are using 980f's stuff to get a cli. #include "simpleUtil.h"

const unsigned numStations = 6;
#define ForStations(si) for(unsigned si=0; si<numStations; ++si)

// time from first pull to restart
unsigned fuseSeconds = 3 * 60; // 3 minutes

// pin assignments being globalized is convenient administratively, while mediocre form programming-wise.

// worker/remote pin allocations:
const unsigned LED_PIN = 13; // drives the chain of programmable LEDs
#define LEDStringType WS2811, LED_PIN, GRB

struct StripConfiguration {
  unsigned perRevolution = 89;
  unsigned perStation = perRevolution / 2;
  unsigned numStrips = 4;
  unsigned total = perRevolution * numStrips; // 356
} VortexFX; // not const until wiring is confirmed, so that we can play with config to confirm wiring.

#include "ledString.h" //FastLED stuff, uses LEDStringType


uint8_t MAX_BRIGHTNESS = 80; // todo: debate whether worker limits received values or trusts the boss.

CRGB stationColor[numStations] = {
  0xFF0000,
  0x00FF00,
  0x0000FF,
  0xFF00FF,
  0xFFFF00,
  0x00FFFF,
};

void printColorSet(const char *prefix, CRGB *color, Print &stream) {
  if (prefix) {
    stream.print(prefix);
    stream.print(": \t");
  }
  ForStations(si) {
    stream.printf("[%u]=%06X \t", color[si].as_uint32_t());
  }
  stream.println();
}

void printColorsOn(Print &stream) {
  printColorSet("Station Colors", stationColor, stream);
}
/////////////////////////
// debug cli
#include "clirp.h" //Command Line Interpreter, Reverse Polish expressions.

//this should be somewhere in the 980f libraries, but couldn't be found:
struct DebugConsole : public CLIRP<unsigned> {
  Stream *cin;
  bool echo = true;
  void setup(Stream &cin) {
    this->cin = &cin;
    Serial.printf("DebugConsole init:%p, Serial is:%p\n", this->cin, &Serial);
  }

  using CommandHandler = std::function<void(unsigned char /*key*/, bool /*wasUpper*/)>;
  void operator()(CommandHandler clido) {
    for (unsigned ki = cin->available(); ki-- > 0;) { // read 'available' just once as a means of not spinning forever when we are getting input faster than we handle it.
      auto key = cin->read();
      //todo: "if echo" : Serial.print(char(key));//echo as indication that we received.
      if (doKey(key)) {
        Char caser(key);
        bool wasUpper = caser.toLower();//#don't inline- no guarantee on whether caser would be passed before of after it was lowered.
        clido(caser, wasUpper);
      }
    }
  }
};

DebugConsole cli;

#include "simplePin.h"
#include "simpleTicker.h"
//debug trace flags
struct CliState {
  unsigned colorIndex = 0; // might use ~0 to diddle "black"
  unsigned leverIndex = ~0;//enables diagnostic spew on selected lever
  bool spewOnTick = false;
  SimpleOutputPin onBoard{2};//Wroom LED
  unsigned spewWrapper = 0;
  Ticker pulser;
  bool onTick() {
    if (pulser.due == Ticker::Never) {//todo: debug 'isRunning'
      onBoard << true;
      pulser.next(250);
      return true;
    } else if (pulser.done()) {
      pulser.next(onBoard.toggle() ? 2500 : 1500);
      if (spewOnTick) {
        Serial.println(onBoard ? "\theart\t" : "\tBEAT\t");
      }
      return true;
    }
    return false;
  }
} clistate;

////////////////////////////////////////////
// Boss/main pin allocations:
#include "simpleDebouncedPin.h"

struct LeverSet {
    struct Lever {
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
    };
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
          Serial.printf("lever %u just became: %x,latched: %x\n", index, lever[index].presently, lever[index].solved);
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
      ForStations(index) {
        lever[index].restart();
      }
    }

    unsigned numSolved() const {
      unsigned sum = 0;
      ForStations(index) {
        sum += lever[index].solved;
      }
      return sum;
    }

    void listPins(Print &stream) const {
      stream.print("Lever logical pin assignments");
      ForStations(index) {
        stream.printf("\t%u:D%u", index, lever[index].pinNumber());
      }
      stream.println();
    }

    void setup(MilliTick bouncer) {
      ForStations(index) {
        lever[index].presently.filter(bouncer);
      }
    }

    LeverSet() : lever{16, 17, 5, 18, 19, 21} {}

    void printOn(Print &stream) {
      stream.printf("Levers: \t");
      ForStations(index) {
        stream.printf("[%u]:%x/%x (%x) \t", index, lever[index].solved, lever[index].presently, lever[index].presently.bouncy);
      }
      stream.write('\n');
    }
};

// boss side:
SimpleOutputPin relay[] = {26, 25, 33, 32, 22, 23}; // all relays in a group to expedite "all off"

enum RelayChannel { // index into relay array.
  //  AUDIO = 0,
  VortexMotor,
  Lights_1,
  Lights_2,
  DoorRelease, // to be replace with text that is in comments at this time
  numRelays    // marker, leave in last position.
};

SimplePin IamBoss = {15}; // pin to determine what this device's role is (Boss or worker) instead of comparing MAC id's and declare the MAC ids of each end.
SimplePin IamReal = {23}; // pin to determine that this is the real system, not the single processor development one.
SimplePin Run     = {4};  //pin to enable else reset the puzzle.

#include "macAddress.h"
// known units, until we implement a broadcast based protocol.
std::array knownDevices = { // todo: figure out why template deduction did not do our counting for us.
  MacAddress{0xD0, 0xEF, 0x76, 0x5C, 0x7A, 0x10},
  MacAddress{0xD0, 0xEF, 0x76, 0x58, 0xDB, 0x98}
};


//todo: compiler knows quite well that there is a class MacAddress, but the next function fails to compile with "unknown class MacAddress" unless I relocate the implementation to later in the file.
unsigned whichDeviceIs(const MacAddress &perhapsMe);

#include "nowDevice.h"
/////////////////////////////////////////
// Structure to convey data
struct DesiredState : public NowDevice::Message {
  /** this is added to the offset of each light */
  unsigned vortexAngle; // 0 to 89 for 0 to 359 degrees of rotation.
  CRGB color[numStations];

  CRGB &operator [](unsigned si) {
    return color[si];
  }

  unsigned size() const override {
    return sizeof(*this);
  }

  // default binary copies in and out work for us
  ///////////////////////////////////////////////////
  void printOn(Print &stream) {
    stream.printf("Angle:%d\n", vortexAngle);
    printColorSet("Active Colors", color, stream);
  }

};

// command to remote
DesiredState stringState; // zero init: vortex angle 0, all stations black.
// what remote is doing,or locally copied over when command would have been sent.
DesiredState echoState;

///////////////////////////////////////////////////////////////////////
// the guy who receives commands as to which lights should be active.
// failed due to errors in FastLED's macroed namespace stuff: using FastLed_ns;
class Worker : public NowDevice {
    LedStringer leds;

  public:
    void setup() {
      leds.setup(VortexFX.total);

      NowDevice::setup(stringState); // call after local variables setup to ensure we are immediately ready to receive.
    }

    void loop() {
      if (flagged(dataReceived)) { // message received
        ForStations(index) {
          for (unsigned grouplength = 44 + (index & 1); grouplength-- > 0;) {
            unsigned offset = (grouplength + stringState.vortexAngle) % VortexFX.perRevolution; // rotate around the strip by group amount
            leds[offset + (index * VortexFX.perRevolution) / 2] = stringState.color[index]; // half of a strip per station
          }
        }
        leds.show();
      }
    }

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
    bool haveRemote = true; // if no remote then is controlling LED string instead of talking to another ESP32 which is actually doing that.
    LeverSet lever;
    Ticker timebomb; // if they haven't solved the puzzle by this amount they have to partially start over.


  public:
    void setup() {
      lever.setup(50); // todo: proper source for debounce time
      NowDevice::setup(echoState); // must do this before we do any other esp_now calls.
      // BTW:ownAddress is all zeroes until after NowDevice::setup.
      haveRemote = IamReal; // read a pin
      if (haveRemote) {     // if not dual role then will actually talk to peer
        esp_now_peer_info_t peerInfo;
        // Register peer
        unsigned myOrdinal = whichDeviceIs(ownAddress);
        // todo: generically there could be more than one peer, here we trust that there is just one:
        knownDevices[myOrdinal ^ 1] >> peerInfo.peer_addr;
        peerInfo.channel = 0; // defering to some inscrutable default selection. Should probably canonize a "show channel" and a different one for luma and hollis.
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
          Serial.println("Failed to add peer");
          // no return here as we need to finish local init even if remote connection fails.
        }
      }
      Serial.println("Setup Complete");
    }

    void loop() {
      // levers are tested on timer tick, since they are debounced by it.
      // someday we will get echoState here and test it against desired and resend on mismatch.
    }

    bool refreshColors() {
      unsigned diffs = 0;
      ForStations(si) {
        stringState[si] = lever[si] ? stationColor[si] : 0;//todo: what is the name for the shared 'black' ledColor?
        if (stringState[si] != echoState[si]) { //until we implement worker status reporting we rely upon the base class faking the echo. see autoEcho.
          ++diffs;
        }
      }
      return diffs > 0;
    }

    void onSolution() {
      timebomb.next(Ticker::Never);
      relay[DoorRelease] << true;
      relay[VortexMotor] << true;
      // any other bells and whistles?
      //todo: timer for vortex auto off? perhaps one full minute just in case operator gets distracted?
    }

    void onTick(MilliTick now) {
      if (timebomb.done()) {
        Serial.println("Timed out solving puzzle, resetting lever state");
        lever.restart();
        return;
      }

      switch (lever.onTick()) {
        case LeverSet::Event::NonePulled: // none on
          break;
        case LeverSet::Event::FirstPulled: // some pulled when none were pulled
          timebomb.next(fuseSeconds * 1000);
          break;
        case LeverSet::Event::SomePulled: // nothing special, but not all off
          break;
        case LeverSet::Event::LastPulled:
          onSolution();
          break;
      }
      //now setup desiredState and if not the same as echoState then send it
      if (refreshColors()) {
        if (!messageOnWire) { //can't send another until prior is handled, this needs work.
          sendMessage(stringState);
        }
        //else we will eventually get here and think to try again.
      }
    }
} primary;
//////////////////////////////////////////////////////////////////////////////////////////////
using ThisApp = NowDevice; //<DesiredState>;
// at the moment we are unidirectional, need to learn more about peers and implement bidirectional pairing by function ID. These are set in the related setup() calls.
ThisApp *ThisApp::receiver = nullptr;
ThisApp *ThisApp ::sender = nullptr;

unsigned ThisApp::setupCount = 0;
ThisApp::SendStatistics ThisApp::stats{0, 0, 0};


//////////////////////////////////////////////////////////////////////////////////////////////
// arduino's setup:
void setup() {
  Serial.begin(115200);
  if (IamBoss) {
    Serial.println("Setting up as boss");
    primary.setup();
  }
  Serial.println("Setting up remote");
  remote.setup();
  Serial.println("All setup and raring to go");
  cli.setup(Serial);
}


#define tweakColor(which) stationColor[clistate.colorIndex ].which = cli.arg; \
  stringState[clistate.colorIndex ] = stationColor[clistate.colorIndex];\
  Serial.printf("color[%u] = 0x%06X\n",clistate.colorIndex,stationColor[clistate.colorIndex]);

bool cliValidStation(unsigned arg, const unsigned char key) {
  if (arg < numStations) {
    return true;
  }
  Serial.printf("Invalid station: %u, valid values are [0..%u] for command %c\n", arg, numStations, key);
  return false;
}

void clido(const unsigned char key, bool wasUpper) {
  unsigned param = cli.arg; //clears on read, can only access once!
  switch (key) {
    case '.':
      Serial.printf("Refresh colors returned:%x\n", primary.refreshColors());
      break;
    case '!':
      primary.onSolution();
      break;
    case 'c': // select a color to diddle
      if (cliValidStation(param, key)) {
        clistate.colorIndex = param;
        Serial.printf("Selecting station %u, present value is 0x%06X\n", clistate.colorIndex, stationColor[clistate.colorIndex]);
      }
      break;
    case 'l': // select a lever to monitor
      if (cliValidStation(param, key)) {
        clistate.leverIndex = param;
        Serial.printf("Selecting station %u lever for diagnostic tracing\n", clistate.leverIndex);
      }
      break;
    case 'r':
      tweakColor(r);
      break;
    case 'g':
      tweakColor(g);
      break;
    case 'b':
      tweakColor(b);
      break;
    case 's'://simulate a lever solution
      if (cliValidStation(param, key)) {
        clistate.leverIndex = param;
        primary.lever[clistate.leverIndex] = true;
        Serial.printf("Lever[%d] latch triggered, reports: %x\n", primary.lever[clistate.leverIndex]);
      }
      break;
    case 't':
      if (wasUpper) {
        primary.timebomb.next(param);
      }
      Serial.printf("Timebomb due:%u, in: %d, Now:%u\n", primary.timebomb.due, primary.timebomb.remaining(), Ticker::now);
      break;
    case 'u': //unsolve
      if (param == ~0u) {
        primary.lever.restart();
        Serial.printf("After simulated bombing out there are %d levers active\n", primary.lever.numSolved());
      } else if (cliValidStation(param, key)) {
        clistate.leverIndex = param;
        primary.lever[clistate.leverIndex] = false;
        Serial.printf("Lever[%d] latch cleared, reports: %x\n", primary.lever[clistate.leverIndex]);
      }
      break;
    case 'q'://periodic spew, lower is off, upper is on.
      clistate.spewOnTick = wasUpper;
      break;
    case 'e'://keystroke echo, lower is on, upper is off
      cli.echo = !wasUpper;
      break;
    case 26: //ctrl-Z
      Serial.println("Processor restart imminent, pausing long enough for this message to be sent \n");
      for (unsigned countdown = min(param, 4u ); countdown-- > 0;) {
        delay(666);
        Serial.printf("%u,\t", countdown);
      }
      ESP.restart();
      break;
    case 'o':
      switch (param) {
        case 0:
          clistate.onBoard << wasUpper;
          Serial.printf("LED:%x\n", bool(clistate.onBoard));
          break;
        case ~0u:
          clistate.onBoard.toggle();
          Serial.printf("LED:%x\n", bool(clistate.onBoard));
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
    case '\r':
      primary.sendMessage(stringState);
      break;
    case ' ':
      primary.lever.printOn(Serial);
      Serial.print("Desired state:\t");
      stringState.printOn(Serial);
      Serial.print("Apparent state:\t");
      echoState.printOn(Serial);
      break;
    case '*':
      primary.lever.listPins(Serial);
      Serial.print("Relay assignments:");
      for (unsigned channel = numRelays; channel-- > 0;) {
        Serial.printf("\t%u:D%u", channel, relay[channel].number);
      }
      Serial.println();
      //add all other pins in use ...
      break;
    case '?':
      Serial.printf("usage:\n\tc:\tselect color/station to tweak color\n\tr,g,b:\talter pigment,%u(0x%2X) is bright\n\tl,s,u:\tlever trace/set/unset\n ", MAX_BRIGHTNESS , MAX_BRIGHTNESS );
      Serial.printf("Undocumented: !^Z.o[Enter]qet\n");
      break;
    default:
      Serial.printf("Unassigned command letter %c\n", key);
      break;
  }
}

// arduino's loop:
void loop() {
  // debug interface
  cli(clido);//process incoming keystrokes
  // time paced logic
  if (Ticker::check()) { // read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    if (IamBoss) {
      primary.onTick(Ticker::now);
    }
    remote.onTick(Ticker::now);
    clistate.onTick();
  }
  // check event flags
  if (IamBoss) {
    primary.loop();
  }
  remote.loop();
}

unsigned whichDeviceIs(const MacAddress &perhapsMe) {
  for (unsigned index = knownDevices.size(); index-- > 0;) {
    if (knownDevices[index] == perhapsMe) {
      return index;
    }
  }
  return ~0; // which is -1 if looked at as an int.
}
