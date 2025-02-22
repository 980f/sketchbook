
/////////////////////////
#include "cheaptricks.h" //eztypes.h

#include "dbgserial.h"

#include "clirp.h" //commandline interpreter wtih reverse polish input, all args precede operator.

class SUI { //Simple User Interface. Binds together a console and an RPN command parser.
    CLIRP<> cli;
    decltype(Serial) &cin;
    ChainPrinter cout;
  public:
    SUI (decltype(Serial) &keyboard, Print&printer): cin(keyboard), cout(printer, true) {}

    using User = void(*)(char key);

    void operator()(User handler) {
      for (unsigned strokes = cin.available(); strokes-- > 0;) {
        char key = cin.read();
        if (cli.doKey(key)) {
          handler(key);
        }
      }
    }

    unsigned has2() const {
      return cli.twoargs();
    }

    bool hasarg() const {
      return bool(cli.arg);
    }

    operator unsigned() {
      return cli.arg;
    }

    unsigned more() {
      return cli.pushed;
    }

};

#include "digitalpin.h"

#ifndef LED_BUILTIN
//todo: ifdef on boasrd identifiers, or fix esp32 board files!
#define LED_BUILTIN 13
#endif

DigitalOutput imAlive(LED_BUILTIN);
//initially named 'T1' and 'T2' those names conflicted with what should have been enums in Arduino.h of the ESP32.
DigitalOutput Tester1(12);//12,11 are LEDs on xia0
DigitalOutput Tester2(11);

 
#include "millievent.h" //millis() utilities
OneShot pulse;
MonoStable periodic;


#include "millichecker.h" //used to report on any action that takes longer than a millisecond
MilliChecker<100> skipper;


#include "scani2c.h" //checks the I2C bus for devices
/////////////////////////////

SUI sui(Serial, Serial);

void setup() {
  Serial.begin(115200);//ignored by SerialUSB implementations.
}


////////////////////////////
void loop() {
  Tester1 = pulse.isRunning();
  Tester2 = dbg.stifled;

  if (dbg.stifled) {
    //NB: polling Serial for connection gets whacked by a delay(10ms) in that code. There is no explanation for why that is required.
    if (changed(dbg.stifled, !Serial)) {
      dbg("libTester Connected at ", MilliTicker.recent());//disconnect might queue a 'connected' message ?!
    }
  }

  if (MilliTicker) {//this is true once per millisecond
    skipper.check();
    //the following seems to be written for some unknown variant of the millichecker, but I found no branch with such a thing.
//    if (skipper(dbg.raw, 1000, true)) { //see if we are occasionally skipping milliseconds
//      dbg("Absolute time:", MilliTicker);
//    }
  }

  sui([](char key) {//the sui will process input, and when a command is present will call the following code.
    bool upper = key < 'a';
    switch (tolower(key)) {
      default:
        dbg("ignored:", unsigned(sui), ',', key);
        break;
      case 'i'://i2cscan  takes 12 ms
        scanI2C(dbg);
        break;
      case 'd':
        dbg.stifled = upper; 
        break;

      case 'x':
        delay(sui);
        break;

      case 'o':
        if (sui.hasarg()) {
          periodic = sui;
        } else {
          periodic.start();
        }
        [[fallthrough]];
      case 'l':
        dbg("MS :", periodic.isRunning(), bool(periodic), " due:", periodic.due(), " now:", MilliTicker);
        break;

      case 'p':
        pulse = sui.hasarg() ? sui : 1234; //if no param then a bit over one second.
        [[fallthrough]];
      case ' '://info for regression being worked on
        dbg("RB :", pulse.isRunning(), bool(pulse), " due:", pulse.due(), " now:", MilliTicker);
        break;
    }
  });
}


#if 0
The histogram (skipped()) showed early on 71 10 ms skips and one 11 ms skip. so checking SerialUSB when starting seems to take around 10 ms.
next run 185 at 10, 2 at 11. 50 / 1.




#endif
