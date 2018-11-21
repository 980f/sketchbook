
//arduino after many restarts suddenly discovered the library and imported all of its headers. Vicious!
#include "pinclass.h"
#include "digitalpin.h"
#include "millievent.h"

#include "cheaptricks.h"


//preceding following item with const generates spurious warnings, gcc 5.4 has a bug.
//const
//DigitalOutput relay1(7,LOW);//doesnt' work, always gets 0 at critical line of code. Really would prefer this for remote reconfiguration.
const OutputPin<7, LOW> relay1;
//low turns relay on
const OutputPin<9, LOW> relay2;
//hall effect sensor is low when magnet is present
const InputPin<10, LOW> hall;

const OutputPin<17, LOW> rxled;


/** pulse modulator, cycle is active:idle*/
class TogglerTimer {
    TickType zero = BadTick;
    TickType activeTime;
    TickType idleTime;
  public:
    /* call from MilliTicked event */
    operator bool() {
      TickType now = MilliTicked.recent();
      if (now > zero) {
        TickType elapsed = now - zero;
        TickType phase = elapsed % (activeTime + idleTime);     
        return phase < activeTime;
        //return ((now-zero) %(activeTime+idleTime))<activeTime;
      } else {
        //not running
        return false;
      }
    }
    
    void setPhases(TickType activeTime, TickType idleTime,bool andStart=true) {
      this->activeTime = activeTime;
      this->idleTime = idleTime;
      if(andStart){
        zero=MilliTicked.recent();
      }
    }

    void setCycle(TickType length, TickType active) {
      setPhases(active, (length > active) ? length - active : 0);
    }
};

TogglerTimer led;

MonoStable r1pulse(2345);

void setup() {
  relay1 = 0;
  relay2 = 0;
  led.setPhases(250, 750);
  Serial.begin(500000);//number doesn't matter.
}



// the loop function runs over and over again forever
void loop() {
  //update input pin as fast as loop() allows.
  rxled = hall; //digitalWrite(rxled,digitalRead(hall));

  if (MilliTicked) { //this is true once per millisecond.
    // causes gross delays, printing is blocking!   if(hall) Serial.println(milliEvent.recent());
    led? TXLED1 : TXLED0; //vendor macros, no assignment provided.
    if(r1pulse.isRunning()){
      relay1 = 1;
    } else if(r1pulse.isDone()){
      relay1 = 0;
      r1pulse.stop();//enables override
    }
  }
  
  if (Serial) {
    if (Serial.available()) {
      auto key = Serial.read();
      Serial.print(char(key));//echo.
      switch (key) {
        case 'r':
          r1pulse.start();
          break;
        case 't':
          relay1 = true;
          //        relay1.wtf(1);
          //        digitalWrite(relay1.number , relay1.polarity );
          break;
        case 'g':
          relay1 = 0;
          //        digitalWrite(relay1.number , DigitalPin::inverse(relay1.polarity ));
          break;
        case 'y':
          relay2 = 1; //digitalWrite(8, HIGH);
          break;
        case 'h':
          relay2 = 0; //digitalWrite(8, LOW);
          break;
        default:
          Serial.print("?\n");
          break;
        case '\n': case '\r':
          //ignore end of line used to flush letter commands.
          break;
      }
    }
  }
}
