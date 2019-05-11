#include <ESP8266WiFi.h>
#include <Ticker.h>
Ticker steprate;

#include "cheaptricks.h"

#include "stepper.h"
Stepper positioner;

#include "chainprinter.h"
ChainPrinter dbg(Serial);

//ms per step
unsigned thespeed = 250;
//user step size for speed tuning
unsigned speedstep = 100;
//spin enable and direction
int spinit = 0;

OutputPin<2> led;

void step(){	
  positioner += spinit;
	led=(positioner.step&3) == 3;
}


void upspeed(unsigned newspeed) {
  if (changed(thespeed, newspeed)) {
		steprate.attach_ms(thespeed,step);
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
    case 'q':
      positioner.step &= 3; //closest to 0 we can get while the phases are still tied to position.
      break;
    case 'w':
      ++positioner;
//      dbg.println(positioner.step);
      break;
    case 'e':
      --positioner;
//      dbg.println(positioner.step);
      break;
    case 'x':
      spinit = 0;
      break;
    case 'r':
      spinit = -1;
      break;
    case 'f':
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
}

void loop() {
  // put your main code here, to run repeatedly:

}
