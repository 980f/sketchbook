#include <ESP8266WiFi.h>

#include "cheaptricks.h"

#include "stepper.h"
Stepper positioner;

#include "chainprinter.h"
ChainPrinter dbg(Serial);

//soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"
MonoStable ticker;//start up dead

//ms per step
unsigned thespeed = 250;//4 steps per second, 50 Hz for a 200spr.
//user step size for speed tuning
unsigned speedstep = 100;
//spin enable and direction
int spinit = 0;

//OutputPin<2> led;

void step(){	
  positioner += spinit;
//	led=(positioner.step&3) == 3;
}


void upspeed(unsigned newspeed) {
  if (changed(thespeed, newspeed)) {
		ticker.set(thespeed);//this one will stretch a cycle in progress. 
    dbg("\nSpeed:",thespeed);
  }
}

/** keytroke commands for debugging stepper motors */
bool stepCLI(char key) {
  switch (key) {
    case ' ':
      dbg("\nSpeed:",thespeed," \nLocation:",positioner);
      break;

    case '1': case '2': case '3': case '4': //jump to phase
      positioner.applyPhase(key - 1);
      break;
    case 'q'://zero counter while retaining phase.
      positioner.step &= 3; //closest to 0 we can get while the phases are still tied to position.
      break;
    case 'w'://manual step forward
      ++positioner;
//      dbg.println(positioner.step);
      break;
    case 'e'://manual step back
      --positioner;
//      dbg.println(positioner.step);
      break;
    case 'x'://stop stepping
      spinit = 0;
      break;
    case 'r'://run in reverse
      spinit = -1;
      break;
    case 'f'://run forward
      spinit = 1;
      break;
    case 'u':
      speedstep -= 10;
      break;
    case 'j':
      speedstep += 10;
      break;
    case 'y':
      upspeed(thespeed + speedstep);
      break;
    case 'h':
      upspeed(thespeed - speedstep);
      break;
    default: return false;
  }
  return true;
}

void setup() {
	Serial.begin(115200);
	upspeed(thespeed);//start timer, but the load won't move unless we default spinit to non-zero.
}

void loop() {
	if(MilliTicked){
  	while(Serial.available()>0){//only checking every milli to save power
  		stepCLI(Serial.read());
  	}
  	if(ticker.perCycle()){
  		step();
  	}
	}

}
