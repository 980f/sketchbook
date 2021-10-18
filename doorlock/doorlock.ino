/**
  Drives bi-directional motor based on two different types of input.

	One is a fwd/back switch, which is acted upon when it changes.
	The other is a momentary which toggles the state of the system when pressed.

	todo: implement fallback to holding value after a time.
  todo: add checking jumpers for pulse time.




*/


#include "edgyinput.h"
#include "motorshield.h"

#include "millievent.h"

//this is state input, not toggle, for when you can't see the mechanism
const DigitalInput stableswitch(7, LOW);
//this is toggle, used when you can see the mechanism
const DigitalInput momentary(6, LOW);

//debounce is good, we don't want to rapidly dick with the mechanism, although the first use could get away with that.
EdgyInput stable(stableswitch, 15);
EdgyInput toggler(momentary, 30);


using Moveit =   L298Bridge::Code ; // 'Code' was too generic, L298Brdige might get replaced with L293 in a more generic build

struct PulsedMotor {
  SeeedStudioMotorShield driver;

  //after some time the drive level is dropped, either by going to hold state or disabling (todo: configuration flag to indicate which should be used vs recompiling code)
  MonoStable holder;

  Moveit lastCommand;

  void begin(MilliTick backoff) {
    *this =  stable.raw() ? Moveit::Forward : Moveit::Backward;
    holder = backoff; //which also starts it
  }

  void operator =(Moveit newstate) {
    lastCommand = newstate;
    driver = newstate;
    //    driver.one.enable = newstate != Off;
    holder.start();
  }

  /** if toggleit then if last issued command was a direction go in other directio, else if off go to hold */
  void toggleif (bool toggleit) {
    if (toggleit) {
      if (lastCommand != Moveit::Off) { //if off leave off, do not go to hold
        *this = 3 - lastCommand; //reverse direction or if held then off.
      }
    }
  }

  void onTick() {
    if (holder.hasFinished()) {
      driver = Moveit::Off;
    }
  }
};

PulsedMotor driver;

void setup() {
  stable.begin();
  toggler.begin();
  driver.begin(300);//todo: parameter at top of file or EEProm
}


void loop() {
  if (MilliTicked) {
    if (toggler.onTick()) {
      driver .toggleif( toggler);
      stable.onTick();//debounce it even if overriding/ignoring.
    } else { //only check stable
      if (stable.onTick()) {
        driver = stable ? Moveit::Forward : Moveit::Backward; //if this is backwards swap the wires to the device :)
      }
    }

  }
}

//end of file.
