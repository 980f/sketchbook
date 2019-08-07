
#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

#include "digitalpin.h"

DigitalInput button(4, LOW);

DigitalOutput a(18);
DigitalOutput b(17);
DigitalOutput c(16);

//you can make arrays:
const DigitalOutput nib[] = {{18}, {17}, {16}};
const DigitalInput quad[] = {{23} , {20}, {21}, {24}};
bool last[4];


class EdgyInput {
	bool last;
	const DigitalInput pin;
	public:
	EdgyInput(unsigned which):pin(which){
		last=this->operator bool();	
	}

	operator bool (){
		return bool(pin);
	}

	bool changed(){
		return ::changed(last,*this);
	}

};

#ifndef LED_BUILTIN
DigitalOutput led(LED_BUILTIN);
#else
bool led; //this is one of the advantages of the DigitalXX classes, you can replace with a boolean when there is no pin for the concept.
#endif


#include "cheaptricks.h"

#include "millievent.h"
Using_MilliTicker

BiStable toggler(250, 550);

bool butevent = 0;



void setup() {
	//the following doesn't wait if USB, but USB buffers data prior to host connection OR connects before setup.
  Serial.begin(115200);//can't use dbg here
  while (!Serial);//... or here.

//esp8266 build uses different build tokens, grrr.
#ifdef ARDUINO_BOARD
  dbg("ARDUINO_BOARD:", ARDUINO_BOARD);
#endif

#ifdef ARDUINO_VARIANT
  dbg("ARDUINO_VARIANT:",ARDUINO_VARIANT);
#endif

//samdD build tokens, ~1.8.10.
#ifdef ARDUINO
  dbg("ARDUINO:", ARDUINO);
#endif

#ifdef DARDUINO_ARCH_SAMD
  dbg("DARDUINO_ARCH_SAMD:", DARDUINO_ARCH_SAMD);
#endif

}

unsigned loopcount = 0;

EdgyInput ei(24);

void loop(){
//  //works:  led = button;
//  if (changed(butevent, button)) {
//    dbg(butevent ? '+' : '-', millis());
//  }
//  //  	if(loopcount++ % 5000 == 0){
//  //  		  Serial.println(loopcount);
//  //  	}

if (MilliTicked) {//slow down check to minimize worst of switch bounce.
  for (unsigned i = 3; i-- > 0;) {
    if (changed(last[i], quad[i])) {
      dbg( i + 1, " changed to ", last[i]);
      nib[i] = last[i];
    }
  }
  if(ei.changed()){
  	dbg("EiEi:",bool(ei));//ei doesn't have a native print interface (yet) so we have to cast it to one that does.
  }
}
  //    //      if (MilliTicked.every(1000)) {
  //    //        bool bee = MilliTicked.every(2000); //button;
  //    //        Serial.print(bee ? 'K' : 'k');
  //    //      }
  //    if (toggler.perCycle()) {
  //      //works:led.toggle();
  //    }
  //    //works:
  //    led = toggler;
}
