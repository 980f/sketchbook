#include <Arduino.h> //needed by some IDE's.

#include "options.h"
/* test stepper motors.
    started with code from a clock project, hence some of the peculiar naming and the use of time instead of degrees.

    the test driver initially is an adafruit adalogger, it is what I had wired up for the clock.
    the test hardware has a unipolar/bipolar select on D17
    and power on/off D18 (need to learn the polarity )

    it is convenient that both DRV8333 and L298n take the same control input, and that pattern is such that a simple connect of the center taps to the power supply makes them operate as unipolar.

    with fixed power supply voltage the unipolar mode is twice the voltage on half the coil.
    the uni current is double, but only half as many coils are energized.

*/
////////////////////////////////////
#include "pinclass.h"

#ifdef LED_BUILTIN
OutputPin<LED_BUILTIN> led;
#else
bool led;
#endif

#include "cheaptricks.h"

#include "easyconsole.h"
EasyConsole<decltype(Serial4Debug)> dbg(Serial4Debug, true /*autofeed*/);

////soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"

//using microsecond soft timer to get more refined speeds.
#include "microevent.h"

#include "stepper.h" //should probably merge this into steppermotor.h
#include "motordrivers.h" //FourBanger and similar direct drive units.

#if SEEEDV1_2==1
//pinout is not our choice
FourBanger<8, 11, 12, 13> L298;
bool unipolar;//else bipolar
DuplicateOutput<9, 10> motorpower; //pwm OK. These are the ENA and ENB of the L298 and are PWM

void L298lambda(byte phase) {
  L298(phase);
}

#elif UsingSpibridges==1
#include "spibridges.h"  //consumes 3..12

//14..17 are physically hard to get to on leonardo.
InputPin<18, LOW> zeroMarker;
InputPin<19, LOW> oneMarker;
//2 still available, perhaps 4 more from the A group.

//compiler version too old to do this inline, and nasty to type:
template<bool second> void bridgeLambda(byte phase) {
  SpiDualBridgeBoard::setBridge(second, phase);
}

#endif


unsigned StepsPerRev = 200;

///////////////////////////////////////////////////////////////////////////
#include "steppermotor.h"
StepperMotor motor[2];//setup will attach each to pins/

//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP<MicroStable::Tick> cmd;//need to accept speeds, both timer families use 32 bit values.

//I2C diagnostic
#include "scani2c.h"

void doKey(char key) {
  Char k(key);
  bool which = k.isLower();//which of two motors
  if (which) {
    k.raw -= ' '; //crass way to do a 'toupper'
  }

  switch (k) {
    case ' '://report status
      motor[0].stats(&dbg);
      motor[1].stats(&dbg);
      break;

    case 'M'://go to position  [speed,]position M
      motor[which].moveto(take(cmd.arg), take(cmd.pushed));
      break;

    case 'N'://go to negative position (present cmd processor doesn't do signed numbers, this is first instance of needing a sign )
      motor[which].moveto(-take(cmd.arg), take(cmd.pushed));
      break;

    case 'H'://takes advantage of 0 not being a viable value for these optional parameters
      if (cmd.pushed) {//if 2 args width,speed
        motor[which].homeWidth = take(cmd.pushed);
        dbg("set home width to ", motor[which].homeWidth);
      }
      if (cmd.arg) {//if 1 arg speed.
        motor[which].homeSpeed = take(cmd.arg);
        dbg("set home speed to ", motor[which].homeSpeed);
      }
      motor[which].homing = StepperMotor::NotHomed;//will lock up if no sensor present or is broken.
      dbg("starting home procedure at stage ", motor[which].homing);
      break;

    case 'K':
      motor[which].target -= 1;
      break;
    case 'J':
      motor[which].target += 1;
      break;

    case 'Z'://declare present position is ref
      dbg("marking start");
      motor[which].pos = 0;
      break;

    case 'S'://set stepping rate to use for slewing
      dbg("Setting slew:", cmd.arg);
      motor[which].setTick(cmd.arg);
      break;

    case 'X': //stop stepping
      dbg("Stopping.");
      motor[which].freeze();
      break;

    //one test system had two relays for switching the motor wiring:
    //  case 'U':
    //    dbg("unipolar engaged");
    //    unipolar = true;
    //    break;
    //  case 'B':
    //    dbg("bipolar engaged");
    //    unipolar = false;
    //    break;

    case 'P':
      dbg("power on");
#if UsingSpibridges==1
      SpiDualBridgeBoard::power(which, true);

#endif
#if SEEEDV1_2==1
      motorpower = 1;
#endif

      break;

    case 'O':
      dbg("power off");
#if UsingSpibridges==1
      SpiDualBridgeBoard::power(which, false);

#endif
#if SEEEDV1_2==1
      motorpower = 0;
#endif

      break;

    case 'R'://free run in reverse
      dbg("Run Reverse.");
      motor[which].run = -1;
      motor[which].freeRun = true;
      break;

    case 'F'://run forward
      dbg("Run Forward.");
      motor[which].run = +1;
      motor[which].freeRun = true;
      break;

    case 'G'://whatever
      dbg("inputs:", motor[0].edgy, ",", motor[1].edgy);
      break;

    case '#':
      switch (cmd.arg) {
        case 42://todo: save initstring to eeprom
          dbg("save initstring to eeprom not yet implemented");//this message reserves bytes to do so :)
          break;
        case 6://todo: read initstring from eeprom
          dbg("read initstring from eeprom not yet implemented");//this message reserves bytes to do so :)
          break;
        default:
          dbg("# takes 42 to burn eeprom, 6 to reload from it, else ignored");
          break;
      }
      break;
    case '?':
      scanI2C(dbg);
#if UsingEDSir
      dbg("IR device is ", IRRX.isPresent() ? "" : "not", " present");
#endif
      break;

    default:
      dbg("Ignored:", char(key), " (", key, ") ", cmd.arg, ',', cmd.pushed);
      //      return; //don't put in trace buffer
  }
}

//keep separate, we will feed from eeprom as init technique
void accept(char key) {
  if (cmd.doKey(key)) {
    doKey(key);
  }
}

void doui() {
  while (auto key = dbg.getKey()) {
    accept(key);
  }
}

void doString(const char *initstring) {
  while (char c = *initstring++) {
    accept(c);
  }
}

/////////////////////////////////////

void setup() {
  Serial.begin(115200);
#if UsingSpibridges==1
  dbg("setup");
  SpiDualBridgeBoard::start(true);//using true during development, low power until program is ready to run

  motor[0].start(0, bridgeLambda<0>, &zeroMarker);//uppercase
  motor[1].start(1, bridgeLambda<1>, nullptr);//lower case
#endif

#if SEEEDV1_2==1
  motor[1].start(0, L298lambda, nullptr);
#endif

  if (StepsPerRev == 200) {//marker for testing big motor with L298 coz my new dual didn't come in when it was supposed to.
    doString("xmz3600spr");
  } else {
    //24 SPR/ 15 degree
    doString("15,250000h");
    doString("XMZ");
  }


}


void loop() {

  if (MicroTicked) {//on avr this is always true, cortexm0, maybe 50%
    motor[0]();
    motor[1]();
  }
  if (MilliTicked) {//non urgent things like debouncing index sensor
    doui();
  }
}
