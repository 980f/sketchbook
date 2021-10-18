/**
  Looks at simple SPST switch and generates directional pulses for a motor driven locking pin.

  Might add checking jumpers for pulse time.




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

SeeedStudioMotorShield driver;

void setup() {
  stable.begin();
  toggler.begin();
  //driver = 0; //after a reset we need to do nothing until a human touches a switch
  //except since we have a stable switch we can sample it
  driver.one = stable.raw();
}


void loop() {
  if (MilliTicked) {
    if (toggler.onTick()) {
      if (toggler) {
        unsigned presently = driver.one;
        driver.one = 3 - presently; //toggles 1<->2
      }
      stable.onTick();//debounce it even if overriding.
    } else { //only check stable
      if (stable.onTick()) {
        driver.one = stable ? 2 : 1;//if this is backwards swap the wires to the device :)
      }
    }
  }
}

//end of file.
