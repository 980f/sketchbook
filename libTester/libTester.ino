
/////////////////////////
#include "cheaptricks.h" //eztypes.h

#include "dbgserial.h"

#include "clirp.h"

class SUI {
    CLIRP<> cli;
    decltype(Serial) &cin;
    ChainPrinter cout;
  public:
    SUI (decltype(Serial) &keyboard, Print&printer): cin(keyboard), cout(printer, true) {}

    using User = void(*)(char key);

    void operator()(User handler) {
      for (unsigned strokes = Serial.available(); strokes-- > 0;) {
        char key = Serial.read();
        bool upper = key < 'a';
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

DigitalOutput imAlive(LED_BUILTIN);

//millis() utilities
#include "millievent.h"
OneShot pulse;
DigitalOutput T1(12);//12,11 are LEDs on xia0
DigitalOutput T2(11);


//used to report on any action that takes longer than a millisecond
#include "millichecker.h"
MilliChecker skipper;

//check the I2C bus for devices
#include "scani2c.h"
/////////////////////////////

SUI sui(Serial, Serial);

void setup() {

}


////////////////////////////
void loop() {
  T1 = pulse.isRunning();
  T2 = dbg.stifled;

  if (dbg.stifled) {
    //NB: polling Serial for connection gets whacked by a delay(10ms) in that code. There is no explanation for why that is required.
    if (changed(dbg.stifled, !Serial)) {//ignoring direction of change serves as a test of stifling
      dbg("libTester Connected at ", MilliTicker.recent());
    }
  }

  if (MilliTicker) {
    if (skipper(dbg.raw, 1000, true)) { //see if we are occasionally skipping milliseconds
      dbg("Absolute time:", MilliTicker);
    }
  }

  sui([](char key) {
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
