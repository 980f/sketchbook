#pragma once

#include "cstr.h"



/** wrapper class for ledc C functions 

This module has the duty cycle always run from 0 to uint_max, which is conveniently ~0 for this processor family (and every processor designed since the late 1970's).
As such only the person allocating the pin needs to know what the hardware wants, and the hardware resolution can be changed (for frequency range reasons) without any other code being modifed.

We could also choose a signed integer and use the sign bit as an overflow/underflow indicator.

*/
class LEDC {
  //store pin so that only the constructor needs to know which pin.
  unsigned pin;
  unsigned shift; //recoded resolution
  bool ok;
  
  public:


  /** sets output duty cycle, passes @param duty back*/
  uint32_t operator =(uint32_t duty){
    if(ok){ //do not even attempt the ledcWrite unless ledcAttach was called successfully. (reduces error spew when library debug is enabled)
      ok = ledcWrite(pin, duty>>shift);
    }
    return duty;
  }

  LEDC &configure(uint32_t freq, uint8_t resolution){
    shift=32 - resolution; //if resolution is 1 then only msb of duty is to be used
    ok = ledcAttach(pin,freq,resolution);
    return *this;
  }

  LEDC &attach(unsigned pin){
    //todo: check that pin is realistic
    //todo: if already ok then detach
    this->pin=pin;
    return *this;
  }


  //default to detectably invalid pin number
  LEDC(unsigned gpioPin=~0):pin(gpioPin),shift(12),ok(false){}

  /////////////
  // the group shares a clock
  struct ClockSource {
    operator ledc_clk_cfg_t () const {
      return ledcGetClockSource();
    }
    const ClockSource& operator =(ledc_clk_cfg_t source) const {
      ledcSetClockSource(source);
      return *this;
    }
  };
  static const ClockSource clockSource;

};

struct Color {
  unsigned red;
  unsigned green; 
  unsigned blue;  

  static unsigned indexFor(char rgb){
    return Cstr{"rgb"}.index(rgb);//perhaps prophylactic tolower?
  }

  void apply(char letter,unsigned value){
    switch(letter){
      case 'r': red=value; break;
      case 'g': green=value; break;
      case 'b': blue=value ; break;
    }    
  }
};

/** access to the hardware */
#include "ESP32Servo.h"
struct RGB_C3 {
  // todo: pick 3 pins
  Color desired; // retain applied value for debug, and as a convenience for manipulation

  LEDC driver[3];

  
  RGB_C3(unsigned red, unsigned green, unsigned blue){
    //todo: attach the pins
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
