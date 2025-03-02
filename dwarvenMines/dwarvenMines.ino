
#include <WiFi.h>
#include <esp_now.h>

//pin assignments being globalized is convenient syntactically, versus keeping them local to the using classes, for constant init reasons:
//worker/remote pin allocations:
const unsigned LED_PIN = 13;//drives the chain of programmable LEDs
#define LEDStringType WS2811, LED_PIN, GRB

const unsigned  ELPins[] = {4, 16, 17, 18, 19, 21, 22, 23};
const unsigned NUM_LEDS = 89;

//Boss/main pin allocations:
const unsigned PIN_TRIGGER = 4;     // Pin 4: Trigger input
const unsigned TRIGGER_DEBOUNCE = 50; //formerly buried in code.

const unsigned PIN_AC_RELAY[4] = {26, 25, 33, 32};
//the following is the only conflict between primary and remote:
const unsigned PIN_AUDIO_RELAY = 21; // Pin 22: I2C (temporarily Audio Relay trigger)

//todo: pick a pin to determine what this device's role is (primary or remote) instead of comparing MAC id's and declare the MAC ids of each end.

#include "macAddress.h"

// Primary Receiver's MAC
//uint8_t broadcastAddress[] = {0xD0, 0xEF, 0x76, 0x58, 0xDB, 0x98};

//the following should be a member of the device, not a global.
// Backup Receiver's MAC
MacAddress broadcastAddress {0xD0, 0xEF, 0x76, 0x5C, 0x7A, 0x10};
///////////////////////////////////////////////////////////////////////
// this next group will become a sharable module
#define FASTLED_INTERNAL
#include <FastLED.h>
///////////////////////////////////////////////////////////////////////
template <unsigned NUM_LEDS> struct LedString {
  CRGB leds[NUM_LEDS];
  const CRGB ledOff = {0, 0, 0};

#define forLEDS(indexname) for (int indexname = NUM_LEDS; i-->0;)

  void all(CRGB same) {
    forLEDS(i) {
      leds[i] = same;
    }
  }

  void allOff() {
    all( ledOff);
  }

  using BoolPredicate = std::function<bool(unsigned)>;
  void setJust(CRGB runnerColor, BoolPredicate lit) {
    forLEDS(i) {
      leds[i] = lit(i) ? runnerColor : ledOff;
    }
  }

  void setup() {
    FastLED.addLeds<LEDStringType>(leds, NUM_LEDS);//FastLED tends to configuring the GPIO, most likely as a pwm/timer output.
  }

  void show() {
    FastLED.show();
  }

  CRGB & operator [](unsigned i) {
    i %= NUM_LEDS; //makes it easier to marquee
    return leds[i];
  }

  static CRGB blend(unsigned phase, unsigned cycle, const CRGB target, const CRGB from) {//todo: CRGB class has a blend method we can use here.
    return CRGB (
             map(phase, 0, cycle, target.r, from.r),
             map(phase, 0, cycle, target.g, from.g),
             map(phase, 0, cycle, target.b, from.b)
           );
  }

};
//////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
//copied in from 980f's library
template <typename Scalar, typename ScalarArg> bool changed(Scalar &target, const ScalarArg &source) {
  if (target != source) { //implied conversion from ScalarArg to Scalar must exist or compiler will barf on this line.
    target = source;
    return true;
  }
  return false;
}

bool flagged(bool &flag) {
  auto was = flag;
  flag = false;
  return was;
}

///////////////////////////////////////////////////////////////////////
//minimal version of ticker service, will add full one later:
using MilliTick = decltype(millis());
struct Ticker {
  static MilliTick now; //cached/shared sampling of millis()
  MilliTick due = ~0U; //'forever'

  static bool check() {
    auto sample = millis();
    if (now != sample) {
      now = sample;
      return true;
    }
    return false;
  }

  bool done() {
    bool isDone = due <= now;
    due = ~0U; //run only once per 'next' call.
    //this is inlined version so I am leaving out refinements such as check for a valid  'due' value.
    return isDone;
  }

  /** @returns whether the 'future' expiration time is in the past due to wrapping the ticker counter. The program has to run for 49 days for that to occur.*/
  bool next(MilliTick later) {
    due = later + now;
    return due < now; //timer service wrapped.
  }
};

