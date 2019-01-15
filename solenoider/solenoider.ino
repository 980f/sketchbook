#include "pinclass.h"

#include "millievent.h"

#include "cheaptricks.h"
/** merge usb serial and serial1 streams.*/
#include "twinconsole.h"
TwinConsole Console;

#include "analog.h"
#include "linearmap.h"

#include "promicro.board.h"
ProMicro board;

//9,10 PWM, in addition to being run to LED's
const OutputPin<9, LOW> T9;
const OutputPin<10, LOW> T10;

/**
  want programmability from 30Hz to 100Hz
  base is 16MHz.
  16MHz/32Hz = 500000  (5E5)
  so divide by 8 prescale gets us 2MHz base rate.
  30.5Hz @65536 divider,

  We pulse the solenoid, its travel is modified by a spring of some sort.
  If we leave it full on the coil may melt.
  Every which way we can we turn it off when unsure of what we are doing.

*/

//if we only had C++14 these would be inside Solenoider:
static const ProMicro::T1Control::CS cs = ProMicro::T1Control::By8;
static const LinearMap dutyRange(6000, 0); //sets the limit
static const LinearMap percent(100, 0); //sets the limit

/** driving a solenoid */
struct Solenoider {
  bool primary;
  AnalogValue rate;
  AnalogValue duty;

  void begin(bool which) {
    primary = which;
    rate=~0U;//slow as possible
    duty=0;//minimal blips.
  }

  void setRate(AnalogValue av) {
    if (changed(rate, av)) {
      Console("\nRate=",percent(rate)); 
      board.T1.setPwmBase(av * 2, cs);
    }
  }

  void setDuty(AnalogValue av) {
    if (changed(duty, av)) {
      unsigned raw=dutyRange(av);
      Console("\nDuty=",percent(duty)," raw:",raw);
      if (primary) {
        board.T1.pwmA.setDuty(raw);
      } else {
        board.T1.pwmB.setDuty(raw);
      }
    }
  }
};

Solenoider plus;
AnalogInput rateControl(A3);
AnalogInput dutyControl(A2);

void setup() {
  plus.begin(0);
}

void loop() { 
  plus.setRate(rateControl);
  plus.setDuty(dutyControl);  
}
