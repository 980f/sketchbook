
#include "chainprinter.h"
ChainPrinter dbg(Serial);
//
//
//#define DebugDigitalPin Serial
#include "digitalpin.h"


//what is our clue for whether Dxx symbols have been defined?
#ifndef D5
#define D5 5
#endif

DigitalInput button(4, LOW);

//#ifdef LED_BUILTIN
DigitalOutput led(5);
//#endif

#include "cheaptricks.h"

#include "millievent.h"
Using_MilliTicker

BiStable toggler(250, 550);


bool butevent = 0;

void setup() {

  Serial.begin(115200);
  while (!Serial);
#ifdef   ARDUINO_BOARD
  Serial.println(ARDUINO_BOARD);
#endif

#ifdef   ARDUINO_VARIANT
  Serial.println(ARDUINO_VARIANT);
#endif

  //everything needed is in constructors.
  //  dbg(
  Serial.println(
    "\nin Setup");
  delay(2000);

  //	 pinMode(D5, INPUT_PULLUP);
  Serial.println(
    "\nDone");
  delay(2000);

}


unsigned loopcount = 0;

void loop() {
  //works:  led = button;
  if (changed(butevent, button)) {
    dbg(butevent ? '+' : '-', millis());
  }
  //  	if(loopcount++ % 5000 == 0){
  //  		  Serial.println(loopcount);
  //  	}
  if (MilliTicked) {
    //      if (MilliTicked.every(1000)) {
    //        bool bee = MilliTicked.every(2000); //button;
    //        Serial.print(bee ? 'K' : 'k');
    //      }
    if (toggler.perCycle()) {
      //works:led.toggle();
    }
    //works:
    led = toggler;
  }

}
