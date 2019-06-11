#pragma once //(C) 2019 Andy Heilveil, github/980f

#include "pinclass.h"

bool greylsb(byte step) {
  byte phase = step & 3;
  return (phase == 1) || (phase == 2);
}

bool greymsb(byte step) {
  return (step & 3) >> 1;
}

//for 4 bit packed field:
byte motorNibble(byte step) {
  bool x = greylsb(step);
  bool y = greymsb(step);
  byte nib = 0;
  if ( x) nib |= 1;
  if (!x) nib |= 2;
  if ( y) nib |= 4;
  if (!y) nib |= 8;
  return nib;
}


/** 4 wire 2 phase unipolar drive */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn> class FourBanger {
  protected:
    OutputPin<xp> mxp;
    OutputPin<xn> mxn;
    OutputPin<yp> myp;
    OutputPin<yn> myn;

    void operator()(byte step) {
      bool x = greylsb(step);
      bool y = greymsb(step);
      mxp = x;
      mxn = !x;
      myp = y;
      myn = !y;
    }

};

/** 4 phase unipolar with power down via nulling all pins. Next step will energize them again. It is a good idea to energize the last settings before changing them, but not absolutely required */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn> class ULN2003: public FourBanger<xp, xn, yp, yn> {
    using Super = FourBanger<xp, xn, yp, yn>;
  public:
    void operator()(byte step) {
      if (step == 255) { //magic value for 'all off'
        powerDown();
      }
      Super::operator()(step);
    }

    void powerDown() {
      //this-> or Super:: needed because C++ isn't yet willing to use the obvious base class.
      this->mxp = 0;
      this->mxn = 0;
      this->myp = 0;
      this->myn = 0;
    }
};

/** 4 unipolar drive, with common enable. You can PWM the power pin to get lower power, just wiggle it much faster than the load can react. */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn, PinNumberType pwr,  unsigned polarity> class FourBangerWithPower: public FourBanger<xp, xn, yp, yn> {
    using Super = FourBanger<xp, xn, yp, yn>;
  public: //while we are working out init
    OutputPin<pwr, polarity> enabler;
  public:
    void operator()(byte step) {
      if (step == 255) { //magic value for 'all off'
        powerDown();
      }
      //      enabler = 1;
      Super::operator()(step);
    }

    void powerDown() {
      //      enabler = 0;
    }
};

//old chips in hand:
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn, PinNumberType pwr> class UDN2540: public FourBangerWithPower<xp, xn, yp, yn, pwr, LOW> {};//to test by switching between this and DRV8833

//popular dual bridge, which curiously has same driver pattern as unipolar.
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn, PinNumberType pwr> class DRV8833: public FourBangerWithPower<xp, xn, yp, yn, pwr, HIGH> {};
