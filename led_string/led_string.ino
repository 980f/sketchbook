

/* This started as an example program from Adafruit.
  It has been modified by 980F so that instead of a canned demo you can pick from the various modes of the original.
  It also allows for other things to happen such as testing inputs to change what gets done.

  And later modified to do some canned stuff on powerup, to act as a string connection tester

  The original file header:
  // A basic everyday NeoPixel strip test program.

  // NEOPIXEL BEST PRACTICES for most reliable operation:
  // - Add 1000 uF CAPACITOR between NeoPixel strip's + and - connections.
  // - MINIMIZE WIRING LENGTH between microcontroller board and first pixel.
  // - NeoPixel strip's DATA-IN should pass through a 300-500 OHM RESISTOR.
  // - AVOID connecting NeoPixels on a LIVE CIRCUIT. If you must, ALWAYS
  //   connect GROUND (-) first, then +, then data.
  // - When using a 3.3V microcontroller with a 5V-powered NeoPixel strip,
  //   a LOGIC-LEVEL CONVERTER on the data line is STRONGLY RECOMMENDED.
  // (Skipping these may work OK on your workbench but can fail in the field)
*/

#ifdef __AVR__
#include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif


//pin that is monitored for automatic running. When low interactive, when high (missing jumper ;) automatic.
#define AutoRunPin 0

//980F patched the pins.h in the adafruit_neopixel_zerodma_master directory under libraries in order to add the XAIO with DMA
//if your board isn't in that file don't use DMA!
#define UseDMA 0

// Which pin on the Arduino is connected to the NeoPixels?
// On a Trinket or Gemma we suggest changing this to 1:
//EPS-01S your choice is pin 2
#define LED_PIN 2

// How many NeoPixels are attached to the Arduino?
#define LED_COUNT 300


#if UseDMA
//formerly included for all processor types, need to limit to zeroes: #include <Adafruit_ZeroDMA.h>
#include <Adafruit_NeoPixel_ZeroDMA.h>
Adafruit_NeoPixel_ZeroDMA strip(LED_COUNT, LED_PIN, NEO_GRB);
#else
#include <Adafruit_NeoPixel.h>
// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)


/** each class here expects to get called every 50 ms or so. */
struct LightingEffect {
  static unsigned numPixels;// = LED_COUNT; //got tired of calling strip.numPixels(), a function that returns a constant!
  static bool defaultReverse;// = false;

  bool reverse;//swap order of lights in effect. Handy for back and forth bouncing.

  /** most effects do a loop over the number of pixels*/
  unsigned looper;

  virtual bool next() {
    return looper-- > 0;
  }

  void restart() {
    looper = numPixels;
  }

  /** Set pixel's color (in RAM) */
  void set(uint32_t color) {
    strip.setPixelColor(reverse ? looper : (numPixels - looper), color);
  }

  /** this is called for each pixel. Typical overrides call next() and if true set(some color computed from looper) then return inverse of what next returned;
    @return true when effect has just set last pixel in its cycle
    This is the animation tick */
  virtual bool tick() = 0;

  virtual void start(bool inreverse) {
    reverse = inreverse;
    restart();
  }


  virtual const char *name() {
    return "No effect";
  }

  virtual void buggy() {
    Serial.println();
    Serial.print(name());
    Serial.print(reverse ? " forward " : " reverse ");
  }

};

unsigned LightingEffect ::numPixels = LED_COUNT; //got tired of calling strip.numPixels(), a function that returns a constant!
bool LightingEffect ::defaultReverse = false;


class WiringTest: public LightingEffect {
    uint32_t color;
        
  public:
    bool tick() override {
      if (next()) {
        for (int i = 0; i < looper; i++) {
          set(color);
        }
        if(0==(looper%10)){
          color>>8; //red becomes green then blue
          if(!color){
            color=strip.Color(80,  0,   0);
          }
        }
        return true;
      } else {
        restart();
        for (int i = 0; i < numPixels; i++) {
          set(0);
        }
        return false;
      }
    }

    void start(bool inreverse) override {
      color = strip.Color(80,  0,   0);
      LightingEffect::start(inreverse);
    }

    void buggy() override {
      LightingEffect::buggy();
      Serial.println(" testing wiring integrity");
    }

    const char *name() {
      return "Wire Tester";
    }

} tester;

/*
  for (int i = 0; i < strip.numPixels(); i++) { // For each pixel in strip...
    strip.setPixelColor(i, color);         //  Set pixel's color (in RAM)
    strip.show();                          //  Update strip to match
    delay(wait);                           //  Pause for a moment
  }
*/
class ColorWipe: public LightingEffect {
    uint32_t color;
  public:
    bool tick() override {
      if (next()) {
        set(color);
        return false;//not done yet.
      }
      Serial.println();
      //loop colors here
      color >>= 8;
      if (color == 0) {
        return true;
      }

      restart();
      return false;
    }

