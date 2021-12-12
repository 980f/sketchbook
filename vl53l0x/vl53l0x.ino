/*
  Evaluation program for VL53L0X device and library

  Original example code was close to having HoggingEveything defined to 1.

  With 'continuous' sampling set to 100ms it took either 4 or 5 20.4 ms polls of the device to detect that data was present.
  Need to redo without calls to 'delay' to see how exact the interval is. Pipelined single request and read might be easier to deal with.

  Each poll attempt took 0.4 ms, presumably in I2C activity. I haven't chased down whether 100kbaud or 400k baud is used.

  There is an indicator bit coming from the device, we should see if it can be set to identify read data available.

  The actual read of data took a hefty 3.66 ms, an unpleasant amount of time. I will have to find how much of that is device time and how much postprocessing on the Arduino.
  Floating point is avoided in stm's code so that is not likely to be the problem. Bad programming is far more likely.



*/

#include "cheaptricks.h" //changed()

/** quick and dirty stopwatch with print. */
class MicroStopwatch: public Printable  {
    using MicroTicks = decltype(micros());
    MicroTicks starting, ending;
  public:
    MicroStopwatch() {
      start();
    }

    MicroTicks elapsed() const {
      return ending - starting;
    }

    MicroTicks lap() const {
      return micros() - starting;
    }

    void start() {
      ending = starting = micros();
    }

    void stop() {
      ending = micros();
    }

    size_t printTo(Print &p) const override {
      return p.print(elapsed());
    }

};


///////////////////////////////
#include "Adafruit_VL53L0X.h"

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

//allow for plugging device in after this processo boots:
bool connected = false;
MicroStopwatch connectionPacer;

//perhaps use millievent.h, until then:
using MilliTicks = decltype(millis());

//whether to let device sample continuously or to ask for samples. controlled by 'c' and 's' commands:
bool devicePaces = true;
//timer for either taking a sample or checking for one generated autonomously (because polling is expensive)
MilliTicks sampler;

MilliTicks sampleInterval = 100;

//time execution of api calls, some take many milliseconds.
MicroStopwatch stopwatch;

#define TIMED(...) \
  stopwatch.start();\
  __VA_ARGS__ \
  ; stopwatch.stop();


void report(unsigned millimeters) {
  static bool OOR = false;
  if (changed(OOR, millimeters == 65535) || !OOR) {
    Serial.print("Access: "); Serial.println(stopwatch);
    if (OOR) {
      Serial.println("Out of range.");
    } else {
      Serial.print("Distance: mm:"); Serial.print(millimeters);
      Serial.print("\tin:");
      Serial.println(float(millimeters) / 25.4);
    }
  }
}

void reportTime(const char *prefix, const char *suffix = "") {
  Serial.print(prefix);
  Serial.print(stopwatch);
  Serial.println(suffix);
}

void setup() {
  Serial.begin(115200);

  //up to 64 chars will buffer waiting for usb serial to open
  Serial.println("VL53L0X tester (github/980f)");

  sampler = millis() + sampleInterval;
  connectionPacer.start();
}

void startContinuous() {
  TIMED(
    lox.startRangeContinuous(sampleInterval);
  );

  sampler = millis();
  reportTime("startRangeContinuous us:");
}

void stopContinuous() {
  TIMED(
    lox.stopRangeContinuous();
  );
  reportTime("stopRangeContinuous us:");
}

void loop() {
  if (!Serial) {
    delay(1);//because hammering on Serial is painful I presume.
    return;
  }

  if (!connected) {
    if (connectionPacer.lap() > 100000) { //10Hz retry on connect
      if (!lox.begin()) {
        connectionPacer.start();
        return;
      } else {
        connected = true;
        if (devicePaces) {
          startContinuous();
        }
      }
    }
  }

  bool timeToSample = sampler < millis();

  if (!timeToSample) {
    //todo: read serial to change operating parameters
    for (unsigned some = Serial.available(); some-- > 0;) {
      int one = Serial.read();
      switch (tolower(one)) {
        case 'c':
          if (changed(devicePaces, true)) {
            startContinuous();
          }
          break;
        case 's':
          if (changed(devicePaces, false)) {
            stopContinuous();
          }
          break;
      }
    }
    return;
  }
  //we have Serial and are connected, and if device is pacing then it has been started
  if (devicePaces) {
    TIMED (bool measurementIsReady = lox.isRangeComplete());

    if (!measurementIsReady) {
      Serial.print("Poll took "); Serial.print(stopwatch); Serial.println(" us ");
      //perhaps increment sampler to slow down poll a bit, BUT it should be ready except for clock gain differences between us and VL53.
      return;
    } else {
      sampler += sampleInterval;

      TIMED(unsigned millimeters = lox.readRangeResult();); // 3.66 ms!
      report(millimeters);
    }
  } else {
    VL53L0X_RangingMeasurementData_t measure;
    sampler += sampleInterval;
    TIMED(lox.rangingTest(&measure, false);); // pass in 'true' to get debug data printout!
    //got 8190 when signal status was '4'
    report(measure.RangeStatus == 4 ? 65535 : measure.RangeMilliMeter); //65535 is what other mode gets us when signal is out of range
  }

}
