/*
  punch list:
  vortex off: wait for run/reset change of state?
  mac diagnostic printout byte order
  check whether a "wait for init" is actually needed in esp_now init

*/
#include <WiFi.h>
#include <esp_now.h>

#include "simpleUtil.h"

const unsigned numStations = 6;
//time from first pull to restart
unsigned fuseSeconds = 3 * 60; //3 minutes

//pin assignments being globalized is convenient administratively, while mediocre form programming-wise.

//worker/remote pin allocations:
const unsigned LED_PIN = 13;//drives the chain of programmable LEDs
#define LEDStringType WS2811, LED_PIN, GRB

struct StripConfiguration {
  unsigned perRevolution = 89;
  unsigned perStation = perRevolution / 2;
  unsigned numStrips = 4;
  unsigned total = perRevolution * numStrips; //356
} VortexFX;//not const until wiring is confirmed, so that we can play with config to confirm wiring.

#include "ledString.h"  //FastLED stuff, uses NUM_LEDS and LEDStringType

////////////////////////////////////////////
//Boss/main pin allocations:
#include "simpleDebouncedPin.h"

const unsigned leverpin[] = {4, 16, 17, 18, 19, 21}; //formerly trigger in and audio out on boss, EL pins on worker
class LeverSet {
    struct Lever {
      //latched version of "presently"
      bool solved;
      //debounced input
      DebouncedInput presently;

      //check up on bouncing.
      void onTick(MilliTick ignored = 0) { //implements latched edge detection
        if (presently.onTick(ignored)) { //if just became stable
          solved |= presently;
        }
      }

      //restart puzzle. If a lever sticks we MUST fix it. We cannot fake one.
      void restart() {
        solved = presently;
      }

      void setup(const SimplePin&simplepin, MilliTick bouncer) {
        //how do we get pin to a debounced input post construction?
        //write a new method:
        presently.attach(simplepin, bouncer);
      }

      Lever(unsigned pinNumber): presently{pinNumber} {}

    };
    std::array <Lever, numStations> lever; //using std::array over traditional array to get initializer syntax that we can type

  public:

    enum class Event {
      NonePulled,  //none on
      FirstPulled, //some pulled when none were pulled
      SomePulled,  //nothing special, but not all off
      LastPulled,  //all on
    };

    Event onTick() {
      //update, and note major events
      unsigned prior = numSolved();

      unsigned someNow = numSolved();
      if (someNow == prior) {//no substantial change
        return someNow ? Event::SomePulled : Event::NonePulled;
      }
      //something significant changed
      if (someNow == numStations) {
        return Event::LastPulled;//takes priority over FirstPulled when simultaneous
      }
      if (prior == 0) {
        return Event::FirstPulled;
      }
      return Event::SomePulled;// a different number but nothing special.
    }

    bool operator[](unsigned index) {
      return lever[index].solved;
    }

    void restart() {
      for (unsigned index = numStations; index -- > 0;) {
        lever[index].restart();
      }
    }

    unsigned numSolved() const {
      unsigned sum = 0;
      for (unsigned index = numStations; index -- > 0;) {
        sum += lever[index].solved;
      }
      return sum;
    }

    void setup(MilliTick bouncer) {
      for (unsigned index = numStations; index -- > 0;) {
        lever[index].setup(leverpin[index], bouncer);
      }
    }

    LeverSet(): lever {4, 16, 17, 18, 19, 21} {}
};

//boss side:
const SimpleOutputPin relay[] = {21, 26, 25, 33, 32};//all relays in a group to expedite "all off"

enum RelayChannel { //index into relay array.
  AUDIO = 0,
  VortexMotor, Lights_1, Lights_2, DoorRelease, //to be replace with text that is in comments at this time
  LAST
};



const SimplePin IamBoss = {15}; //pin to determine what this device's role is (Boss or worker) instead of comparing MAC id's and declare the MAC ids of each end.
const SimplePin IamReal = {2}; //pin to determine that this is the real system, not the single processor development one.


#include "macAddress.h"
//known units, until we implement a broadcast based protocol.
std::array knownDevices = {//todo: figure out why template deduction did not do our counting for us.
  MacAddress{0xD0, 0xEF, 0x76, 0x5C, 0x7A, 0x10},
  MacAddress{0xD0, 0xEF, 0x76, 0x58, 0xDB, 0x98}
};

unsigned whichDeviceIs(const MacAddress &perhapsMe) {
  for (unsigned index = knownDevices.size(); index-- > 0;) {
    if (knownDevices[index] == perhapsMe) {
      return index;
    }
  }
  return ~0;//which is -1 if looked at as an int.
}

#include "nowDevice.h"
/////////////////////////////////////////
// Structure to convey data
struct DesiredState: public NowDevice::Message {
  /** this is added to the offset of each light */
  unsigned vortexAngle; //0 to 89 for 0 to 359 degrees of rotation.
  CRGB color[numStations];

  unsigned size() const override {
    return sizeof(*this);
  }

  //default binary copies in and out work for us
  ///////////////////////////////////////////////////
  void printOn(Print &stream) {
    stream.printf("Angle:%d\n", vortexAngle);
    stream.print("station:lighted\t");
    for (unsigned index = 0; index < numStations; ++index) {
      stream.printf("%u:%06x\t", index, color[index].as_uint32_t());
    }
    stream.println();
  }
};

