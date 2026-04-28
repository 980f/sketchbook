#pragma once

#include "esp32-hal-ledc.h"
#include "cstr.h"


/** wrapper class for ledc C functions 
The device itself has more functionality than is exposed by this class.

This module has the duty cycle always run from 0 to uint_max, which is conveniently ~0 for this processor family (and every processor designed since the late 1970's).
As such only the person allocating the pin needs to know what the hardware wants, and the hardware resolution can be changed (for frequency range reasons) without any other code being modifed.

We could also choose a signed integer and use the sign bit as an overflow/underflow indicator.
//todo: add access to fade functionality

*/
class LEDC {
  //store pin so that only the constructor needs to know which pin.
  unsigned pin;
  unsigned shift; //recoded resolution
  bool ok; //might remove this by letting pin msb serve as 'valid' flag. I suspect that the bytes of code to check the MSB is more than the bytes of code and data to maintain this concept as a member.
  
  public:

  /** changes resolution number into a bit shift, which happens to be a symmetric operation */
  static constexpr unsigned shiftForResolution(uint8_t resolution){
    return 32-resolution;
  }

  /** sets output duty cycle from msb aligned value, passes @param duty back*/
  uint32_t operator =(unsigned duty){
    if(ok){ //do not even attempt the ledcWrite unless ledcAttach was called successfully. (reduces error spew when library debug is enabled)
      //don't update, might ail due to bad data value:  ok = 
      ledcWrite(pin, uint32_t(duty) >> shift);
    }
    return duty;
  }

  /** @returns duty cycle, msb aligned */
  operator unsigned() const {
    return ledcRead(pin)<<shift;
  }

  void detach(){
     if(ok){
      ledcDetach(pin);
      ok = false;
    }
    pin = ~0;
  }

  /** connect to hardware using local defaults for frequency and resolution */
  LEDC &attach(unsigned gpioPin){
    // if already ok then detach
    detach();
    //todo: check that pin is realistic
    ok = ledcAttach(pin, clockSource.typicalFrequency, shiftForResolution(shift));    
    pin = ok? gpioPin : ~0;    
    return *this;
  }

  /** after attaching change the frequency and resolution to non-default values */
  LEDC &configure(uint32_t freq, uint8_t resolution){
    shift = shiftForResolution(resolution); //if resolution is 1 then only msb of duty is to be used
    ok = ledcAttach(pin,freq,resolution);
    return *this;
  }


  uint32_t frequency() const {
    return ledcReadFreq(pin);
  }


  //default is to detectably invalid pin number, default shift from typical arduino code for users of ESP32's
  LEDC(unsigned gpioPin=~0):pin(gpioPin),shift(32-12),ok(false){}


  /////////////////////////////////////////////////////////
  // the group shares a clock
  // todo: needs work to justify even existing.
  // todo: add resolution bounds checking (e.g. APB clock at 5 kHz only supports 13 bits of resolution )
  struct ClockSource {
    bool wasSet=false;
    unsigned typicalFrequency=5000; //5000 was pulled from some example code.
    
    operator ledc_clk_cfg_t () const {
      return ledcGetClockSource();
    }
    ClockSource& operator =(ledc_clk_cfg_t source) {
      wasSet = ledcSetClockSource(source);
      return *this;
    }
  };
  static ClockSource clockSource;
  ////////////////////////////////////////////
  // this must be called before any function that touches on frequency.
  static bool begin(ledc_clk_cfg_t source){
    clockSource = source;
    return clockSource.wasSet;
  }
};

struct Color {
 
  static constexpr unsigned indexFor(char rgb){//b->0, g->1, r->2
    return 3 & (rgb>>2 | rgb>>3);//depends upon ascii encoding and valid input, GIGO.
  }

  enum Hue { blue, green, red , numHues};//ordered to match indexFor
  static unsigned hue(char rgb){
    return indexFor(rgb);
  }
///////////////////////////////////////////////
  unsigned pigment[numHues];
 
  //for when iterating over all hues, not caring which is processed first
  unsigned &operator[](unsigned which){
    if(which<numHues){
      return pigment[which];
    }
    return pigment[0];//bad pigment index gets you #0, which is presently blue
  }

  //crafted for UI usage. Programmatic usage should use Hue enum values and operator[]
  void apply(char letter,unsigned value){
    pigment[hue(letter)] = value;
  }
};

/** access to the hardware */
#include "ESP32Servo.h"
struct RGB_C3 {
  Color desired; // retain applied value for debug, and as a convenience for manipulation

  LEDC driver[3];
  
  RGB_C3(unsigned red, unsigned green, unsigned blue){
    //attach the pins
    driver[Color::Hue::red].attach(red);
    driver[Color::Hue::green].attach(green);
    driver[Color::Hue::blue].attach(blue);
  }

  void apply(Color &desired) {
    // todo: send 'desired' values to hardware.
    this->desired = desired;
    update();
  }

  void update(){
    for (unsigned hue=Color::Hue::numHues; hue-->0;){
      driver[hue]= desired[hue];
    }
  }

  void apply(char color, unsigned rawvalue) {
    desired.apply(color,rawvalue);
  }


};
