
//arduino after many restarts suddenly discovered the library and imported all of its headers. Vicious!
#include "pinclass.h"
#include "digitalpin.h"
#include "millievent.h"

#include "cheaptricks.h"


//preceding following item with const generates spurious warnings, gcc 5.4 has a bug. 
//const
//DigitalOutput relay1(7,LOW);
const OutputPin<7,LOW> relay1;
//low turns relay on
const OutputPin<9,LOW> relay2;
//hall effect sensor is low when magnet is present
const InputPin<10,LOW> hall;

const OutputPin<17,LOW> rxled;

#include "softpwm.h"

SoftPwm led;

void setup() {
  relay1=1;
  relay2=0;
  led.early=250;
  led.later=750;
 
  Serial.begin(500000);
}

// the loop function runs over and over again forever
void loop() {
  //update input pin as fast as loop() allows.
  rxled=hall;//digitalWrite(xx,digitalRead(xx));
  
  if(milliEvent){//this is true once per millisecond. 
//    if(hall) Serial.println(milliEvent.recent());
    led?TXLED1:TXLED0;//vendor macros, no assignment provided.  
  }
  if(Serial){
    if(Serial.available()){
      auto key=Serial.read();
      Serial.print(char(key));//echo.
      switch(key){
      case 't': 
        relay1=true;
//        relay1.wtf(1);
//        digitalWrite(relay1.number , relay1.polarity );
      break;
      case 'g':
        relay1=0;
//        digitalWrite(relay1.number , DigitalPin::inverse(relay1.polarity ));
      break;
      case 'y':
      relay2=1;//digitalWrite(8, HIGH);    
      break;
      case 'h':
      relay2=0;//digitalWrite(8, LOW);    
      break;
      default:
      
      Serial.print(" ignored.");      
      break;
      case '\n': case '\r':
        //ignore end of line used to flush letter commands.
      break;
      }
    }
  }
}