uint8_t MAX_BRIGHTNESS = 80;//todo: debate whether worker limits received values or trusts the boss.

constexpr uint8_t bitColor(unsigned bitpack, unsigned bitpick) {
  return bitpack & (1 << bitpick) ? MAX_BRIGHTNESS : 0;
}

/** the six colors, an init for them */
constexpr CRGB color(unsigned index) {
  ++index;//skipping black as not usefu!
  return {bitColor(index, 0), bitColor(index, 1), bitColor(index, 2)};
}

//command to remote
DesiredState stringState;//zero init: vortex angle 0, all stations black.
//what remote is doing
DesiredState echoState;

///////////////////////////////////////////////////////////////////////
// the guy who receives commands as to which lights should be active.
//failed due to errors in FastLED's macroed namespace stuff: using FastLed_ns;
class Worker: public NowDevice {
    LedStringer leds;

  public:
    void setup() {
      leds.setup(VortexFX.total);
      //don't trust zero init as we may implement a remote restart command to aid in debug.
      stringState.vortexAngle = 0;
      for (unsigned index = numStations; index-- > 0;) {
        stringState.color[index] = color(index); //unique set so that we can start debug.
      }
      NowDevice::setup(stringState);//call after local variables setup to ensure we are immediately ready to receive.
    }

    void loop() {
      if (flagged(dataReceived)) {//message received
        for (unsigned index = numStations ; index-- > 0;) {
          for (unsigned grouplength = 44 + (index & 1); grouplength-- > 0;) {
            unsigned offset = (grouplength + stringState.vortexAngle ) % VortexFX.perRevolution;//rotate around the strip by group amount
            leds[offset + (index * VortexFX.perRevolution) / 2] = stringState.color[index]; //half of a strip per station
          }
        }
        leds.show();
      }
    }

    //this is called once per millisecond, from the arduino loop(). It can skip millisecond values if there are function calls which take longer than a milli.
    void onTick(MilliTick ignored) {
      //not yet animating anything, so nothing to do here.
      //someday: fireworks on 4th ring
    }
} remote;

////////////////////////////////////////////////////////
/// maincontroller.ino:
//needed c++20 using enum LeverSet::Event ;
class Boss: public NowDevice {
    bool haveRemote = true; //if no remote then is controlling LED string instead of talking to another ESP32 which is actually doing that.
    LeverSet lever;
    Ticker timebomb;//if they haven't solved the puzzle by this amount they have to partially start over.

  public:
    void setup() {
      lever.setup(50);//todo: proper source for debounce time
      NowDevice::setup(echoState);//must do this before we do any other esp_now calls.
      //BTW:ownAddress is all zeroes until after NowDevice::setup.
      haveRemote  = IamReal;//read a pin
      if (haveRemote) {//if not dual role then will actually talk to peer
        esp_now_peer_info_t peerInfo;
        // Register peer
        unsigned myOrdinal = whichDeviceIs(ownAddress);
        //todo: generically there could be more than one peer, here we trust that there is just one:
        knownDevices[myOrdinal ^ 1] >> peerInfo.peer_addr;
        peerInfo.channel = 0;//defering to some unscruptable default selection. Should probably canonize a "show channel" and a different one for luma and hollis.
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
          Serial.println("Failed to add peer");
          //no return here as we need to finish local init even if remote connection fails.
        }
      }
      Serial.println("Setup Complete");
    }

    void loop() {
      //levers are tested on timer tick, since they are debounced by it.
      //someday we will get echoState here and test it against desired and resend on mismatch.
    }

    void onTick(MilliTick now) {
      if (timebomb.done()) {
        lever.restart();
        return;
      }

      switch (lever.onTick()) {
        case LeverSet::Event::NonePulled:  //none on
          break;
        case LeverSet::Event::FirstPulled: //some pulled when none were pulled
          timebomb.next(fuseSeconds * 1000);
          break;
        case LeverSet::Event::SomePulled:  //nothing special, but not all off
          break;
        case LeverSet::Event::LastPulled:
          timebomb.next(Ticker::Never);
          relay[DoorRelease] << true;
          relay[VortexMotor] << true;
          //any other bells and whistles
          //todo: timer for vortex auto off?
          break;
      }
    }
} primary;
//////////////////////////////////////////////////////////////////////////////////////////////
using ThisApp = NowDevice; //<DesiredState>;
//at the moment we are unidirectional, need to learn more about peers and implement bidirectional pairing by function ID.
//these are set in the related setup() calls.
ThisApp *ThisApp::receiver = nullptr;
ThisApp *ThisApp ::sender = nullptr;

unsigned ThisApp::setupCount = 0;
ThisApp::SendStatistics ThisApp::stats {0, 0, 0};

/////////////////////////
// debug cli

#include "clirp.h"


//////////////////////////////////////////////////////////////////////////////////////////////
//arduino's setup:
void setup() {
  Serial.begin(115200);
  if (IamBoss) {
    primary.setup();
  }
  remote.setup();
}

//arduino's loop:
void loop() {
  if (Ticker::check()) { //read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    if(IamBoss) {
      primary.onTick(Ticker::now);
    }
    remote.onTick(Ticker::now);
  }
  if(IamBoss) {
    primary.loop();
  }
  remote.loop();
}