MilliTick Ticker::now = 0;
///////////////////////////////////////////////////////////////////////
// yet another input debouncer, will use library one after code factoring is complete
struct DebouncedInput {
  unsigned pin;
  //use this to invert logical sense of the input, such as when you add in an inverting amplifier late in development.
  bool activeHigh;
  bool readPin() {
    return digitalRead(pin) == activeHigh;
  }
  ///////// above this is actually a class in its own right.
  //official state
  bool stable;
  bool bouncy;
  Ticker bouncing;
  MilliTick DebounceDelay = ~0u;//never

  /** @returns whether the input has officially changed to a new state */
  bool onTick(MilliTick now) {
    if (changed(bouncy, readPin())) {
      bouncing.next(DebounceDelay);
    }

    if (bouncing.done()) {
      return changed(stable, bouncy);
    }
    return false;
  }

  //use ~pin (not -pin) to indicate low active sense
  void setup(int thePin, MilliTick bounceTime, bool triggerOnStart = false) {
    activeHigh = thePin >= 0;
    pin = activeHigh ? thePin : ~thePin;
    pinMode(pin, INPUT);
    DebounceDelay = bounceTime;

    bouncy = readPin();

    if (triggerOnStart) {
      stable = ~bouncy;
      bouncing.next(DebounceDelay);
    } else {
      stable = bouncy;
    }
  }

  operator bool() const {
    return stable;
  }
};
///////////////////////////////////////////////
//communications manager:
//todo: replace template with helper class or a base class for Messages.
template <class Message> class NowDevice {
  protected:

    /////////////////////////////////
    Message lastMessage;
    bool messageOnWire = false; //true from send attempt if successful until 'OnDataSent' is called.
    bool sendFailed = false; //false when send attempted, set by OnDataSent according to its 'status' param
    bool sendRequested = false;

    int sendRetryCount = 0;//todo: use status exchange to get rid of retry logic.
  public: //wtfversion of C++ is in use by esp32? I could not init these inside the class but could not declare them outside either!
    static NowDevice *sender; //only one sender is allowed at this protocol level.
  protected:
    static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
      //todo: check that  mac_addr makes sense.
      Serial.print("\r\nLast Packet Send Status:\tDelivery ");
      bool failed = status != ESP_NOW_SEND_SUCCESS;

      Serial.println(failed ? "Failed" : "Succeeded");
      if (sender) {
        sender->messageOnWire = false;
        sender->sendFailed = failed;
        //if there is a requested one then send that now.
      }
    }

    void sendMessage(const Message &newMessage) {
      // Set values to send
      lastMessage = newMessage;
      sendRequested = true;
      // Send message via ESP-NOW
      esp_err_t result = esp_now_send(broadcastAddress, reinterpret_cast < uint8_t *>(&lastMessage), sizeof(Message));
      messageOnWire = result == OK;
      sendFailed = !messageOnWire;
    }

    /////////////////////////////////
    Message message;//incoming
    bool dataReceived = false;

    // callback function that will be executed when data is received
    void onMessage(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
      memcpy(&message, incomingData, sizeof(Message)); //todo: check len against sizeof(Message), they really must be equal or we have a serious failure to communicate.
      Serial.print("Bytes received: ");
      Serial.println(len);
      message.printOn(Serial);
      dataReceived = true;
    }

  public:
    static NowDevice<Message> *receiver; //only one receiver is allowed at this protocol level.
  protected:
    static void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {//esp32 is not c++ friendly in its callbacks.
      if (receiver) {
        receiver->onMessage(esp_now_info, incomingData, len);
      }
    }

    /////////////////////////////////
  public:
    MacAddress ownAddress{0, 0, 0, 0, 0, 0}; //will be all zeroes at startup

    virtual void setup() {

      // Set device as a Wi-Fi Station
      WiFi.mode(WIFI_STA);
      WiFi.macAddress(ownAddress);
      Serial.print("I am: ");
      ownAddress.PrintOn(Serial);
      //todo: other examples spin here waiting for WiFi to be ready.
      // Init ESP-NOW
      if (esp_now_init() == ESP_OK) {
        receiver = this;
        esp_now_register_recv_cb(&OnDataRecv);
        sender = this;
        esp_now_register_send_cb(&OnDataSent);
      } else {
        Serial.println("Error initializing ESP-NOW");
      }
    }

    virtual void loop() {
      //empty loop rather than =0 in case extension doesn't need a loop
    }

    virtual void onTick(MilliTick now) {
      //empty loop rather than =0 in case extension doesn't need timer ticks
    }

    //for when one chip does both roles: (and for standalone testing where we add in the trigger code to this guy)
    void fakeReception(const Message &faker) {
      message = faker;
      dataReceived = true;
    }

};

