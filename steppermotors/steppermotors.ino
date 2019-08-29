#include <Arduino.h> //needed by some IDE's.

#include "options.h"
/* drive stepper motors.
 *  one or two depending upon jumper, L298 for single, 2 with L293 common arduino shields.
 *  partially implemented: chaining to another leonardo for a second motor when local only has 1. 
 *  ... to finish the above I need to suppress all local action for the 2nd motor, especially sending feedback to host.
 *  

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

#ifdef SerialRing
Stream *ring = &SerialRing;
#else
Stream *ring = nullptr;
#endif

////soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"

//using microsecond soft timer to get more refined speeds.
#include "microevent.h"

#include "stepper.h" //should probably merge this into steppermotor.h
#include "motordrivers.h" //FourBanger and similar direct drive units.

#include "eeprinter.h" //presently only works for AVR  

#if SEEEDV1_2 == 1
//pinout is not our choice
FourBanger<8, 11, 12, 13> L298;
bool unipolar;//else bipolar
DuplicateOutput<9, 10> motorpower; //pwm OK. These are the ENA and ENB of the L298 and are PWM

void L298lambda(byte phase) {
  L298(phase);
}
#endif

#if UsingSpibridges == 1
#include "spibridges.h"  //consumes 3..12
SpiDualPowerBit p[2] = {0, 1};

//14..17 are physically hard to get to on leonardo.
InputPin<18, LOW> zeroMarker;
InputPin<19, LOW> oneMarker;
//2 still available, perhaps 4 more from the A group.

//compiler version too old to do this inline, and nasty to type:
template<bool second> void bridgeLambda(byte phase) {
  SpiDualBridgeBoard::setBridge(second, phase);
}

#endif

/** no jump== single controller, with jumper dual. This polarity was chosen by which board had jumperable pins handy. */
InputPin<23> UseSeeed;

///////////////////////////////////////////////////////////////////////////
#include "steppermotor.h"

StepperMotor motor[2];//setup will attach each to pins/

//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP<MicroStable::Tick> cmd;//need to accept speeds, both timer families use 32 bit values.

//I2C diagnostic
#include "scani2c.h"

void initread(bool forrealz) {
  dbg("Initstring:");
  unsigned addr = 0;
  char key = EEPROM.read(addr++);
  if (key == '#') {
    auto x = dbg.nofeeds();
    while (key = EEPROM.read(addr++) && addr < 64) { //guard  against  no nulls in blank eeprom.
      dbg(key);
      if (forrealz) {
        accept(key);
      }
    }
  } else {
    dbg("not present or corrupt header");
  }
}

void initwriter(ChainPrinter &writer) {
  writer("#");
  for (auto sm : motor) {
    if (sm.homeSensor != nullptr) { //if motor has a home
      writer(sm.homeWidth, ',', sm.homeSpeed, sm.which ? 'h' : 'H'); //todo: case matters!
    } else { //wake up running at speed it was going when burn occurred
      writer(sm.which ? "xmz" : "XMZ", sm.ticker.duration, sm.which ? "sp" : "SP", sm.run > 0 ? sm.which ? 'f' : 'F' : sm.run < 0 ? sm.which ? 'r' : 'R' : sm.which ? 'x' : 'X'); //definitely regretting using case. it has a virtue-no UI state.
    }
  }
  writer(char(0));//null terminator for readback loops
}

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
      motor[which].power(1);
      break;

    case 'O':
      dbg("power off");
      motor[which].power(0);
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
        case 666: {// save initstring to eeprom
#if HaveEEPrinter
            EEPrinter eep(0);
            ChainPrinter writer(eep);
#else
            //todo: I2C eeprom as Print
#endif
            initwriter(writer);
          }
          break;
        case 123:
          initwriter(dbg);
          break;
        case 0:
          initread(false);
          break;
        case 42:  //execute initstring from eeprom
          initread(true);
          break;
        default:
          dbg("# takes 666 to burn eeprom, 42 to reload from it, 0 show it");
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
      if (key == 255) {
        //some jerk is sending this when the shift key is hit all by its lonesome. Looking at you arduino serial monitor!
      }
      dbg("Ignored:", char(key), " (", unsigned(key), ") ", cmd.arg, ',', cmd.pushed);
  }
}

//keep separate, we will feed from eeprom as init technique
void accept(char key) {
  if (cmd.doKey(key)) {
    doKey(key);
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
  dbg("setup");

  if (UseSeeed) {
    motor[0].start(0, [](byte phase) {}, nullptr, nullptr);
    motor[1].start(1, L298lambda, nullptr, &motorpower);
  } else {
    SpiDualBridgeBoard::start(true);//using true during development, low power until program is ready to run

    motor[0].start(0, bridgeLambda<0>, &zeroMarker, &p[0]); //uppercase
    motor[1].start(1, bridgeLambda<1>, nullptr, &p[1]); //lower case
  }

  doString("6#");
  //      doString("xmz3600spr");
  //  } else {
  //    //24 SPR/ 15 degree
  //    doString("15,250000h"  "XMZ");
  //  }
}

void loop() {

  if (MicroTicked) {//on avr this is always true, cortexm0, maybe 50%
    motor[0]();
    motor[1]();
  }
  if (MilliTicked) {//non urgent things like debouncing index sensor
    while (char key = dbg.getKey()) {
      accept(key);
			if(ring){
				ring->write(key);
			}
    }
    if(ring){//todo: locally buffer until newline seen then send whole line OR implement DC1/DC3 for second channel and wrap this chunk of copying.
      for(unsigned qty=ring->available();qty-->0;){
      	dbg.conn.write(ring->read());
      }
    }
  }
}
