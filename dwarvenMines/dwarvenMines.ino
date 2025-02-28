
#define FASTLED_INTERNAL
#include <FastLED.h>
//you should never need to include this in an .ino file: #include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

//pin assignments being globalized is convenient syntactically, versus keeping them local to the using classes, for constant init reasons:
const unsigned LED_PIN = 13;//drives the chain of programmable LEDs
const int ELPins[] = {4, 16, 17, 18, 19, 21, 22, 23};

const unsigned PIN_TRIGGER = 4;     // Pin 4: Trigger input
const unsigned PIN_AC_RELAY[4] = {26, 25, 33, 32};
//the following is the only conflict between primary and remote:
const unsigned PIN_AUDIO_RELAY = 21; // Pin 22: I2C (temporarily Audio Relay trigger)

// Primary Receiver's MAC
//uint8_t broadcastAddress[] = {0xD0, 0xEF, 0x76, 0x58, 0xDB, 0x98};

// Backup Receiver's MAC
uint8_t broadcastAddress[] = {0xD0, 0xEF, 0x76, 0x5C, 0x7A, 0x10};


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

  bool next(MilliTick later) {
    due = later + now;
    return due < now; //timer service wrapped.
  }
};

MilliTick Ticker::now = 0;
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////
//communications manager:
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
      memcpy(&message, incomingData, sizeof(Message)); //todo: check len agaisnt sizeof(Message), they really must be equal or we have a serious violation of communcations.
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
    void setup() {

      // Set device as a Wi-Fi Station
      WiFi.mode(WIFI_STA);

      Serial.print("I am: ");
      Serial.println(WiFi.macAddress());
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
};

///////////////////////////////////////////////////////////////////////

//common logic of both ends
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


///////////////////////////////////////////////////////////////////////
// the guy who receives commands as to which lighting sequence should be active.
//we should contain rather than inherit the Sequencer, but we are mutating C code and will do that later
class Worker: public NowDevice<DesiredSequence>, Sequencer {
#define NUM_LEDS          89
    const unsigned NUM_SHOW_LIGHTS   = 33;
    const unsigned RUN_LIGHT_SPACING = 15;
    const unsigned MAX_BRIGHTNESS = 80;

    //grouped for easier excision.
    class ELgroup {
        const int numPins = 8;//sizeof(ELPins) / sizeof(decltype(ELPins));

      public:

        void setup() {
          for (int i = 0; i < numPins; i++) {
            pinMode(ELPins[i], OUTPUT);
          }
          allOff();
        }

        void allOff() {
          for (int i = 0; i < numPins; i++) {
            digitalWrite(ELPins[i], LOW);
          }
        }

        void toggleRandom(unsigned numberToToggle) {
          allOff();
          for (int i = 0; i < numberToToggle; i++) {
            digitalWrite(ELPins[random8(0, 7)], HIGH);
          }
        }
    } EL;

    CRGB leds[NUM_LEDS];
    CRGB ledColor;
    CRGB showLightColor;
    CRGB exitLightColor;
    CRGB ledOff;

    int speed = 0;
    int ringLightPosition = 0;
    int ringAcceleration = 1;

    const int showLightIndex[3] = {0, 30, 60};//these are inlined elsewhere

    //no need for protoypes inside a class, you can forward reference without announcing it
    //    bool atTime(int minutes, int seconds, int milliseconds);
    //    void startSequence(int sequenceToPlay);
    //    void sequence0();
    //    void sequence1();
    //    void sequence2();
    //    void sequence3();
    //    void sequence4();

  public:
    void setup() {
      FastLED.addLeds<WS2811, LED_PIN, GRB>(leds, NUM_LEDS);
      EL.setup();

      //todo: is there a constructor that takes these arguments?
      ledColor.setRGB(0, 247, 255);
      showLightColor.setRGB(240, 222, 163);
      exitLightColor.setRGB(240, 0, 0);
      ledOff.setRGB(0, 0, 0);

      NowDevice::setup();//call after local variables setup to ensure we are immediately ready to receive.
    }

    void loop() {
      if (dataReceived) {
        sequenceToPlay = message.number;
        dataReceived = false;
        startSequence(sequenceToPlay); //the code formerly here was duplicated inside startSequeuce which was ...called here.
      }
    }