///////////////////////////////////////////////////////////////////////
//other common logic of both ends
struct Sequencer {

  int frame = 0;
  int currentSequence = -1;
  int sequenceToPlay = -1;
  int frameDelay = 20;
  Ticker frameTimer;

  bool atTime(int minutes, int seconds, int milliseconds) {
    seconds += minutes * 60;
    seconds *= 1000;
    seconds += milliseconds;

    MilliTick currentTimeMin = frameDelay * frame; //time since start of sequence
    MilliTick currentTimeMax = currentTimeMin + frameDelay;

    return seconds > currentTimeMin && seconds <= currentTimeMax;
  }


  unsigned framesIn(unsigned  minutes, unsigned seconds, unsigned milliseconds) {
    MilliTick interval = milliseconds;
    interval += seconds * 1000;
    interval += minutes * 60000;
    //here is where we decide on ceil() floor() or round() via adding frameDelay-1, nothing, or frameDelay/2 respectively
    return interval / frameDelay;
  }

};

/////////////////////////////////////////
// Structure to convey data
struct DesiredSequence {
  unsigned action;
  int number;
  void printOn(Print &stream) {
    stream.print("Action: ");
    stream.println(action);
    stream.print("Number: ");
    stream.println(number);
    stream.println();
  }
};

//there is only one message sent!
const DesiredSequence StartSequence {
  0, 0
};


///////////////////////////////////////////////////////////////////////
// the guy who receives commands as to which lighting sequence should be active.
//we should contain rather than inherit the Sequencer, but we are mutating C code and will do that later
class Worker: public NowDevice<DesiredSequence>, Sequencer {

    LedString<NUM_LEDS>leds;
    const unsigned NUM_SHOW_LIGHTS   = 33;
    const unsigned RUN_LIGHT_SPACING = 15;
    const unsigned MAX_BRIGHTNESS = 80;

    const CRGB ledColor = {0, 247, 255}; //todo: figure out why the argument based constructors don't work here while assignement of implied constructor does.
    const CRGB showLightColor = {240, 222, 163};
    const CRGB exitLightColor = {240, 0, 0};

    //grouped for easier excision.
    class ELgroup {
        //the ELPins are defined external to this class for development ease. It saves on adding constructor stuff and allows us to const things which is efficient.
        const int numPins = sizeof(ELPins) / sizeof(decltype(ELPins));
      public:

        void allOff() {
          for (int i = 0; i < numPins; i++) {
            digitalWrite(ELPins[i], LOW);
          }
        }

        void toggleRandom(unsigned numberToToggle) {
          //todo:00 this is hard on relays, should compute a set of booleans and then apply them!
          allOff();
          for (int i = 0; i < numberToToggle; i++) {
            digitalWrite(ELPins[random8(0, 7)], HIGH);
          }
        }

        void setup() {
          for (int i = 0; i < numPins; i++) {
            pinMode(ELPins[i], OUTPUT);
          }
          allOff();
        }

    } EL;


    int speed = 0;
    int ringLightPosition = 0;
    int ringAcceleration = 1;

    const unsigned showLightIndex[3] = {0, 30, 60};//these are inlined elsewhere

    bool isShowLight(const unsigned index) {
      for (unsigned which = sizeof(showLightIndex) ; which-- > 0;) {
        if (index == showLightIndex[which]) {
          return true;
        }
      }
      return false;
    }

    void setJustRunners(CRGB runnerColor) {
      auto &phasor(ringLightPosition);
      auto &spacer(RUN_LIGHT_SPACING);
      leds.setJust(runnerColor, [phasor, spacer](unsigned i) -> bool {
        return (i - phasor) % spacer == 0;
      });
    }

  public:
    void setup() {
      leds.setup();
      EL.setup();
      NowDevice::setup();//call after local variables setup to ensure we are immediately ready to receive.
    }

    void loop() {
      if (flagged(dataReceived)) {
        sequenceToPlay = message.number;
        startSequence(sequenceToPlay); //the code formerly here was duplicated inside startSequeuce which was ...called here.
      }
    }

    //this is called once per millisecond, from the arduino loop(). It can skip millisecond values if there are function calls which take longer than a milli.
    void onTick(MilliTick ignored) {
      if (frameTimer.done()) {
        ++frame;
        switch (currentSequence) {
          case 0:
            sequence0();
            break;

          case 1:
            sequence1();
            break;

          case 2:
            sequence2();
            break;

          case 3:
            sequence3();
            break;

          case 4:
            sequence4();
            break;
        }
        leds.show();
        frameTimer.next(frameDelay);//defer this to after the sequencex() calls as they are allowed to modify frameDelay, although most don't.
      }
    }

