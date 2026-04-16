#pragma once

#include "cstr.h"

// todo: see if we have a more complete class somewhere, such as in FastLED.
struct Color {
  unsigned r;
  unsigned g;
  unsigned b;

  void apply(char color, unsigned rawvalue) {
    switch (color) {
      case 'r':
        r = rawvalue;
        break;
      case 'g':
        g = rawvalue;
        break;
      case 'b':
        b = rawvalue;
        break;
    }
  }  
};


/** wrapper class for ledc C functions 
//bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
//void ledcWrite(uint8_t pin, uint32_t duty);
*/
class LEDC {
  //store pin so that only the constructor needs to know which pin.
  uint8_t pin;
  bool ok;
  
  public:
  

  LEDC &configure(uint32_t freq, uint8_t resolution){
    ok = ledcAttach(pin,freq,resolution);
    return *this;
  }

  bool operator ()(uint32_t duty){
    if(ok){ //do not even attempt the ledcWrite unless ledcAttach was called successfully. (reduces error spew when library debug is enabled)
      ok = ledcWrite(pin, duty);
    }
    return ok;
  }

  LEDC(unsigned gpioPin):pin(gpioPin){}

  //todo: add operator *= to expedite fading and such.

};


/** access to the hardware */
#include "ESP32Servo.h"
struct RGB_C3 {
  // todo: pick 3 pins
  Color desired; // retain applied value for debug, and as a convenience for manipulation

  LEDC driver[3];

  static const Cstr map("rgb");

  unsigned indexFor(char rgb){
    return map.index(rgb);//perhaps prophylactic tolower?
  }

  RGB_C3(unsigned red, unsigned green, unsigned blue){

  }

  void apply(Color &desired) {
    // todo: send 'desired' values to hardware.
    this->desired = desired;
  }

  void apply(char color, unsigned rawvalue) {
    desired.apply(color,rawvalue);
  }

  void refresh(){
    apply(desired);
  }

};
