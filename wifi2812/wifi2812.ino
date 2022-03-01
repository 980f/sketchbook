#include <NeoPixelSegmentBus.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <NeoPixelBrightnessBus.h>

#undef countof  //macro was defined in deeply nested headers but is too generic a name
//#define countof(arr) (sizeof(arr)/sizeof(*arr))

#include "WS2812FX.h"

#define LED_COUNT 200

#define LED_PIN 2


/*
  other pin preferences:
  D1:SCL
  D2:SDA
  D3: force boot mode (aka FLASH)
  D7/GPIO 13: MOSI
*/

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart1800KbpsMethod> strip(LED_COUNT, LED_PIN); //NB: LED_PIN is ignored for some methods but always tolerated


#include "millievent.h"
#include "histogrammer.h"
#include "stopwatch.h"
Histogrammer<100> showTimes;
StopWatchCore showTimer(false, true); //2nd true->micros
Histogrammer<100>::ShowOptions showopts{true, true, 3000};

#include "chainprinter.h"
ChainPrinter dbg(Serial, true);

//esp puts some instruction set macros in global namespace!:
#undef cli

#include "clirp.h"
CLIRP cli;

#include "digitalpin.h"

DigitalOutput blinker(LED_BUILTIN);

DigitalInput beDark(D5, LOW);

bool isDark = false;

void showem() {
  showTimer.start();
  strip.Show();
  showTimer.stop();
  showTimes(showTimer.peek() / 100, dbg.raw, showopts); //100 us to 10 ms range
}

/**
  called from fx' service:
*/
void myCustomShow(void) {
  if (strip.CanShow()) {
    //double buffering so that we don't have to wait for 'CanShow' before service the fx
    // copy the WS2812FX pixel data to the NeoPixelBus instance
    memcpy(strip.Pixels(), ws2812fx.getPixels(), strip.PixelsSize());
    strip.Dirty();
    showem();
  }
}
/////////////////////////////////////////////
struct FxTriplet {
  uint8_t mode = FX_MODE_SPARKLE;
  uint8_t brightness = 100;
  uint16_t speed = 200;
  void show(ChainPrinter &dbg) {
    dbg("\tmode:", mode, ' ', _names[mode]);
    dbg("\tbrightness:", brightness);
    dbg("\tspeed:", speed );
  }

  void apply() {
    ws2812fx.setBrightness(brightness);
    ws2812fx.setSpeed(speed);
    ws2812fx.setMode(mode);
  }
} fx[2];


/////////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  ws2812fx.init();
  isDark = !beDark; //force apparent change

  fx[0].mode = FX_MODE_RAINBOW ;
  fx[0].brightness = 100;
  fx[0].speed = 200;

  fx[1].mode = FX_MODE_SPARKLE;
  fx[1].brightness = 100;
  fx[1].speed = 200;


  fx[1].apply();
  // set the custom show function
  ws2812fx.setCustomShow(myCustomShow);


  ws2812fx.start();


  // MUST run strip.Begin() after ws2812fx.init(), so GPIOx is initalized properly
  strip.Begin();
  showem();
}


void clido(int key) {

  bool isUpper = key <= 'Z'; //can't remember asrduino name for isupper.
  switch (tolower(key)) {
    case ' ':
      dbg("1/Dark:");
      fx[1].show(dbg);
      dbg("0/Light:");
      fx[0].show(dbg);
      dbg("Active:", isDark ? "Dark" : "Light");
      break;
    case 'm':
      ws2812fx.setMode(fx[beDark].mode = cli.arg);
      break;
    case 's':
      ws2812fx.setSpeed(fx[beDark].speed = cli.arg);
      break;
    case 'b':
      ws2812fx.setBrightness(fx[beDark].brightness = cli.arg);
      break;
    case 'x':
      { auto pack = dbg.stackFeeder(false);
        dbg('\n');
        for (unsigned di = 32; di-- > 0;) {
          dbg((di & 3) ? ' ' : '\t', digitalRead(di));
        }
      }
      break;
    default:
      dbg("Key:", key, " (", char(key), ")");
      break;
  }
}

void checkcli() {
  for (unsigned ki = Serial.available(); ki-- > 0;) {
    auto key = Serial.read();
    if (cli(key)) {
      clido(key);
    }
  }
}

MonoStable toggler(2000);

void loop() {
  if (MilliTicker) {

    if (changed(dbg.stifled, !bool(Serial))) {
      dbg("\nWifilights  ");
    }

    ws2812fx.service();
   
    checkcli();
    
    if (changed(isDark, beDark)) {
      dbg("Switching to mode:", isDark ? "Dark" : "Light");
      fx[isDark].apply();
    }

    blinker = bool(Serial);
    
  }
}
//EOF.
