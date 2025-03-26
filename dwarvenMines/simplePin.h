#pragma once

struct SimplePin {
  unsigned number;
  //use this to invert logical sense of the input, such as when you add in an inverting amplifier late in development.
  bool activeHigh;
  unsigned modeSet = ~0u; // an invalid value

  operator bool () {
    //    Serial.printf("Read D%u\t", number);
    if (modeSet == ~0u) {
      setup(activeHigh ? INPUT : INPUT_PULLUP);
    }
    return digitalRead(number) == activeHigh;
  }

  //this confuses the compiler, so we will use shift operators to set values
  void operator = (bool setto) = delete ;  //need to make this work, but until then it is invalid
  //    const {
  //      digitalWrite(pin, setto == activeHigh);
  //    }

  //  SimplePin(SimplePin &&other) = default;

  SimplePin(unsigned pinNumber, bool activeHigh = true): number(pinNumber), activeHigh(activeHigh) { }

  void setup(unsigned the_pinMode) {
    //    Serial.printf("setup pin %u to %u\n", number, the_pinMode);
    modeSet = the_pinMode;
    pinMode(number, the_pinMode);
  }

};

#ifndef INPUT_PULLDOWN
#define INPUT_PULLDOWN INPUT
#endif

struct SimpleInputPin: public SimplePin {
  SimpleInputPin(unsigned pinNumber, bool activeHigh = true): SimplePin(pinNumber, activeHigh) {}
  void setup() {
    SimplePin::setup(activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
};

struct SimpleOutputPin: public SimplePin {

  //not constexpr, must run actual code, not just record bit patterns.
  SimpleOutputPin(unsigned pinNumber, bool activeHigh = true): SimplePin(pinNumber, activeHigh) {}

  void operator <<(bool setto) {
    if (modeSet == ~0u) { //lazy init
      SimplePin::setup(OUTPUT);
    }
    digitalWrite(number, setto == activeHigh);
  }

  void setup() {
    SimplePin::setup(OUTPUT);
  }

  bool toggle() {
    operator<<(!operator bool());
    return operator bool();//read a second time in case pin is broken, e.g. is an invalid pin number
  }

};