    void start(bool inreverse) override {
      color = strip.Color(255,   0,   0);
      LightingEffect::start(inreverse);
    }

    void buggy() override {
      LightingEffect::buggy();
      Serial.println(color);
    }


    const char *name() {
      return "Wiper";
    }

} wiper;



/**
  // Hue of first pixel runs 5 complete loops through the color wheel.
  // Color wheel has a range of 65536 but it's OK if we roll over, so
  // just count from 0 to 5*65536. Adding 256 to firstPixelHue each time
  // means we'll make 5*65536/256 = 1280 passes through this outer loop:
  for (long firstPixelHue = 0; firstPixelHue < 5 * 65536; firstPixelHue += 256) {
    for (int i = 0; i < strip.numPixels(); i++) { // For each pixel in strip...
      // Offset pixel hue by an amount to make one full revolution of the
      // color wheel (range of 65536) along the length of the strip
      // (strip.numPixels() steps):
      int pixelHue = firstPixelHue + (i * 65536L / strip.numPixels());
      // strip.ColorHSV() can take 1 or 3 arguments: a hue (0 to 65535) or
      // optionally add saturation and value (brightness) (each 0 to 255).
      // Here we're using just the single-argument hue variant. The result
      // is passed through strip.gamma32() to provide 'truer' colors
      // before assigning to each pixel:
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
    }
    strip.show(); // Update strip with new contents
    delay(wait);  // Pause for a moment
  }
*/
class Rainbow: public LightingEffect {
    const  uint32_t hueChunker = 65536;
    unsigned hueLooper;

    unsigned firstPixelHue = 0;
    //firstPixelHue < 5 * 65536; firstPixelHue += 256) {

  public:
    bool tick() override {
      if (next()) {
        int pixelHue = firstPixelHue + ((looper * hueChunker) / numPixels);
        // strip.ColorHSV() can take 1 or 3 arguments: a hue (0 to 65535) or optionally add saturation and value (brightness) (each 0 to 255).
        // Here we're using just the single-argument hue variant. The result is passed through strip.gamma32() to provide 'truer' colors:
        set(strip.gamma32(strip.ColorHSV(pixelHue)));
        return false;//not done yet.
      }
      Serial.println();
      firstPixelHue += 256;
      if (--hueLooper == 0) {
        return true;
      }
      Serial.print("Next hue:"); Serial.println(firstPixelHue);
      restart();
      return false;
    }

    void start(bool inreverse) override {
      hueLooper = 5;
      firstPixelHue = 0;
      LightingEffect::start(inreverse);
    }

    void buggy() override {
      LightingEffect::buggy();
      Serial.println(hueLooper);
    }


    const char *name() {
      return "Rainbow";
    }

} rainbow;


/*
  // Theater-marquee-style chasing lights. Pass in a color (32-bit value,
  // a la strip.Color(r,g,b) as mentioned above), and a delay time (in ms)public:

  // between frames.
  void theaterChase(uint32_t color, int wait) {
  Serial.print("chase:");
  Serial.println(color);
  for (int a = 0; a < 10; a++) { // Repeat 10 times...
    for (int b = 0; b < 3; b++) { //  'b' counts from 0 to 2...
      strip.clear();         //   Set all pixels in RAM to 0 (off)
      // 'c' counts up from 'b' to end of strip in steps of 3...
      for (int c = b; c < strip.numPixels(); c += 3) {
        strip.setPixelColor(c, color); // Set pixel 'c' to value 'color'
      }
      strip.show(); // Update strip with new contents
      delay(wait);  // Pause for a moment
    }
  }
  }

*/
class Marquee: public LightingEffect {
    //for (int a = 0; a < 10; a++) { // Repeat 10 times...
    //    for (int b = 0; b < 3; b++) { //  'b' counts from 0 to 2...
    uint32_t color;
    int a;
    int b;

  public:

    void start(bool inreverse) override {
      color = strip.Color(127, 64, 32); //todo: allow entry

      a = 10;
      b = 3;
      LightingEffect::start(inreverse);
    }

    bool tick() override {
      if (--b == 0) {
        if (--a == 0) {
          return true;//cycle complete
        }
        b = 3;
      }
      strip.clear();
      for (looper = b; looper < numPixels; looper += 3) {
        set(color);
      }
      return false;
    }

    void buggy() override {
      LightingEffect::buggy();
      Serial.println();
    }


    const char *name() {
      return "Marquee";
    }

} marquee;

LightingEffect *currentEffect = &tester;

unsigned updateRate;

bool began = !UseDMA;

bool showticks = true;

bool autoRunning = true;