  private:
    void startSequence(int sequenceToPlay) {
#if originalWay
      if ( !changed(currentSequence, sequenceToPlay) ) {
        // If another request for the same sequence is received, then consider it a stop request
        //980f: this really should be 'send a different command', so that we aren't hoisted by repeating a failed communication which failed on the acknowledge rather than the send. "idempotent" is desirable.
        currentSequence = -1;//todo: remove this, just return presuming a false resending of command.
      }

      EL.allOff();
      frame = 0;
#else
      if ( changed(currentSequence, sequenceToPlay) ) {
        EL.allOff();
        frame = 0;
      }
#endif

    }


    // Nothing is happening
    void sequence0() {
      speed = 1;
      ringLightPosition = 0;

      leds.allOff();
      //todo: replace with a Ticker set for 500:
      if (atTime(0, 0, 500)) {
        startSequence(1);
      }
    }

    // Initialize show lights
    void sequence1() {
      if (atTime(0, 0, 500)) {
        leds[showLightIndex[0]] = showLightColor;
      }
      if (atTime(0, 1, 500)) {
        leds[showLightIndex[1]] = showLightColor;
      }
      if (atTime(0, 2, 500)) {
        leds[showLightIndex[2]] = showLightColor;
      }
      if (atTime(0, 4, 0)) {
        startSequence(2);
      }
    }

    // Transition to running lights
    void sequence2() {
      const unsigned numberOfSteps = 80;

      if (frame < numberOfSteps) {
        CRGB showy = leds.blend(frame, numberOfSteps, showLightColor, ledColor);
        CRGB plain = leds.blend(frame, numberOfSteps, leds.ledOff, ledColor);

        forLEDS(i) {
          if (i % RUN_LIGHT_SPACING == 0) {
            // Fade in running lights
            if (isShowLight(i)) {
              // Fade from show lights
              leds[i] = showy;
            } else {
              // Fade from black
              leds[i] = plain;
            }
          }
        }
      }

      if (frame == 2 * numberOfSteps + 40) {
        startSequence(3);
      }

    }

    // Start spinning
    void sequence3() {

      if (speed < 6) {
        // When speed is less than 6 then simply increase the rate of position changes until it reaches the framerate
        if (frame % (6 - speed) == 0) {
          ringLightPosition += 1;
        }
      } else {
        // Once speed is past the framerate then start skipping LEDs to increase speed
        if (speed <= 8) {
          ringLightPosition += speed - 4;
        } else {
          // Don't go faster than speed 8
          ringLightPosition += 2;
        }
      }

      if (ringLightPosition >= NUM_LEDS) {
        ringLightPosition = 0;
      }

      if (frame < 1150) {
        if (frame % 80 == 0) {
          // Accelerate
          speed += ringAcceleration;
        }
      } else {
        if (frame % 30 == 0) {
          // Deaccelerate
          if (--speed < 0) {//decr with saturate is available in a lib
            speed = 0;
          }
        }
      }

      if (speed != 0) {
        setJustRunners(ledColor);
      } else {
        // leds.allOff()
      }

      if (speed >= 9 && speed < 25 && frame % (30 - speed) == 0) {
        if (speed < 11) {
          EL.toggleRandom(1);
        } else if (speed < 14) {
          EL.toggleRandom(2);
        } else if (speed < 17) {
          EL.toggleRandom(3);
        } else {
          EL.toggleRandom(5);
        }
      }

      if (frame > 1000 && speed == 0) {
        // End of sequence
        // leds.allOff()
        startSequence(4);
      }

    }

    // Cool Down
    void sequence4() {
      //spews:      Serial.println(frame);
      setJustRunners(exitLightColor);
      if (frame > 500) {
        // End of sequence
        leds.allOff();
        startSequence(-1);
      }
    }
} remote;

////////////////////////////////////////////////////////
/// maincontroller.ino:
class Boss: public NowDevice<DesiredSequence>, Sequencer {
    bool haveRemote = false; //if no remote  then is controlling LED string instead of talking to another ESP32 which is actually doing that.
    DebouncedInput trigger;

    bool sequenceRunning = false;

    void startSequence() {
      Serial.println("Start Sequence");
      frame = 0;
      sequenceRunning = true;
    }

