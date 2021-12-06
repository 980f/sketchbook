//(C) 2019 by Andy Heilveil, github/980F
//started as a demo of digitalpin.h, being expanded to test the actual pins and I2C and SPI GPIO as well.

#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

#include "scani2c.h"
#include "cheaptricks.h" //for changed()
#include "millievent.h"

#include "digitalpin.h"

#include "clirp.h"  //CLIRP== Command Line Interpreter, Reverse Polish value input
CLIRP<> cli;
bool echoinput = true;
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

//the QTPY only has a neopixel, the circuitplayground express has one but also 10 neopixels.
#ifdef ADAFRUIT_QTPY_M0
#define BOARD_NS QTPY
#include "qtpy.board.h"
QTPY::NeoPixel led;
QTPY::NeoPixel::ColorChannel red(led, 'r');
QTPY::NeoPixel::ColorChannel green(led, 'g');
QTPY::NeoPixel::ColorChannel blue(led, 'b');

#elif ARDUINO_SAMD_CIRCUITPLAYGROUND_EXPRESS
#define BOARD_NS CPE
#include "cpe.board.h"
BOARD_NS ::NeoPixel led;
BOARD_NS ::NeoPixel::ColorChannel red(led, 'r');
BOARD_NS ::NeoPixel::ColorChannel green(led, 'g');
BOARD_NS ::NeoPixel::ColorChannel blue(led, 'b');

#else
byte red, blue, green;//stub for color control of qtpy neopixel

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


void showDefines() {
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
  dbg("builtin LED ", LED_BUILTIN);
#else
  dbg("No builtin LED");
#endif

}

void setup() {
  Serial.begin(115200);//can't use dbg here
  //instead of the traditional spin on !Serial we test it in the loop() at each reference.
  BOARD_NS::setup();

  led = true;//indicate that we made it through setup.
}

//unsigned loopcount = 0;

//EdgyInput ei(24);



void loop() {
  if (Serial && Serial.available()) {
    auto key = Serial.read();
    bool upper = isupper(key);
    if (echoinput) {
      Serial.write(key);
    }
    if (cli.doKey(key)) {
      switch (tolower(key)) {
        case 'i':
          scanI2C(dbg);
          break;
        case 'l':
          led = upper;
          break;
        case 'd':
          showDefines();
          break;
        case 'r':
          red = cli.arg;
          break;
        case 'g':
          green = cli.arg;
          break;
        case 'b':
          blue = cli.arg;
          break;        
        default:
          dbg(" (", key , ") ");
          break;
      }
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

}
