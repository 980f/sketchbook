/*
  Blink without Delay

  This example code is in the public domain.

  https://www.arduino.cc/en/Tutorial/BuiltInExamples/BlinkWithoutDelay
*/


const unsigned ledPin =  LED_BUILTIN;// the number of the LED pin

using TimerTick = decltype(millis());

TimerTick step[]={100,200,300,400,500,600,700};
const unsigned numSteps = sizeof(step)/sizeof(step[0]);
unsigned int ledStep = 0;             
TimerTick stepDoneAt=0;

/** whether to show the number of times the LED has been toggled, with each toggle */
bool showCount = false;
unsigned count=0;
///////////////////////////////////////
#include "sui.h"
struct MySui: public SUI<> {
  using SUI::SUI; //incantation that is required to inherit base class constructors.

  bool handleKey(unsigned char cmd ,bool wasUpper){
    switch(tolower(cmd)){
    case '\r': case '\n': break;
    case '!':{
      unsigned which=cli[1];
      unsigned amount=cli[0];
      if(which<numSteps){
        Serial.printf("\nSetting step %d to %d",which,amount);
        step[which] = amount;     
      } else {
        Serial.printf("\nUnreasonable step number, can't set step %d to %d",which,amount);
      }
    } break;      
    case 'c':
      showCount = wasUpper;
      break;
    case ' ': //show program state
      Serial.printf("\nSteps:");
      for(unsigned i=0;i<numSteps;++i){
        Serial.printf("\n%d:%d",i,step[i]);
      }
      break;
    default: //unrecognized command stuff gets to here      
      Serial.printf("\nunknown command letter %c",wasUpper? toUpperCase(cmd) : cmd);
      break;
    }
    return true;//discard
  }
} sui(Serial,Serial);

TimerTick serialLive=0;//takes around a second for USB serial to start working.
TimerTick serialChecked=0;
///////////////////////////////////////
void setup() {
  // set the digital pin as output:
  pinMode(ledPin, OUTPUT);
  Serial.begin(115200);
  stepDoneAt = millis() + step[ledStep]; //make the first be the same as all of the rest.
}

void loop() {
  TimerTick currentMillis = millis();
  if (currentMillis >= stepDoneAt) {
    stepDoneAt = currentMillis + step[ledStep];
    if(++ledStep >= numSteps){
      ledStep = 0;
      ++count;
      if(showCount){
        Serial.println();
        Serial.print(count);
      }
    }
    digitalWrite(ledPin, ledStep & 1);   
  }
  if(!serialLive){
    if(serialChecked != currentMillis){
      serialChecked = currentMillis;
      if(Serial){ //reduce pressure on USB Serial code which takes a long time to check if it is working.
        serialLive = currentMillis;
        Serial.printf("\nSerial started at %u",serialLive);
      }
    }
  }
  if(serialLive){
    sui.loop();
  }
}
