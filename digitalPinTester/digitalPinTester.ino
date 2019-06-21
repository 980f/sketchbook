
#include "chainprinter.h"
ChainPrinter dbg(Serial);
//
//
//#define DebugDigitalPin Serial
#include "digitalpin.h"
//DigitalOutput led(LED_BUILTIN);

#include "cheaptricks.h" 

//#include "millievent.h"
//Using_MilliTicker
//
//BiStable toggler(250, 550);


bool butevent = 0;

void setup() {
	
  Serial.begin(115200);
  while (!Serial);
  //everything needed is in constructors.
  //  dbg(
  Serial.println(
    "\nin Setup");
  delay(2000);
  
	 pinMode(5, INPUT_PULLUP);
//  DigitalInput button(8);

  butevent = digitalRead(D5)==HIGH;//button;
  
  Serial.println(
    "\nDone");
  delay(2000);
  
}


unsigned loopcount = 0;

void loop() {
//  DigitalInput button(8);
	 
  if (changed(butevent, digitalRead(D5)==HIGH)) {
    Serial.print(butevent ? '+' : '-');
    Serial.print(millis());
  }
  //	if(loopcount++ % 5000 == 0){
  //		  Serial.println(loopcount);
  //	}
  //  if (MilliTicked) {
  //    if (MilliTicked.every(1000)) {
  //      bool bee = MilliTicked.every(2000); //button;
  //      Serial.print(bee ? 'K' : 'k');
  //    }
  ////    if (toggler.perCycle()) {
  ////      //    	Serial.println(loopcount);
  ////      Serial.print("Toggle:");
  ////      bool phase = 1;//bool(toggler); //AVR compiler is so old that we have to make this type cast explicit, it won't recognize operator bool() and use it.
  ////      Serial.print(phase ? '+' : '-');
  ////      //      //   	 	dbg("\ntoggle", MilliTicked.recent());
  ////      //      led = phase;
  ////    }
  //  }

}
