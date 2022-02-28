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
ChainPrinter dbg(Serial);

#include "clirp.h"
CLIRP cli;

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
void setup() {
  Serial.begin(115200);
  dbg("\nWifilights");

  ws2812fx.init();
  ws2812fx.setBrightness(100);
  ws2812fx.setSpeed(200);
  ws2812fx.setMode(FX_MODE_RAINBOW_CYCLE);

  // set the custom show function
  ws2812fx.setCustomShow(myCustomShow);


  ws2812fx.start();


  // MUST run strip.Begin() after ws2812fx.init(), so GPIOx is initalized properly
  strip.Begin();
  showem();
}


void loop() {
  if (MilliTicker) {
    ws2812fx.service();
    //
    //    if (longest < elapsed) {
    //      longest = elapsed;
    //      Serial.println(elapsed);
    //    }
    //    if ((lastChecked % 5000) == 0 ) {
    //      Serial.println(elapsed);
    //    }
  }
}
