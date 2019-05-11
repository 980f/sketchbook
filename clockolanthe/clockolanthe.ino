#include <ESP8266WiFi.h>

#include "cheaptricks.h"

#include "stepper.h"
Stepper positioner;

#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);

//soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"
MonoStable ticker;//start up dead

//ms per step
unsigned thespeed = 250;//4 steps per second, 50 Hz for a 200spr.
//user step size for speed tuning
unsigned speedstep = 100;
//spin enable and direction
int spinit = 0;

unsigned target = ~0U;

void step() {
  positioner += spinit;
  if (target == positioner) {
    spinit = 0;
  }
}


void upspeed(unsigned newspeed) {
  if (changed(thespeed, newspeed)) {
    ticker.set(thespeed);//this one will stretch a cycle in progress.
    dbg("\nSpeed:", thespeed);
  }
}

const OutputPin<D4> xph0;
const OutputPin<D3> xph1;
const OutputPin<D2> xph2;
const OutputPin<D1> xph3;


/** keytroke commands for debugging stepper motors */
bool stepCLI(char key) {
  switch (key) {
    case ' ':
      dbg("\nSpeed:", thespeed, " \nLocation:", positioner, "\nPhases:", xph0, xph1, xph2, xph3);
      break;
    case 'i':
      upspeed(thespeed);//start timer, but the load won't move unless we default spinit to non-zero.
      dbg("\npinsymbols:", D1, D2, D3, D4);
      break;
    case '1': case '2': case '3': case '4': //jump to phase
      positioner.applyPhase(key - 1);
      dbg("\nPhases:", xph0, xph1, xph2, xph3);
      break;
    case 'q'://zero counter while retaining phase.
      positioner.step &= 3; //closest to 0 we can get while the phases are still tied to position.
      break;
    case 'w'://manual step forward
      ++positioner;
      dbg("\nPhases:", xph0, xph1, xph2, xph3);
      break;
    case 'e'://manual step back
      --positioner;
      dbg("\nPhases:", xph0, xph1, xph2, xph3);
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


/**
  Command Line Interpreter, Reverse Polish input


  If you have a 2-arg function
  then the prior arg is take(pushed)
*/
#include "unsignedrecognizer.h"  //recognize numbers but doesn't deal with +/-


class CLIRP {
    UnsignedRecognizer numberparser;
    //for 2 parameter commands, gets value from param.
  public://until we get template to work.
    unsigned arg = 0;
    unsigned pushed = 0;
  public:
    /** command processor */
    bool doKey(byte key) {
      if (key == 0) { //ignore nulls, might be used for line pacing.
        return false;
      }
      //test digits before ansi so that we can have a numerical parameter for those.
      if (numberparser(key)) { //part of a number, do no more
        return false;
      }
      arg = numberparser; //read and clear, regardless of whether used.

      switch (key) {//used: aAbcdDefFhHiIjlMmNnoprstwxyzZ  :@ *!,.   tab cr newline
        case '\t'://ignore tabs, makes param files easier to read.
          return false;
        case ','://push a parameter for 2 parameter commands.
          pushed = arg;//by not using take() here 1234,X will behave like 1234,1234X
          return false;
      }
      return true;//we did NOT handle it, you look at it.
    }

    template <typename Ret> Ret call(Ret (*fn)(unsigned, unsigned)) {
      (*fn)(take(pushed), arg);
    }

    template <typename Ret> Ret call(Ret (*fn)(unsigned)) {
      pushed = 0; //forget unused arg.
      (*fn)(arg);
    }

};

CLIRP cmd;
/////////////////////////////////////


void setup() {
  Serial.begin(115200);
  upspeed(10);//try 100Hz for mental math.
}

void loop() {
  if (MilliTicked) {
    while (auto key = dbg.getKey()) { //only checking every milli to save power
      if (cmd.doKey(key)) {
        switch (key) {
          case 'p'://go to position
            spinit = assigncmp(target,cmd.arg);//assigns cmd.arg to target, returns  -1,0, or 1.
            break;        
          case 'v'://set stepping rate to use
            upspeed(cmd.arg);
            break;
          case 'x'://stop stepping
            spinit = 0;
            break;
          case 'r'://run in reverse
            spinit = -1;
            target = positioner + 1;
            break;
          case 'f'://run forward
            spinit = 1;
            target = positioner - 1;
            break;
        }
      }
    }
    if (ticker.perCycle()) {
      step();
    }
  }

}
