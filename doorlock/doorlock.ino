/**
  Drives bi-directional motor based on two different types of input.

	One is a fwd/back switch, which is acted upon when it changes.
	The other is a momentary which toggles the state of the system when pressed.

	todo: implement fallback to holding value after a time.
  todo: add checking jumpers for pulse time.

*/


#include "edgypin.h"
#include "motorshield.h"

#include "millievent.h"


#ifdef ARDUINO_ESP8266_GENERIC
#define pulseopen 2
#define pulseclose 0
#else
#define pulseopen 5
#define pulseclose 4
#endif


//debounce is good, we don't want to rapidly dick with the mechanism, although the first use could get away with that.

//this is state input, not toggle, for when you can't see the mechanism
EdgyPin stable(7, LOW, 15);
//this is toggle, used when you can see the mechanism
EdgyPin toggler(6, LOW, 30);
//two buttons
EdgyPin pulseopener(pulseopen, LOW, 30);
EdgyPin pulsecloser(pulseclose, LOW,  30);


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
    holder.start();
  }

  /** if toggleit then if last issued command was a direction go in other direction, else if off go to hold */
  void toggle (bool toggleit = true) {
    if (toggleit && lastCommand != Moveit::Off) { //if off leave off, do not go to hold
      *this = L298Bridge::reverseof(lastCommand);
    }
  }


  void onTick() {
    if (holder) {
      driver = Moveit::Off;
    }
  }
};

PulsedMotor driver;

void setup() {
  stable.begin();
  toggler.begin();
  pulseopener.begin();
  pulsecloser.begin();

  driver.begin(300);//todo: parameter at top of file or EEProm
}


void loop() {
  if (MilliTicked) {
    //debounce all even if overriding/ignoring.
    auto togglerticked = toggler.onTick();
    auto stableticked = stable.onTick();
    auto openerticked = pulseopener.onTick();
    auto closerticked = pulsecloser.onTick();

    //priority scan, only honor one per millitick. Simultaneous means some may get ignored.
    if (togglerticked && toggler) { //tested first as the othes are idempotent, repeating them gets the same action.
      driver.toggle(toggler);
    } else if (openerticked && pulseopener) { //could drop the trailing term to fire on release as well as press
      driver = Moveit::Forward;
    } else if (closerticked && pulsecloser) {
      driver = Moveit::Backward;
    } else  if (stable.onTick()) {
      driver = stable ? Moveit::Forward : Moveit::Backward; //if this is backwards swap the wires to the device :)
    }
  }
}

//end of file.
