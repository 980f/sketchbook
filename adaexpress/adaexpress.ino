
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

struct CircuitExpress {
  const InputPin<4, LOW> swA;
  const InputPin<5, LOW> swB;
  const InputPin<7, LOW> slider;
  const OutputPin<13, LOW> redled;
};
const CircuitExpress ce;

SoftPwm flasher(800, 200);

#include "millichecker.h"
MilliChecker mcheck;

void setup() {
  //relying on constructors to do most init.
  Serial.begin(1000000);
  //this prevented Serial monitor from starting.  while(!Serial);
}

void loop() {
  if (MilliTicked) { //this is true once per millisecond.
    mcheck.check();

    ce.redled = flasher;
    if (MilliTicked.every(1000)) {
      //     Serial.print(ce.redled ? '+':'-');
      Serial.println(flasher.phase());
    }

    if (MilliTicked.every(10000)) {
      mcheck.show();
    }
  }

}
