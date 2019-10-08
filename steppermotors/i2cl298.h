#include "pcf8574.h"  //8 bit I2C I/O expander

/**
  byte I2C expander attached to L298
  M1: 0,1 legs, 2 power,
  M2: 3,4 legs, 5 power , 6,7 for home sensor

  Note 3,4 are probably swapped compared to other implementors of this interface, motor might go in reverse direction when switching from one to the other.
  since you are already changing wires to change controller you can swap the motor leads if this is a problem.
*/
class I2CL298 {
    PCF8574 dev;

  public:
    I2CL298(uint8_t which): dev(which) {
      dev.setInput(bits(6, 7));
    }

    /** matching FourBanger interface as best we can */
    void operator()(byte phase) {
      byte pattern = 0;
      //somewhat obtuse, but allows matching code to the comment
      pattern |= bit(0) << greylsb(phase);//set one of two bits, other left at zero
      pattern |= bit(3) << greymsb(phase);//... coz that is how this chip works.

      dev.merge(pattern, bits(0, 1, 3, 4));
    }

    class PowerWidget: public BoolishRef {
        I2CL298 &parent;

      public:
        PowerWidget(I2CL298 &parent): parent(parent) {}
        operator bool() const {
          return bitFrom(parent.dev.cachedBits() , 2); //trust that 2 and 5 are the same
        }
        bool operator =(bool on) const {
          parent.dev.merge(on ? bits(2, 5) : 0, bits(2, 5));
          return on;
        }
    };

};