    void runSequence() {

      if (atTime(0, 0, 0)) {
        // Start Audio
        digitalWrite(PIN_AUDIO_RELAY, HIGH);
      }

      if (atTime(0, 0, 100)) {
        digitalWrite(PIN_AUDIO_RELAY, LOW);
      }

      if (atTime(0, 5, 0)) {
        // Send message to start ring light sequence (it has internal 3 second delay at beginning)
        if (haveRemote) {
          sendMessage(StartSequence);
        } else {
          remote.fakeReception(StartSequence);//we could poke into its message and set 'received'
        }
      }

      if (atTime(0, 5, 500)) {
        // Lights 1
        digitalWrite(PIN_AC_RELAY[2 - 1], HIGH);
      }

      if (atTime(0, 6, 700)) {
        // Lights 2
        digitalWrite(PIN_AC_RELAY[3 - 1], HIGH);
      }

      if (atTime(0, 7, 800)) {
        // Lights 3
        digitalWrite(PIN_AC_RELAY[4 - 1], HIGH);
      }

      if (atTime(0, 18, 500)) {
        // Vortex Motor Start
        digitalWrite(PIN_AC_RELAY[1 - 1], HIGH);
      }

      if (atTime(0, 47, 0)) {
        // Vortex Motor Stop
        digitalWrite(PIN_AC_RELAY[1 - 1], LOW);
      }

      if (atTime(0, 50, 0)) {
        // Done
        stopSequence();
      }

    }

    void stopSequence() {
      Serial.println("Stop Sequence");
      for (unsigned pin : PIN_AC_RELAY) {
        digitalWrite(pin, LOW);
      }
      digitalWrite(PIN_AUDIO_RELAY, LOW);//added to ensure complete stopping of everything.
      frame = 0;
      sequenceRunning = false;
      //todo: (maybe) signal remote that it also should be finished.
    }
  public:
    void setup() {
      frameDelay = 0; //feature not used, being explicit here cause it took me awhile to figure out how the atTime() worked without a controlled value for frameDelay.
      trigger.setup(PIN_TRIGGER, TRIGGER_DEBOUNCE);
      for (unsigned pin : PIN_AC_RELAY) {
        pinMode(pin, OUTPUT);
      }
      pinMode(PIN_AUDIO_RELAY, OUTPUT);

      NowDevice::setup();//must do this before we do any other esp_now calls.

      haveRemote  = ownAddress != broadcastAddress;
      if (haveRemote ) {//if not dual role then will actually talk to peer
        esp_now_peer_info_t peerInfo;
        // Register peer
        broadcastAddress >> peerInfo.peer_addr;

        peerInfo.channel = 0;
        peerInfo.encrypt = false;

        // Add peer
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
          Serial.println("Failed to add peer");
          //need to finish local init even if remote connection fails.
        }
      }

      // Turn off lights which are on by default
      //digitalWrite(PIN_AC_RELAY_1, HIGH);
      //digitalWrite(PIN_AC_RELAY_2, HIGH);

      Serial.println("Setup Complete");
    }

    void loop() {
      //todo: replace retry with the other end sending its sequence state to this end. If they don't compare then send again.
      // Retry any failed messages
      if (haveRemote ) {//don't strictly need to check as we will never have a sendFailed if we never send ;)
        if (sendFailed) {
          if (sendRetryCount < 5) {
            sendRetryCount++;
            Serial.println("Retrying");
            sendMessage(lastMessage);
          } else {
            sendRetryCount = 0;
            sendFailed = false;
            Serial.println("Giving Up");
          }
        }
      }
    }

    void onTick(MilliTick now) {
      if (trigger.onTick(now)) {
        if (trigger) {// a toggle, although a run state is nicer.
          if (sequenceRunning) {
            stopSequence();
          } else {
            startSequence();
          }
        }
      }
      if (sequenceRunning) {
        runSequence();
        //the value formerly changed here was ignored, the whole framing thing seems abandoned on this end of the link.
      }
    }

} primary;

//at the moment we are unidirectional, need to learn more about peers and implement bidirectional pairing by function ID.
//these are set in the related setup() calls.
template<> NowDevice<DesiredSequence> *NowDevice<DesiredSequence> ::receiver = nullptr;
template<> NowDevice<DesiredSequence> *NowDevice<DesiredSequence> ::sender = nullptr;


//////////////////////////////////////////////////////////////////////////////////////////////
//arduino's setup:
void setup() {
  Serial.begin(115200);
  remote.setup();
  primary.setup();
}

//arduino's loop:
void loop() {
  if (Ticker::check()) { //read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    remote.onTick(Ticker::now);
    primary.onTick(Ticker::now);
  }
  remote.loop();
  primary.loop();
}
