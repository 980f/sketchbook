
/* playground express:
    A0 (a.k.a D12) - This is a special pin that can do true analog output so it's great for playing audio clips. In can be digital I/O, or analog I/O, but if you do that it will interfere with the built-in speaker. This is the one pin that cannot be used for capacitive touch.
  A1 / D6 - This pin can be digital I/O, or analog Input. This pin has PWM output and can be capacitive touch sensor
  A2 / D9 - This pin can be digital I/O, or analog Input. This pin has PWM output and can be capacitive touch sensor
  A3 / D10 - This pin can be digital I/O, or analog Input. This pin has PWM output and can be capacitive touch sensor
  A4 / D3 - This pin can be digital I/O, or analog Input. This pin is also the I2C SCL pin, and can be capacitive touch sensor
  A5 / D2 - This pin can be digital I/O, or analog Input. This pin is also the I2C SDA pin, and can be capacitive touch sensor
  A6 / D0 - This pin can be digital I/O, or analog Input. This pin has PWM output, Serial Receive, and can be capacitive touch sensor
  A7 / D1 - This pin can be digital I/O, or analog Input. This pin has PWM output, Serial Transmit, and can be capacitive touch sensor
  D4 - Left Button A
  D5 - Right Button B
  D7 - Slide Switch
  D8 - Built-in 10 NeoPixels
  D13 - Red LED
  D27 - Accelerometer interrupt
  D25 - IR Transmitter
  D26 - IR Receiver
  A0 - Speaker analog output
  A8 - Light Sensor
  A9 - Temperature Sensor
  A10 - IR Proximity Sensor
  D28 - Internal I2C SDA (access with Wire1)
  D29 - Internal I2C SCL (access with Wire1)
  D30 (PIN_SPI_MISO) - SPI FLASH MISO
  D31 (PIN_SPI_SCK) - SPI FLASH SCK
  D32 (PIN_SPI_MOSI) - SPI FLASH MOSI
  D33 - SPI FLASH Chip Select

*/

#include "millievent.h"
#include "softpwm.h"
#include "pinclass.h"

#include <Adafruit_CircuitPlayground.h>

/** wrap ce parts together */
struct CircuitExpress {
  const volatile InputPin<CPLAY_LEFTBUTTON, LOW> swA;
  const volatile InputPin<CPLAY_RIGHTBUTTON, LOW> swB;
  const volatile InputPin<CPLAY_SLIDESWITCHPIN, LOW> slider;
  const OutputPin<CPLAY_REDLED, HIGH> redled;
};
const CircuitExpress ce;


Adafruit_CircuitPlayground cp;

SoftPwm flasher(1400, 600);

/** track last high and low times. Once the full 980f libraries are easily accessible we will do running polynomial fits to each level */
class PwmDecoder  {
  unsigned width[2];
  bool lastSample=false;
  unsigned lastChange=0;
  unsigned sampletick=0;
public:
  bool operator()(bool sample){
    ++sampletick;
    if(changed(lastSample,sample)){
      width[1-lastSample]=sampletick-lastChange;
      lastChange=sampletick;
    }
  }

  unsigned operator [](bool which) const {
    return width[which];//could !!which to ensure valid range.
  }
};

PwmDecoder pwmin;
//#include "millichecker.h"
//MilliChecker mcheck;

void setup() {
  //relying on constructors to do most init.
  Serial.begin(1000000);
  //this prevented Serial monitor from starting.  while(!Serial);
  cp.begin();
}

#include "histogrammer.h"

HistoGrammer<2> flashes;
void loop() {
  if (MilliTicked) { //this is true once per millisecond.
    bool flashed=flashes(flasher);
    ce.redled = flashed;
    pwmin(flashed);
    if (MilliTicked.every(1000)) {
      Serial.print(pwmin[0]);
      Serial.print('/');
      Serial.println(pwmin[1]);
    }

    if (MilliTicked.every(10000)) {
      flashes.show();
    }
  }

}