    //this is called once per millisecond, from the arduino loop(). It can skip millisecond values if there are function calls which take longer than a milli.
    void onTick(MilliTick now) {
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
        FastLED.show();
        frameTimer.next(frameDelay);

      }
    }

  private:
    void startSequence(int sequenceToPlay) {
      if (sequenceToPlay == currentSequence) {
        // If another request for the same sequence is received, then consider it a stop request
        currentSequence = -1;
      } else {
        currentSequence = sequenceToPlay;
      }

      EL.allOff();
      frame = 0;
    }


    // Nothing is happening
    void sequence0() {
      speed = 1;
      ringLightPosition = 0;

      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = ledOff;
      }
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
      int numberOfSteps = 80;

      for (int i = 0; i < NUM_LEDS; i++) {
        if (i % RUN_LIGHT_SPACING == 0) {
          if (frame < numberOfSteps) {
            // Fade in running lights
            if (i == 0 || i == 30 || i == 60) { //these are the values in showLightIndex[] init. The 1/3rd points of the chains.
              // Fade from show lights
              leds[i].r = map(frame, 0, numberOfSteps, showLightColor.r, ledColor.r);
              leds[i].g = map(frame, 0, numberOfSteps, showLightColor.g, ledColor.g);
              leds[i].b = map(frame, 0, numberOfSteps, showLightColor.b, ledColor.b);
            } else {
              // Fade from black
              leds[i].r = map(frame, 0, numberOfSteps, 0, ledColor.r);
              leds[i].g = map(frame, 0, numberOfSteps, 0, ledColor.g);
              leds[i].b = map(frame, 0, numberOfSteps, 0, ledColor.b);
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
        // ringLightPosition += 1;
        if (speed <= 8) {
          ringLightPosition += speed - 4;
          // ringLightPosition += 1;
        } else {
          // Don't go faster than speed 8
          ringLightPosition += 2;
          // ringLightPosition += 2;
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
          speed -= 1;
          if (speed < 0) {
            speed = 0;
          }
        }
      }

      if (speed != 0) {
        for (int i = 0; i < NUM_LEDS; i++) {
          if ((i - ringLightPosition) % RUN_LIGHT_SPACING == 0) {
            leds[i] = ledColor;
          } else {
            leds[i] = ledOff;
          }
        }
      } else {
        /* for(int i=0; i<NUM_LEDS; i++){
             leds[i] = ledOff;
          }*/
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
        /* for(int i=0; i<NUM_LEDS; i++){
           leds[i] = ledOff;
          }*/
        startSequence(4);
      }

    }

    // Cool Down
    void sequence4() {
      Serial.println(frame);
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i - ringLightPosition) % RUN_LIGHT_SPACING == 0) {
          leds[i] = exitLightColor;
        } else {
          leds[i] = ledOff;
        }
      }

      if (frame > 500) {
        // End of sequence
        for (int i = 0; i < NUM_LEDS; i++) {
          leds[i] = ledOff;
        }
        startSequence(-1);
      }
    }
} remote;

////////////////////////////////////////////////////////
/// maincontroller.ino:
class Boss: public NowDevice<DesiredSequence>, Sequencer {

    struct DebouncedInput {
      unsigned pin;
      bool activeHigh;
      bool readPin() {
        return digitalRead(pin) == activeHigh;
      }
      //official state
      bool StateCurrent = HIGH;
      bool StatePrevious = HIGH;
      MilliTick StateChangedTime = 0; //todo: replace with Ticker
      MilliTick DebounceDelay = 50;

      /** @returns whether the input has officially changed to a new state */
      bool onTick(MilliTick now) {
        auto presently = readPin();
        if (presently != StatePrevious) {
          StateChangedTime = now;//restart timer
          StatePrevious = presently;
        }

        if (StateChangedTime > now + DebounceDelay) {
          if (presently != StateCurrent) {
            StateCurrent = presently;
            return true;
          }
        }
        return false;
      }

      void setup(unsigned thePin) {
        this->pin = thePin;
        StateCurrent = StatePrevious = readPin();
      }

      operator bool() const {
        return StateCurrent;
      }
    } trigger;



    bool sequenceRunning = false;
    //    int frame = 0;
    //    int frameDelay = 20;
    //
    //    int sendRetryCount = 0;


    //    bool lastMessageFailed = false;

    esp_now_peer_info_t peerInfo;

    //    // callback when data is sent
    //    void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    //      Serial.print("\r\nLast Packet Send Status:\t");
    //      if (status == ESP_NOW_SEND_SUCCESS) {
    //        Serial.println("Delivery Succeeded");
    //        lastMessageFailed = false;
    //      } else {
    //        Serial.println("Delivery Failed");
    //        lastMessageFailed = true;
    //      }
    //    }




    void startSequence() {
      Serial.println("Start Sequence");
      frame = 0;
      sequenceRunning = true;
    }

    void runSequence() {

      if (atTime(0, 0, 0)) {
        // Start Audio
        digitalWrite(PIN_AUDIO_RELAY, HIGH);
        delay(100);//todo: fix this with a ticker!
        digitalWrite(PIN_AUDIO_RELAY, LOW);
      }

      if (atTime(0, 5, 0)) {
        // Send message to start ring light sequence (it has internal 3 second delay at beginning)
        sendMessage({0, 0});
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
      frame = 0;
      sequenceRunning = false;
      //todo: (maybe) signal remote that it also should be finished.
    }
  public:
    void setup() {

      pinMode(PIN_TRIGGER, INPUT);
      for (unsigned pin : PIN_AC_RELAY) {
        pinMode(pin, OUTPUT);
      }
      pinMode(PIN_AUDIO_RELAY, OUTPUT);

      NowDevice::setup();//must do this before we do any other esp_now calls.

      // Register peer
      memcpy(peerInfo.peer_addr, broadcastAddress, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;

      // Add peer
      if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        //need to finish local init even if remote connection fails.
      }

      // Turn off lights which are on by default
      //digitalWrite(PIN_AC_RELAY_1, HIGH);
      //digitalWrite(PIN_AC_RELAY_2, HIGH);

      Serial.println("Setup Complete");
    }

    void loop() {
      //todo: replace retry with the other end sending its sequence state to this end. If they don't compare then send again.
      // Retry any failed messages
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
        //todo: framedelay
        frame++;
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
  remote.setup();//which knows it is an esp_now device and setups eps_now.
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
