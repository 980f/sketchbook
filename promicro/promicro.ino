
#undef bit
#include "bitbanger.h"

#include "pinclass.h"
#include "digitalpin.h"
#include "millievent.h"

#include "cheaptricks.h"


//const OutputPin<7, LOW> relay1;
////low turns relay on
//const OutputPin<9, LOW> relay2;

const OutputPin<9> ph0;
const OutputPin<8> ph1;
const OutputPin<7> ph2;
const OutputPin<6> ph3;


class Stepper {
  public:
    int step = 0;
    //  unsigned perRevolution=200;
    //  unsigned phase=0;
    //
    void applyPhase(unsigned phase) {
      unsigned bits = 0x33 >> (phase % 4);

      ph0 = bit(bits, 0);
      ph1 = bit(bits, 1);
      ph2 = bit(bits, 2);
      ph3 = bit(bits, 3);
    }

    operator ()(bool fwd) {
      step += fwd ? 1 : -1;
      applyPhase(step);
    }

    operator ++() {
      ++step;
      applyPhase(step);
      //    if((++phase)==perRevolution){
      //      phase=0;
      //    }

    }

    operator --() {
      --step;
      applyPhase(step);
      //    if((phase==0){
      //      phase=perRevolution;
      //    }
      //  --phase;

    }
};


class ProMicro {
  public:
    struct TxLed {
      bool lastset;
      operator bool()const {
        return lastset;
      }
      bool operator=(bool setting) {
        lastset = setting;
        lastset ? TXLED1 : TXLED0; //vendor macros, should replace with an OutputPin
        return setting;
      }
    };
};

//ProMicro::TxLed txled;
//
//hall effect sensor is low when magnet is present
//const InputPin<10, LOW> hall;
//
//const OutputPin<17, LOW> rxled;
//
//#include "softpwm.h"
//
////SoftPwm led(250, 750);
//

MonoStable r1pulse(100);
Stepper geared;

bool clockwise = false;

void setup() {
  //  led.setPhases(250, 750);

  Serial.begin(500000);//number doesn't matter.
  //  ++geared;//may jerk. Sould read pins and start from there.
}

// the loop function runs over and over again forever
void loop() {
  //update input pin as fast as loop() allows.
  //  rxled = hall; //digitalWrite(rxled,digitalRead(hall));

  if (MilliTicked) { //this is true once per millisecond.
    // causes gross delays, printing is blocking!   if(hall) Serial.println(milliEvent.recent());
    //    led ? TXLED1 : TXLED0; //vendor macros, no assignment provided.
    if (r1pulse) {
      geared(clockwise);
      //          txled = bit(geared.step, 0);
    }
  }

  if (Serial) {
    if (Serial.available()) {
      auto key = Serial.read();
      Serial.print(char(key));//echo.
      switch (key) {
        case '1': case '2': case '3': case '4': {//jump to phase
            geared.applyPhase(key - 1);
          }
          break;
        case 'w':
          ++geared;
          Serial.println(geared.step);
          break;
        case 'e':
          --geared;
          Serial.println(geared.step);
          break;
        case 'r':
          clockwise = false;
          r1pulse.start();
          break;
        case 'f':
          clockwise = true;
          r1pulse.start();
          break;
        case 'x':
          r1pulse.stop();
          break;
        case 'g':
          break;
        case 'y':
          break;
        case 'h':
          break;
        default:
          Serial.print("?\n");
          break;
        case '\n':
        case '\r':
          //ignore end of line used to flush letter commands.
          break;
      }
    }
  }
}
