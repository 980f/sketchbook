/**
  Drives bi-directional motor based on different types of input.

	One is a fwd/back switch, which is acted upon when it changes. It is also acted upon when processor is reset.
	Next is a momentary which toggles the state of the system when pressed.
  Third is a pair of momentaries, one for forward, one for reverse


	todo: implement eeprom config for fallback to holding value after a time.
  todo: implement eeprom config for debounce times
  todo: soft limit switches, one for each direction, closed == permission to move.

*/


#include "edgypin.h"    //980F debounced digital inputs
#include "motorshield.h" //the doorlock mechanism needs bipolar drive
using Moveit =   L298Bridge::Code ; // L298Bridge might get replaced with L293 in a more generic build

#include "millievent.h"  //we use explicit timers

//former comment was incorrect, these assignments are for ease of mechanical connections:
#ifdef ARDUINO_ESP8266_GENERIC
#define pulseopen 2
#define pulseclose 0
#else
#define pulseopen 5
#define pulseclose 4
#endif

//maximum motor run time, from last command. If you issue new commands while running it will continue to run.
MilliTick timeout(300);

struct DoorLocker {
  //we debounce all inputs as we don't want to rapidly dick with the mechanism

  //this is state input, not toggle, for when you can't see the mechanism. It works well with hardware limit switches in series between the arduino and the motor driver.
  //it is also the powerup state, it is checked at configuration time and executed.
  EdgyPin stable;//(7, LOW, 15);
  //this is toggle, used when you can see the mechanism and can press again if it is going in the wrong direction
  EdgyPin toggler;//(6, LOW, 30);
  //two buttons, safest approach whenever you can manage it.
  EdgyPin pulseopener;//(pulseopen, LOW, 30);
  EdgyPin pulsecloser;//(pulseclose, LOW,  30);

  //eventually we will add other driver options, here is where we will ifdef them

  struct PulsedMotor {
    SeeedStudioMotorShield driver; //from motorshield.h

    //after some time the drive level is dropped, either by going to hold state or disabling (todo: configuration flag to indicate which should be used vs recompiling code)
    MonoStable holder; //from millievent.h

    Moveit lastCommand; //from L298 included in motorshield.h

    //call this from setup, or when configuration is dynamically changed
    void begin(MilliTick backoff) {
      // *this =  stable.raw() ? Moveit::Forward : Moveit::Backward;
      holder = backoff; //which also starts it
    }

    //this is how you move
    void operator =(Moveit newstate) {
      lastCommand = newstate;
      driver = newstate;
      holder.start();
    }

    /** if @param toggleit then if last issued command was a direction go in other direction, else if off go to hold */
    void toggle (bool toggleit = true) {
      if (toggleit && lastCommand != Moveit::Off) { //if off leave off, do not go to hold
        *this = L298Bridge::reverseof(lastCommand);
      }
    }

    /** you must call this once every millisecond */
    void onTick() {
      if (holder) {//a MonoStable is true once when its time has elapsed
        driver = Moveit::Off;
      }
    }


  };//end PulsedMotor

  PulsedMotor driver;

  DoorLocker(MilliTick runtime = 300):
    stable(7, LOW, 15)
    , toggler(6, LOW, 30)
    , pulseopener(pulseopen, LOW, 30)
    , pulsecloser(pulseclose, LOW,  30)
  {
    driver.holder = runtime;
  }

  void setup() {
    stable.begin();
    toggler.begin();
    pulseopener.begin();
    pulsecloser.begin();

    driver.begin(timeout);
  }

  void onTick() {
    driver.onTick();//for delayed shutoff.
    //debounce all even if they will be overridden or ignored:
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

};//end Doorlocker

DoorLocker locker;

////////////////////////////////////
#include "sui.h" //Simple User Interface

SUI sui(Serial, Serial);// default serial for in and out, first use of file is on a Leonardo

void setup() {
  locker.setup();
}

void loop() {
  if (MilliTicker) {//true once per millisecond regardless of how often called
    locker.onTick();
  }

  sui([](char key) {//the sui will process input, and when a command is present will call the following code.
    bool upper = key < 'a';
    switch (toupper(key)) {
      default:
        dbg("ignored:", unsigned(sui), ',', key);
        break;
      case ' '://status dump 0 is off, 1 or 2 is direction, 3 is 'hold/brake'
        dbg(locker.driver.lastCommand, '\t', locker.driver.holder.due());
        break;
      case '[':
        locker.driver = Moveit::Forward;
        break;
      case ']':
        locker.driver = Moveit::Backward;
        break;
      case 'H': //holdoff time
        locker.driver.begin(timeout = sui);
        break;
        //todo: config debounce times
    }
  });
}

//end of file.
