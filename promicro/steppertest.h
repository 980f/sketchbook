
#include "stepper.h"
Stepper positioner;

unsigned thespeed = 60000;
unsigned speedstep = 100;
int spinit = 0;
unsigned blips = 0;

void stepperSetup(){
  positioner.iface = Stepper::Iface::Disk3;
  board.T1.cs = ProMicro::T1Control::By1;
  board.T1.setDivider(thespeed);
}

ISR(TIMER1_COMPA_vect) { //timer1 interrupt at step rate
  ++blips;
  positioner += spinit;
}


void upspeed(unsigned newspeed) {
  if (changed(thespeed, newspeed)) {
    board.T1.clip(thespeed);
    board.T1.setDivider(thespeed);
    dbg("\nSpeed:",thespeed);
  }
}

/** keytroke commands for debugging stepper motors */
bool stepCLI(char key) {
  switch (key) {
    case ' ':
      dbg("\nSpeed:",thespeed," \nLocation:",positioner.step,"\nBlips:",blips);

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