void startEffect(LightingEffect &effect, unsigned steprate_ms = 50) {
  if (!began) {
    Serial.println("you must init with an i first");
    return;
  }
  currentEffect = &effect;
  updateRate = steprate_ms > 0 ? steprate_ms : 50;
  effect.start(LightingEffect::defaultReverse);
  effect.buggy();
}



void beginInteractive() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("led strand tester");
  Serial.println("i: init (only once!)");
  Serial.println("c,t,r,e: different displays");
}

void initDriver() {
  if (began) return; //runonce
  //formerly automatic in setup but locks up the processor if you forget to check the return from begin()
  Serial.println("init library-");
#if UseDMA
  began = strip.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  if (!began) {
    Serial.println("given LED_PIN does not work");
    return;
  }
#else
  strip.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
#endif
  Serial.println("All off");
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(50); // Set BRIGHTNESS to about 1/5 (max = 255)
  Serial.println("-library is init, lights should be off");
}


void setup() {
  autoRunning = false;//digitalRead(AutoRunPin) == HIGH;
  if (!autoRunning) {
    beginInteractive();
  } else {
    beginInteractive();//until we have automation
  }
}

void loop() {
  if ((millis() % updateRate) == 0) {
    if (currentEffect) {
      if (showticks) {
        Serial.print('.');
      }
      if (currentEffect->tick()) {
        if (showticks) {
          Serial.println();
        }
        Serial.println("done");
        currentEffect->start(!currentEffect->reverse);
        currentEffect->buggy();
      }
      strip.show();
    }
  }

  if (autoRunning) {
    if (digitalRead(AutoRunPin) == LOW) {
      //todo: switch to interactive. That entails making sure Serial is working.
    }
  } else {
    if (Serial.available()) {
      char key = Serial.read();
      switch (tolower(key)) {
        case 13: case 10://buffered serial gets us newlines of any flavor.
          break;
        case 'i':
          initDriver();
          break;
        case 'b':
          if (currentEffect) {
            currentEffect->buggy();
          }
          break;
        case 'c':
          Serial.println("ColorWipe");
          startEffect(wiper);
          break;
        case 'm':
          Serial.println("marquee");
          startEffect(marquee, 20);
          break;
        case 'r':
          Serial.println("rainbow");
          startEffect(rainbow, 10);
          break;
        case 'e':
          break;
        case 'h':
          currentEffect = nullptr; //freeze
          Serial.println("Enter letter for effect:");
          break;
        case 't':
          showticks = !showticks;
          Serial.println(showticks ? "ticking" : "quiet");
        default:
          Serial.print(key);
          Serial.println("? I don't understand that.");
          break;
      }
    }
  }
}


// Rainbow-enhanced theater marquee. Pass delay time (in ms) between frames.
void theaterChaseRainbow(int wait) {
  Serial.println("chase rainbow");
  int firstPixelHue = 0;     // First pixel starts at red (hue 0)
  for (int a = 0; a < 30; a++) { // Repeat 30 times...
    for (int b = 0; b < 3; b++) { //  'b' counts from 0 to 2...
      strip.clear();         //   Set all pixels in RAM to 0 (off)
      // 'c' counts up from 'b' to end of strip in increments of 3...
      for (int c = b; c < strip.numPixels(); c += 3) {
        // hue of pixel 'c' is offset by an amount to make one full
        // revolution of the color wheel (range 65536) along the length
        // of the strip (strip.numPixels() steps):
        int      hue   = firstPixelHue + c * 65536L / strip.numPixels();
        uint32_t color = strip.gamma32(strip.ColorHSV(hue)); // hue -> RGB
        strip.setPixelColor(c, color); // Set pixel 'c' to value 'color'
      }
      strip.show();                // Update strip with new contents
      delay(wait);                 // Pause for a moment
      firstPixelHue += 65536 / 90; // One cycle of color wheel over 90 frames
    }
  }
}


#if 0
patch that was added to pins.h:

//980F:
#if defined(SEEED_XIAO_M0)
&sercom0, SERCOM0, SERCOM0_DMAC_ID_TX, 2, SPI_PAD_0_SCK_1, PIO_SERCOM,
&sercom2, SERCOM2, SERCOM2_DMAC_ID_TX, 4, SPI_PAD_2_SCK_3, PIO_SERCOM_ALT,

&sercom0, SERCOM0, SERCOM0_DMAC_ID_TX, 10, SPI_PAD_2_SCK_3, PIO_SERCOM_ALT,
&sercom2, SERCOM2, SERCOM2_DMAC_ID_TX, 4, SPI_PAD_0_SCK_1, PIO_SERCOM_ALT,

&sercom4, SERCOM4, SERCOM4_DMAC_ID_TX, 6, SPI_PAD_0_SCK_1, PIO_SERCOM_ALT,
#endif

}; // end sercomTable[]



#endif
