#include <Arduino.h> //needed by some IDE's.


#include "options.h"
/* drive stepper motors.
    broken: one or two depending upon jumper, L298 for single, 2 with L293 common arduino shields.
    ... the overlapping pin allocations mess with each other, we're back to compiled in motor selection until we 'new' our selection.
    partially implemented: chaining to another leonardo for a second motor when local only has 1.
    ... to finish the above I need to suppress all local action for the 2nd motor, especially sending feedback to host.

*/
////////////////////////////////////
#include "pinclass.h"

#ifdef LED_BUILTIN
OutputPin<LED_BUILTIN> led;
#else
bool led;
#endif

#include "cheaptricks.h"
#include "bitbanger.h"
#include "easyconsole.h"
EasyConsole<decltype(Serial4Debug)> dbg(Serial4Debug, true /*autofeed*/);

//set up daisy chain on serial in case we need to slave a board to get more I/O.
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

/////////////////////////////
#include "steppermotor.h"

StepperMotor motor[2];//setup will attach each to pins/

#if UsingSpibridges
#include "spibridges.h"  //consumes 3..12, which precludes using standard I2C port.
SpiDualPowerBit p[2] = {0, 1};

//14..17 are physically hard to get to on leonardo.
InputPin<18, LOW> zeroMarker;
InputPin<19, LOW> oneMarker;
//2 still available, perhaps 4 more from the A group.

//compiler version was too old to do this inline, and for later ones is too nasty to type:
template<bool second> void bridgeLambda(byte phase) {
  SpiDualBridgeBoard::setBridge(second, phase);
}


void engageMotor() {
  SpiDualBridgeBoard::start(true);//using true during development, low power until program is ready to run
//couldn't get sensor working in time for the show.S
  motor[0].start(0, bridgeLambda<0>, nullptr/*&zeroMarker*/, &p[0]); //uppercase
  motor[1].start(1, bridgeLambda<1>, nullptr, &p[1]); //lower case
}

#elif defined(UsingL298)

#if UsingL298 == 1  //seeed on leonardp
//pinout is not our choice
FourBanger<8, 11, 12, 13> L298;
DuplicateOutput<9, 10> motorpower; //pwm OK. These are the ENA and ENB of the L298 and are PWM

#elif UsingL298 == 2  //promicro+ standalone
//pinout is our choice, but 11,12,13 weren't on connectors so we chose ones that were instead of mimicing seeed
FourBanger<5, 6, 7, 8> L298;
DuplicateOutput<9, 10> motorpower; //pwm OK. These are the ENA and ENB of the L298 and are PWM

#elif UsingL298 == 3  //I2C via 8574
#include "pcf8574.h"

/**
  byte I2C expander attached to L298
  M1: 0,1 legs, 2 power,
  M2: 3,4 legs, 5 power , 6,7 for home sensor

  Note 3,4 are probably swapped compared to other implementors of this interface, motor might go in reverse direction when switching from one to the other.
  since you are already changing wires to change controller you can swap the motor leads if this is a problem.
*/
class I2CL298 {
    PCF8574 dev;

  public:
    I2CL298(uint8_t which): dev(which) {
      dev.setInput(bits(6, 7));
    }

    /** matching FourBanger interface as best we can */
    void operator()(byte phase) {
      byte pattern = 0;
      //somewhat obtuse, but allows matching code to the comment
      pattern |= bit(0) << greylsb(phase);//set one of two bits, other left at zero
      pattern |= bit(3) << greymsb(phase);//... coz that is how this chip works.

      dev.merge(pattern, bits(0, 1, 3, 4));
    }

    class PowerWidget: public BoolishRef {
        I2CL298 &parent;

      public:
        PowerWidget(I2CL298 &parent): parent(parent) {}
        operator bool() const {
          return bitFrom(parent.dev.cachedBits() , 2); //trust that 2 and 5 are the same
        }
        bool operator =(bool on) const {
          parent.dev.merge(on ? bits(2, 5) : 0, bits(2, 5));
          return on;
        }
    };

};

I2CL298 L298(0);
I2CL298::PowerWidget  motorpower(L298);
#else
#error "you must define a motor interface, see options.h"
#endif


void L298lambda(byte phase) {
  L298(phase);
}

void engageMotor() {
  motor[0].start(0, nullptr, nullptr, nullptr);
  motor[1].start(1, L298lambda, nullptr, &motorpower);
}

#endif
//
///** no jumper == single controller, with jumper dual. This polarity was chosen by which board had jumperable pins handy. */
//InputPin<23> UseSeeed;

///////////////////////////////////////////////////////////////////////////
//this doesn't work because the pin assignments are templated and conflict. We need to defer construction rather than just trying to select between two items.
//void selectDriver(bool seeed) {
//  if (seeed) {
//    dbg("L298");
//    motor[0].start(0, [](byte phase) {}, nullptr, nullptr);
//    motor[1].start(1, L298lambda, nullptr, &motorpower);
//  } else {//default/preferred
//    dbg("L293");
//    SpiDualBridgeBoard::start(true);//using true during development, low power until program is ready to run
//
//    motor[0].start(0, bridgeLambda<0>, &zeroMarker, &p[0]); //uppercase
//    motor[1].start(1, bridgeLambda<1>, nullptr, &p[1]); //lower case
//  }
//}

//////////////////////////////////////////////////////////////////////////

//I2C diagnostic
#include "scani2c.h"

//const char *preinit __attribute__((__section__(".eeprom"))) = "#12,34\\";

