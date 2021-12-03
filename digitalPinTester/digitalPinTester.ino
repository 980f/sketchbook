//(C) 2019 by Andy Heilveil, github/980F
//
//
////#define CFG_TUD_CDC 1
////#include <Adafruit_TinyUSB.h>
//
//#define HaveSerial 0
//
//#if HaveSerial
#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.
//
//#include "scani2c.h"
//
//#else
//#define dbg(...)
//#endif
//
#include "cheaptricks.h" //for changed()
#include "millievent.h"
//
#include "digitalpin.h"

//#include "clirp.h"
//
//DigitalInput button(4, LOW);
//
//DigitalOutput a(18);
//DigitalOutput b(17);
//DigitalOutput c(16);
//
////you can make arrays:
//
//#ifdef ESP_PLATFORM
//const DigitalOutput nib[] = {{5}, {4}, {3}};
//const DigitalInput quad[] = {{9} , {8}, {7}, {6}};
//#else
//const DigitalOutput nib[] = {{18}, {17}, {16}};
//const DigitalInput quad[] = {{23} , {20}, {21}, {24}};
//#endif

//bool last[4];
//
//class EdgyInput {
//    bool last;
//    const DigitalInput pin;
//  public:
//    EdgyInput(unsigned which): pin(which) {
//      last = this->operator bool();
//    }
//
//    operator bool () {
//      return bool(pin);
//    }
//
//    bool changed() {
//      return ::changed(last, *this);
//    }
//
//};


#ifdef ADAFRUIT_QTPY_M0
#include "qtpy.board.h"
QTPY::NeoPixel led;
QTPY::NeoPixel::ColorChannel red(led,'r');
QTPY::NeoPixel::ColorChannel green(led,'g');
QTPY::NeoPixel::ColorChannel blue(led,'b');

//QTpyPixel::ColorSetter ledColor(led);


#else

#ifdef LED_BUILTIN
//#warning "using built-in LED"
DigitalOutput led(LED_BUILTIN);
#else
#warning "No builtin LED"
bool led;
#endif

#endif


//BiStable toggler(250, 550);
//
//bool butevent = 0;
//
////CLIRO== command line interpreter, reverse polish value input
//CLIRP<> cli;


void showDefines() {

  //esp8266 build uses different build tokens, grrr.
#ifdef ARDUINO_BOARD
  dbg("ARDUINO_BOARD:", ARDUINO_BOARD);
#endif

#ifdef ARDUINO_VARIANT
  dbg("ARDUINO_VARIANT:", ARDUINO_VARIANT);
#endif

  //samdD build tokens, ~1.8.10.
#ifdef ARDUINO
  dbg("ARDUINO:", ARDUINO);
#endif

#ifdef ARDUINO_ARCH_SAMD
  dbg("ARDUINO_ARCH_SAMD:", ARDUINO_ARCH_SAMD);
#endif

#ifdef LED_BUILTIN
  dbg("builtin LED defined as ", LED_BUILTIN);
#else
  dbg("No builtin LED");
#endif

}

void setup() {
  //the following doesn't wait if USB, but USB buffers data prior to host connection OR connects before setup.
  Serial.begin(115200);//can't use dbg here
#ifdef ADAFRUIT_QTPY_M0
  QTPY::setup();
#endif
}

//unsigned loopcount = 0;

//EdgyInput ei(24);

void loop() {
  if (Serial && Serial.available()) {
    auto key = Serial.read();
    bool upper = isupper(key);
    Serial.write(key);
    switch (tolower(key)) {
      case 'l':
        led = upper;
        break;
      case 'd':
        showDefines();
        break;
#ifdef ADAFRUIT_QTPY_M0
      case 'r':
        red = upper ? 255  : 0;
        break;
      case 'g':
        green = upper ? 255  : 0;
        break;
      case 'b':
        blue = upper ? 255 : 0;
        break;
#endif
    }
  }
  if (MilliTicked) {//slow down check to minimize worst of switch bounce.
    //    for (unsigned i = countof(nib); i-- > 0;) {
    //      if (changed(last[i], quad[i])) {
    //        dbg( i, " changed to ", last[i]);
    //        nib[i] = last[i];
    //      }
    //    }
    //
    //    if (ei.changed()) {
    //      dbg("EiEi:", bool(ei)); //ei doesn't have a native print interface (yet) so we have to cast it to one that does.
    //    }
    //
    //    led = bool(1 & (millis() / 500)) ; //toggler;
  }
  //
  //#if HaveSerial
  //  if (Serial) {
  //    auto key = Serial.read();
  //    if (cli.doKey(key)) {
  //      switch (tolower(key)) {
  //        case 'l':
  //          led = bool(islower(key));
  //          break;
  //        case 'i':
  //          scanI2C(dbg);
  //          break;

  //      }
  //    }
  //  }
  //#endif

}
