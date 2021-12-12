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



#include "Adafruit_VL53L0X.h"


#define HoggingEverything 0

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

/** quick and dirty stopwatch with print. */
class MicroStopwatch: public Printable  {
    using MicroTicks = decltype(micros());
    MicroTicks starting, ending;
  public:
    MicroStopwatch() {
      start();
    }

    MicroTicks elapsed()const {
      return ending - starting;
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
MicroStopwatch stopwatch;

void setup() {
  Serial.begin(115200);

  // wait until serial port opens for native USB devices
  while (! Serial) {
    delay(1);
  }

  Serial.println("Adafruit VL53L0X test, trying continuous mode");
  if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    while (1);
  }
  // power
  Serial.println(F("VL53L0X API Simple Ranging example\n\n"));//formerly Serial1

#if 0==HoggingEverything
  stopwatch.start();
  lox.startRangeContinuous(100);//100 was delay in blocking loop, blocking execution was ~40ms something present, ~23 nothing in view.
  stopwatch.stop();
  Serial.print("startRangeContinuous us:");
  Serial.println(stopwatch);
#endif
}

//todo:histogram or averager for execution time stats.

void loop() {
  VL53L0X_RangingMeasurementData_t measure;
#if HoggingEverything
  delay(100);
  Serial.print("Reading a measurement... ");
  stopwatch.start();
  lox.rangingTest(&measure, false); // pass in 'true' to get debug data printout!
  stopwatch.stop();
  auto millimeters = measure.RangeMilliMeter;
  if (measure.RangeStatus == 4) {  // phase failures have incorrect data
    Serial.println(" out of range ");
    return;
  }

#else
  stopwatch.start();
  bool measurementIsReady = lox.isRangeComplete(); //401 us
  stopwatch.stop();

  if (!measurementIsReady) {
    Serial.print("Poll took "); Serial.print(stopwatch); Serial.println(" us ");
    delay(20);
    return;
  }
  stopwatch.start();
  auto millimeters = lox.readRangeResult();// 3.66 ms!
  stopwatch.stop();

#endif

  Serial.print("Took us: "); Serial.println(stopwatch);
  Serial.print("Distance: mm:"); Serial.print(millimeters);
  Serial.print("\tin:");
  Serial.println(float(millimeters) / 25.4);

}
