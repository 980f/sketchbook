


#include <ESPTelnet.h>

#if defined(ARDUINO_ARCH_ESP8266)
#define DoWakeup 1
#endif

#include <NeoPixelSegmentBus.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <NeoPixelBrightnessBus.h>

#undef countof  //macro was defined in deeply nested headers but is too generic a name, and conflicted with following includes

#include "WS2812FX.h"

const unsigned LED_COUNT = 3 * 30 + 60 + 8; //3 strips of 30, 1 of 60, might add a ring of 8.



//D4 on d1-mini, D2 on esp32-wroom devkit. Use GPIO value here, not arduino pin name.
const unsigned LED_GPIO = 2; //esp8266 uart 1 tx


WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_GPIO, NEO_GRB + NEO_KHZ800);
NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart1800KbpsMethod> strip(LED_COUNT, LED_GPIO); //NB: LED_GPIO is ignored for some methods but always tolerated


#include "millievent.h"
#include "histogrammer.h"
#include "stopwatch.h"
Histogrammer<100> showTimes;
StopWatchCore showTimer(false, true); //2nd true->micros
Histogrammer<100>::ShowOptions showopts(true, true, 3000);

#include "chainprinter.h"
ChainPrinter dbg(Serial, true);

//esp puts some instruction set macros in global namespace!:
#undef cli

#include "clirp.h"
CLIRP<> cli;

#include "digitalpin.h"
#ifndef LED_BUILTIN
#define LED_BUILTIN 18
#endif
DigitalOutput blinker(LED_BUILTIN);

#ifndef D5  //using this to distinguish between ESP01 and D1 variants
#define D5 14
#endif

DigitalInput beDark(D5, LOW);

//we don't immediately react to button, so that operator can bobble it without changing state
bool isDark = false;
bool wantDark = false;

const char * darkness(bool darkly) {
  return darkly ? "Dark" : "Light";
}

/** triggered on going dark, stays dark while active */
MonoStable bouncer(2000);
OneShot updelay;


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
//static randomish with some flicker
// 18 up 12 over 20ish down, 10 on floor, 1 hidden, match first up, then differently spaced
uint16_t staticFlicker() {
  static unsigned dutycycler = 0;
  //pick one randomly to be white for one cycle
  unsigned twinkler = (++dutycycler % 10) ? ~0 : random(LED_COUNT);

  for (unsigned pi = LED_COUNT; pi-- > 0;) {
    uint8_t scrambled = pi << 4 | ((pi >> 4) & 0xF);
    auto color = pi == twinkler ? ~0 : ws2812fx.color_wheel(scrambled);
    ws2812fx.setPixelColor(pi, color);
  }
  return fx[0].speed;
}

/////////////////////////////////////////////

void bugshot(const char *prefix, const OneShot &bugger) {
  dbg(prefix, " ", bugger.isRunning() ? 'R' : 's', " ", bouncer.due(), " @", bouncer.expiry());
}

void buggy(const char *prefix) {
  bugshot(prefix, bouncer);
}


