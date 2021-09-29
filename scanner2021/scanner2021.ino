

#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

//disappeared from libs: #include "cheaptricks.h"
//we will want to delay some activities, such as changing motor direction.
#include "millievent.h"

#include "digitalpin.h"

//switches on leadscrew/rail: 
DigitalInput nearend(2, LOW);
DigitalInput farend(3, LOW);

//2 for the motor
DigitalOutput scan(18);
DigitalOutput away(17);
//lights are separate so that we can force them on as work lights, and test them without motion.
DigitalOutput lights(16);
//not yet sure what else we will do
DigitalOutput other(19);


//these should probably come from eeprom
bool idleNear;   //if true then idle near the motor, else idle at far end.
MilliTick outbound;//time from starting moving scanner bar away from motor to stopping.
MilliTick inbound;//time from starting moving scanner bar towards motor until turn around

//state of motion
bool haveHomed=0; //until we have activated home switch we don't know where we are
bool inMotion=0;   //run relay should be on
bool movingAway=0; //moving away from motor

//program states, above are hardware state
enum MotionState {
  NeedsHoming=0,
  HomeAway,
  HomeBack,
  AtHome,
  ScanAway,
  ScanTurnaround,
  ScanBack,
};

//set to non-zero when should be scanning. When decremented to zero motion stops once home.
unsigned scanpass=0;


void setup() {
  //todo: read options from EEprom 
}

void loop() {
  
  if(MilliTicked){//nothing need be fast and the processor may be in a tight enclosure
  //todo: if scanpass is not zero and we are not moving start moving
  //todo: if scanpass is not zero and we are moving then if related timer done reverse or stop
  //todo: other timers may expire and here is where we service them  
  
  }
  //todo: if serial do some test thing
}
