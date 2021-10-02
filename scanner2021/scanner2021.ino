

#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

//disappeared from libs: #include "cheaptricks.h"
//we will want to delay some activities, such as changing motor direction.
#include "millievent.h"

#include "digitalpin.h"

//switches on leadscrew/rail: 
DigitalInput nearend(2, LOW);
DigitalInput farend(3, LOW);


#include "edgyinput.h"
EdgyInput homesense(farend);
EdgyInput awaysense(nearend);

//2 for the motor
DigitalOutput scan(18);
DigitalOutput away(17);
//lights are separate so that we can force them on as work lights, and test them without motion.
DigitalOutput lights(16);
//not yet sure what else we will do
DigitalOutput other(19);

//////////////////////////////////////////////////
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
  Serial.begin(115200);
}

void loop() {
  
  if(MilliTicked){//nothing need be fast and the processor may be in a tight enclosure
  //todo: if scanpass is not zero and we are not moving start moving
  //todo: if scanpass is not zero and we are moving then if related timer done reverse or stop
  //todo: other timers may expire and here is where we service them  
    if(homesense.changed()){
      dbg(homesense?'H':'h');
    }
    if(awaysense.changed()){
      dbg(awaysense?'F':'f');
    }
   
  }
  //todo: if serial do some test thing
  if(Serial){
    auto key= Serial.read();
    switch(key){
      case 'f': case 'F':
        away=0;
        break;
      case 'b':case 'B':
        away = 1;
        break;
      case 'r':case 'R':
        scan = 1;
        break;
      case 'l':case 'L':
        lights = 1;
        break;
      case 'o':case 'O':
        lights = 0;
        break;
      case 'q':case 'Q':
        other = 1;
        break;
      case 'a':case 'A':
        other = 0;
        break;  
      //////////////////////////////////////  
      default: //any unknown key == panic
        scan=0;
        away=0;
        lights=0;
        break;
        
    }
  }
}
