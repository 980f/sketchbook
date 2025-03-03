#pragma once

struct SimplePin {
  unsigned pin;
  //use this to invert logical sense of the input, such as when you add in an inverting amplifier late in development.
  bool activeHigh;

  operator bool () const {
    return digitalRead(pin) == activeHigh;
  }

  void operator = (bool setto) const {
    digitalWrite(pin, setto == activeHigh);
  }

  void operator <<(bool setto) const {
    digitalWrite(pin, setto == activeHigh);
  }

  constexpr SimplePin(const SimplePin &other) = default;


  constexpr SimplePin(unsigned pinNumber, bool activeHigh = true): pin(pinNumber), activeHigh(activeHigh) { }

  void setup(unsigned the_pinMode) const {
    pinMode(pin, the_pinMode);
  }

};

struct SimpleOutputPin: public SimplePin {
  //not constexpr, must run actual code, not just record bit patterns.
  SimpleOutputPin(unsigned pinNumber, bool activeHigh = true): SimplePin(pinNumber, activeHigh) {
    setup(OUTPUT);
  }
};