void initread(bool forrealz) {
  dbg("Initstring:");
  unsigned addr = 0;
  char key = EEPROM.read(addr);
  if (key == '#') {
    auto pop = dbg.stackFeeder(false);
    do {
      char key = EEPROM.read(addr);
      if (key == 0) {
        return;
      }
      dbg(char(key));
      if (forrealz) {
        accept(key);
      }
    } while (++addr < 35 ) ; //guard  against  no nulls in blank eeprom.
  } else {
    dbg("not present or corrupt header");
  }
}

/** write command strings that will restore the present configuration and some state. */
void initwriter(ChainPrinter &writer) {
  writer("#");//flag to mark the eeprom as having a block of init strings.

  for (auto sm : motor) {
    writer(sm.g.start, ',', sm.g.accel, sm.which ? 'g' : 'G');
    
//1st x deals with potential bad choices in default init of structures
      //the bare M is 'goto 0', Z says 'you are actually at 0' so there should be no motion afterwards.
      //setting S deals with potential bad choices in default init of speed logic, much of which depends upon present state
      //setting P powers up the motor now that its controls are initialized
      //the runcode is probably a very bad idea, it allows the system to come up running on power up. That may be useful someday for a normally on situation, but those are usually rare.
    writer(sm.which ? "xmz" : "XMZ", sm.g.cruise, sm.which ? "sp" : "SP");//, sm.runcode());
//    if (sm.homeSensor != nullptr) { //if motor has a home
//      writer(sm.h.width, ',', sm.h.rev, sm.which ? 'h' : 'H');
//    }
  }
  writer(char(0));//null terminator for readback loops
}


void initmanagement(unsigned arg) {
  switch (arg) {
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
}
///////////////////////////////////////////////////////
//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP<MicroStable::Tick> cmd;//need to accept speeds, both timer families use 32 bit values.

void test(decltype(cmd)::Value arg1, decltype(cmd)::Value arg2) {
  dbg("\ntest:", arg1, ',', arg2);
  switch (arg1) {
    case 0:
      switch (arg2) {
        case 0-3://# gcc extension but I didn't enable those, curious.
#if defined(UsingL298)
          dbg(F("setting L298 to:"), arg2);
          L298(arg2);
#endif
          break;
      }
      break;
    default:
      break;
  }
}



void doKey(char key) {
  Char k(key);
  bool which = k.toUpper();//which of two motors, whuch wilkl be true for LOWER case ()
  auto &m(motor[which]);

  switch (k) {
    case '\\':
      cmd(test);
      break;

    case ' '://report status
#ifndef OnlyMotor
      motor[0].stats(&dbg);
      motor[1].stats(&dbg);
#else
      motor[OnlyMotor].stats(&dbg);
#endif
      break;

    //    case '!': //select motor configuration
    //      selectDriver(cmd.arg);
    //      break;

    case 'M'://go to position  [speed,]position M
      m.moveto(cmd.arg, cmd.pushed);
      break;

    case 'N'://go to negative position (present cmd processor doesn't do signed numbers, this is first instance of needing a sign )
      m.moveto(-cmd.arg, cmd.pushed);
      break;

    case 'G': //acceleration configuration
      m.g.configure(cmd.arg, cmd.pushed);
      dbg("accel:", m.g.accel, " start:", m.g.start);
      break;

    case 'H'://load characteristics
      m.h.configure(cmd.arg, cmd.pushed);
      m.home();//no longer automatic on configuration change.
      dbg("starting home procedure at stage ", m.homing);
      break;

    case 'K':
      m.target -= 1;
      break;
    case 'J':
      m.target += 1;
      break;

    case 'Z'://declare present position is ref
      dbg("marking start");
      m.pos = 0;
      break;

    case 'S'://set stepping rate to use for slewing
      m.setCruise(cmd.arg);
      break;

    case 'X': //stop stepping
      dbg("Stopping.");
      m.freeze();
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
      m.power(1);
      break;

    case 'O':
      dbg("power off");
      m.power(0);
      break;

    case 'R'://free run in reverse
      dbg(F("Run Reverse."));
      m.Run(false);
      break;

    case 'F'://run forward
      dbg(F("Run Forward."));
      m.Run(true);
      break;

    case '#':
      cmd(initmanagement);
      break;
    case '?':
      scanI2C(dbg);
#if UsingEDSir
      dbg("IR device is ", IRRX.isPresent() ? "" : "not", " present");
#endif
      break;

    default:
      if (key == char(255)) {
        //some jerk is sending this when the shift key is hit all by its lonesome. Looking at you arduino serial monitor!
      }
      dbg("Ignored:", char(key), " (", unsigned(key), ") ", cmd.arg, ',', cmd.pushed);
  }
  dbg.endl();
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
#ifdef SerialRing
  SerialRing.begin(115200);
#endif
  //  selectDriver(UseSeeed);//jumper based config
  engageMotor();
  doString("250000,10000g");//todo: doesn't stick in eeprom
  doString("42#");// 42# is "execute eeprom"
}

void loop() {
  if (MicroTicked) {//on avr this is always true, cortexm0, maybe 50%
#ifndef OnlyMotor
    motor[0]();
    motor[1]();
#else
    motor[OnlyMotor]();
#endif
  }
  if (MilliTicked) {//non urgent things like debouncing index sensor
    while (char key = dbg.getKey()) {
      accept(key);
      if (ring) {
        ring->write(key);
      }
    }
    if (ring) { //todo: locally buffer until newline seen then send whole line OR implement DC1/DC3 for second channel and wrap this chunk of copying.
      for (unsigned qty = ring->available(); qty-- > 0;) {
        dbg.conn.write(ring->read());
      }
    }
  }
}
