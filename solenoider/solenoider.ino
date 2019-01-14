#include "pinclass.h"

#include "millievent.h"

#include "cheaptricks.h"
/** merge usb serial and serial1 streams.*/
#include "twinconsole.h"
TwinConsole Console;


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

*/


static const ProMicro::T1Control::CS cs = ProMicro::T1Control::By8;


/** driving a solenoid */
struct Solenoider {
  void begin() {
    board.T1.setPwmBase(~0, cs);
  }
}

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}
