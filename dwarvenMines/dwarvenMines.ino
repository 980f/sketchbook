
// MAC: D0:EF:76:58:DB:98
// New: D0:EF:76:5C:7A:10

#define FASTLED_INTERNAL
#include <FastLED.h>
//you should never need to include this in an .ino file: #include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

//pin assignments being globalized is convenient syntactically, versus keeping them local to the using classes, for constant init reasons:
const unsigned LED_PIN           = 13;
const int ELPins[] = {4, 16, 17, 18, 19, 21, 22, 23};

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

// Structure to convey data
struct Message {
  int action;
  int number;
  void printOn(Print &stream) {
    stream.print("Action: ");
    stream.println(action);
    stream.print("Number: ");
    stream.println(number);
    stream.println();
  }
};


class NowDevice {
  protected:
    Message message;
    bool dataReceived = false;

    // callback function that will be executed when data is received
    void onMessage(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
      memcpy(&message, incomingData, sizeof(message));
      Serial.print("Bytes received: ");
      Serial.println(len);
      message.printOn(Serial);
      dataReceived = true;
    }

    static NowDevice *receiver; //only one receiver is allowed at this protocol level.
    static void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {//esp32 is not c++ friendly in its callbacks.
      if (receiver) {
        receiver->onMessage(esp_now_info, incomingData, len);
      }
    }

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
      } else {
        Serial.println("Error initializing ESP-NOW");
      }

    }
};

NowDevice *NowDevice ::receiver; //only one receiver is allowed at this protocol level.
///////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////
// the guy who receives commands as to which lighting sequence should be active.
class Worker: public NowDevice {
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

    int sequenceToPlay = -1;
    int currentSequence = -1;
    int frame = 0;

    int frameDelay = 20;
    Ticker frameTimer;

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
    void onTick() {
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
} worker;

//arduino's setup:
void setup() {
  Serial.begin(115200);
  worker.setup();//which knows it is an esp_now device and setups eps_now.
}

//arduino's loop:
void loop() {
  if (Ticker::check()) { //read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    worker.onTick();
  }
  worker.loop();
}
