#include <ESP8266WiFi.h>

#include "cheaptricks.h"

#include "stepper.h"

#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);




//soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"

class ClockHand {
    Stepper mechanism;
    //ms per step
    unsigned thespeed = ~0U;

    bool enabled = false;
    int target = ~0U;

    void setTarget(int target) {
      if (changed(this->target, target)) {
        enabled = target != mechanism;
      }

    }

    MonoStable ticker;//start up dead

    void onTick() {
      if (enabled && ticker.perCycle()) {
        mechanism += (target - mechanism);
        if (target == mechanism) {
          enabled = 0;
        }
      }
    }

    void upspeed(unsigned newspeed) {
      if (changed(thespeed, newspeed)) {
        ticker.set(thespeed);//this one will stretch a cycle in progress.
        dbg("\nSpeed:", thespeed);
      }
    }

    ClockHand(Stepper::Interface interface) {
      mechanism.interface = interface;
    }
}

byte drive_2coil(unsigned step) {
  static const byte grey4[] = {0b0101, 0b0110, 0b1010, 0b0110};//use by two coil steppers. H-brdige drivers may select just two of the bits.
  return grey4[step & 3];
}

#include "pcf8575.h"  //reduced accessed but simplified use
PCF8575 bits; //We need wemos D1 I2C and that leaves us one pin short to direct drive two steppers. 



ClockHand minuteHand;
ClockHand hourHand;

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
          case 'h'://go to position
            dbg("\nHour going to:", cmd.arg);
            hourHand.setTarget(cmd.arg);//todo scale to 12 hours!
            break;

          case 'm'://go to position
            dbg("\nMinute going to:", cmd.arg);
            minuteHand.setTarget(cmd.arg);//todo scale to 60 minutes!
            break;

          case 'v'://set stepping rate to use
            dbg("\nSetting step:", cmd.arg);
            hourHand.upspeed(cmd.arg);
            minuteHand.upspeed(cmd.arg);
            break;

          case 'x'://stop stepping
            dbg("\nStopping.");
            hourHand.enabled = 0;
            minuteHand.enabled = 0;
            break;

					case 'i';
          //          case 'r'://free run in reverse
          //            dbg("\nRun Reverse.");
          //            minuteHand.setTarget(0);
          //            break;
          //          case 'f'://run forward
          //            dbg("\nRun Forward.");
          //            spinit = 1;
          //            target = mechanism - 1;
          //            break;
          default:
            dbg("\nIgnored:", key, cmd.arg, cmd.pushed);
            break;
        }
      }//end command
      minuteHand.onTick();
      hourHand.onTick();
    }
  }
}