void clido(int key) {

  bool isUpper = key <= 'Z'; //can't remember asrduino name for isupper.
  switch (tolower(key)) {
    case ' ':
      dbg("1/Dark:");
      fx[1].show(dbg);
      dbg("0/Light:");
      fx[0].show(dbg);
      dbg("Active:", darkness(isDark));
      dbg("Pending:", darkness(wantDark));
      dbg("Switch:", darkness(beDark));
      dbg("Bouncing:", bouncer.due());
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
    case 't'://test the timer!
      {
        auto spewmenot = SoftMilliTimer::logging(true);
        buggy("init");
        dbg("previous:", bouncer.set(1500));
        buggy("Via set:");
        bouncer.start();
        buggy("started");
        for (unsigned trials = 6; trials-- > 0;) {
          delay(300);
          MilliTicker.ticked();
          buggy("ticked");
        }

        bouncer.stop();
        buggy("stopped");

        bouncer = 654;
        buggy("op=");
        delay(754);
        MilliTicker.ticked();
        buggy("later");
      }
      break;
    case 'u':
      dbg(MilliTicker.recent(), "?=", millis(), " +10k:", MilliTicker[10000]);
      {
        auto spewmenot = SoftMilliTimer::logging(true);
        OneShot wtf;

        wtf = 1234;
        bugshot("u1:", wtf);
        delay(200);
        MilliTicker.ticked();
        bugshot("Ticked:", wtf);
        for (unsigned trials = 6; trials-- > 0;) {
          delay(300);
          MilliTicker.ticked();
          dbg("tick:", bool(wtf));
          bugshot("tock:", wtf);
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
////////////////////////

class WifiSerial {

    ESPTelnet telnet;
    IPAddress ip;

#if DoWakeup
    bool woke = false;
    Monostable waking; //200 after forceSleepWake
#endif
    bool connected = false;

  public://todo: make a constructor as these are likely to be constants
    uint16_t  port = 23;
    const char* ssid;
    const char* password;

    void attach(){
  // todo: use these instead of tryConnect state machine
//  
//  telnet.onConnect();
//  telnet.onConnectionAttempt(onTelnetConnectionAttempt);
//  telnet.onReconnect(onTelnetReconnect);
//  telnet.onDisconnect(onTelnetDisconnect);
//  
  //
  telnet.onInputReceived([](String str) {
    // checks for a certain command
    if (str == "ping") {
      telnet.println("> pong");
      Serial.println("- Telnet: pong");
    // disconnect the client
    } else if (str == "bye") {
      telnet.println("> disconnecting you...");
      telnet.disconnectClient();
      }
  });

  Serial.print("- Telnet: ");
  if (telnet.begin(port)) {
    Serial.println("running");
  } else {
    Serial.println("error.");
    errorMsg("Will reboot...");
  }
}
    }

    ////////////////////////////////////////

    bool isConnected() const {
      return (WiFi.status() == WL_CONNECTED);
    }

    bool tryConnect() {
      if (changed(connected, isConnected())) {
        if (connected) { //then just connected
          WiFi.setAutoReconnect(true);
          WiFi.persistent(true);
          return true;
        } else {
          //lost connection, do we need to do anything?
        }
      }

      WiFi.mode(WIFI_STA);
#if DoWakeup
      if (waking.done()) { //the 'true once' function
        woke = true;
      } else if (waking) {
        return false;
      }
      if (!woke) {
        WiFi.forceSleepWake();
        waking = 200;
        return false;
      }
#endif

      WiFi.begin(ssid, password);

      return isConnected();
    }



    void onTick() {
      tryConnect();
      if(connected){
         
      }
    }

};

////////////////////////
void setup() {
  Serial.begin(115200);
  ws2812fx.init();
  ws2812fx.setCustomShow(myCustomShow);
  ws2812fx.setCustomMode(staticFlicker);

  fx[0].mode = FX_MODE_CUSTOM ;
  fx[0].brightness = 100;
  fx[0].speed = 22;

  fx[1].mode = FX_MODE_BREATH;
  fx[1].brightness = 50;
  fx[1].speed = 5000;

  isDark = wantDark = beDark; //match switch on powerup
  fx[isDark].apply();

  ws2812fx.start();

  // MUST run strip.Begin() after ws2812fx.init(), so GPIOx is initalized properly
  strip.Begin();
  showem();
}


void loop() {
  if (MilliTicker) {
    auto pop = SoftMilliTimer::logging(false);
    if (changed(dbg.stifled, !bool(Serial))) {
      dbg("\nWifilights  ");
      clido(' ');
    }

    ws2812fx.service();

    checkcli();

    if (changed(wantDark, beDark)) {
      dbg("Button:", darkness(wantDark));
      //      bouncer.start();
      updelay = 2000;
    }

    //    if (bouncer.isDone()) {//is failing to autodisable when done!
    if (updelay) {
      if (changed(isDark, wantDark)) {
        dbg("Switching to mode:", darkness(isDark));
        fx[isDark].apply();
      }
    }

    //    blinker = updelay.isRunning();

  }
}
//EOF.
