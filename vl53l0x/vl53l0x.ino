/*
  Evaluation program for VL53L0X device and library

  Original example code is mostly still present as 'sample slowly'

  With 'continuous' sampling set to 100ms it took either 4 or 5 20.4 ms polls of the device to detect that data was present.
  Each poll attempt took 0.4 ms, presumably in I2C activity. It appears that the Arduino default of 100kbaud is used,
  switching to 400k should make most operations sub-millisecond which is valuable for integration with other activity.

  The manual was useless, plenty of errors as well as omissions.
  Reading the source code tells us there is a "data available' mode for the GPIO pin.


  The actual read of data took a hefty 3.66 ms, an unpleasant amount of time. I will have to find how much of that is device time and how much postprocessing on the Arduino in ST's library.
  Floating point is avoided in stm's code so that is not likely to be the problem. Bad programming is far more likely.
  It appears that they read all sorts of quality metrics along with the actual value of interest.
  Using a SAMD device we should be able to do the readings in hardware instead of spinning on the I2C interface.
  One 12 byte I2C read and conditionally (2) 2 bytes writes and (1) 2 byte read.
  In fact even using interrupts per byte is viable compared to blocking the whole processor to read a bunch of I2C data.
  Their code accumulates error responses from the I2C driver, but ignores them proceeding to do successive operations unconditionally.

  --  The I2C-DMA facility does multi-byte transfers, we should be able to queue up multiple I2C transactions to be done in sequence via interrupts on DMA completion.   Time to read the arduino zeroDMA module.

  From the datasheet 20ms is fastest useful cycle, 33ms good compromise between speed and accuracy, 200ms best accuracy.
  For simplest and fastest use firing off single cycles and reading results a safe time after they should be present gets rid of the overhead of checking for completion status.

*/
/////////////////////////
//pin used to receive 'GPIO1' signal
#define VLGPIO1pin 3
#define WS2812pin 2

/////////////////////////
#include "cheaptricks.h" //changed()

#include "chainprinter.h"
ChainPrinter dbg(Serial, true);

#include "digitalpin.h"

DigitalOutput imAlive(LED_BUILTIN);
DigitalOutput dataReadyToggle(11);//led on seeed-xia0


////////////////////////////////////////////
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

    MicroTicks stop(bool andReport = false) {
      ending = micros();
      return andReport ? elapsed() : ending;//fyi: ending should already be in a register so returning it instead of a constant such as zero should be less code.
    }

    size_t printTo(Print &p) const override {
      return p.print(elapsed());
    }

};

//time execution of api calls, some take many milliseconds.
MicroStopwatch stopwatch;

//wrap a function call with this, so we can make timing conditional someday via nulling the macro content
#define TIMED(...) \
  stopwatch.start();\
  __VA_ARGS__ \
  ; stopwatch.stop();


void reportTime(const char *prefix) {
  dbg(prefix, stopwatch, " us");
}


///////////////////////////////
#include "Adafruit_VL53L0X.h"
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
#include "digitalpin.h"  //for interrupt signal, even though we will not use it as an interrupt due to response code taking way too long for an isr.
const DigitalInput devReady(VLGPIO1pin);//high active


////////////////////////////////


#include "millievent.h"
//timer for either taking a sample or checking for one generated autonomously (because polling is expensive)
MonoStable sampler{33};

//whether to let device sample continuously or to ask for samples. controlled by 'c' and 's' commands:
bool devicePaces = true;

//allow for plugging device in after the arduino boots:
bool connected = false;



void report(unsigned millimeters) {
  static bool OOR = false;
  if (changed(OOR, millimeters == 65535) || !OOR) {
    reportTime("Access: ");
    if (OOR) {
      dbg("Out of range.");
    } else {
      dbg("Distance: mm:", millimeters, "\tin:", float(millimeters) / 25.4);
    }
  }
}

////////////////////////////////////

void setup() {
  Serial.begin(115200);
  dbg.stifled = !Serial; //almost always going to be false at this point in time.
  sampler.start();//used for connection retries as well as measurement timeout
}

void startContinuous() {
  dbg("enter start");
  TIMED(
    lox.startRangeContinuous(sampler);
  );
  reportTime("startRangeContinuous ");

  sampler.start();
}

void stopContinuous() {
  dbg("enter stop");
  TIMED(
    lox.stopRangeContinuous();
  );
  reportTime("stopRangeContinuous ");
}

bool reportPolls = false;

bool useGpio = true;

bool measurementSeemsReady() {
  if (useGpio) {
    return devReady;
  }

  TIMED ( bool measurementIsReady = lox.isRangeComplete());
  if (!measurementIsReady && reportPolls) {
    reportTime("Poll took ");// ~0.4 ms, ~16 I2C bytes
  }
  return measurementIsReady;
}

#include "scani2c.h" //for i option
unsigned connectionAttempts = 0;
unsigned connectionSuccesses = 0;

void loop() {
  if (MilliTicked) {//keep the processor cool, nothing happens fast with this sensor

    imAlive = bool(Serial);

    if (changed(dbg.stifled, !Serial)) {//takes wildly variable amount of time, 800..1600 so far.
      dbg("VL53L0X tester (github/980f) ", MilliTicked.recent()); //does nothing if stifled.
      dbg(connected ? "Connected" : "not Connected");
    }
    if (!sampler.isRunning()) { //until we figure out why it stops
      sampler.start();
    }
    if (sampler) {//it as been a while, see if the ranger needs some support or has data
      if (!connected) {
        ++connectionAttempts;
        if (changed(connected, lox.begin(VL53L0X_I2C_ADDR, !dbg.stifled))) {
          ++connectionSuccesses;
          dbg("Connected.");
          if (devicePaces) {
            startContinuous();
          } else {
            sampler.start();
          }
        } else {
          if (connectionAttempts % 50 == 0) {
            dbg("Attempts:", connectionAttempts, " Successes:", connectionSuccesses);
          }
          sampler.start();//try try again, but later.
        }
      } else {
        //time to check up on measurement ready
        if (devicePaces) {
          bool measurementIsReady = measurementSeemsReady();
          if (!measurementIsReady) {
            if (reportPolls) {
              reportTime("Poll took ");// ~0.4 ms, ~16 I2C bytes
            }
            //perhaps increment sampler to slow down poll a bit, BUT it should be ready except for clock gain differences between us and VL53.
            sampler.start();
          } else {
            dataReadyToggle.toggle();
            TIMED(unsigned millimeters = lox.readRangeResult()); // 3.66 ms!  ~147 bytes. DMA based I2C would be nice!
            report(millimeters);
            sampler.start();
          }
        } else {//blocking read
          VL53L0X_RangingMeasurementData_t measure;
          sampler.start();//for next one, before the blocking call so that we get harmonic sampling
          TIMED(lox.rangingTest(&measure, false)); // pass in 'true' to get debug data printout!
          //got 8190 when signal status was '4'
          report(measure.RangeStatus == 4 ? 65535 : measure.RangeMilliMeter); //65535 is what other mode gets us when signal is out of range
        }
      }
    }

    for (unsigned some = Serial.available(); some-- > 0;) {
      int one = Serial.read();
      switch (tolower(one)) {
        case ' ':
          dbg("SE:", sampler.elapsed(), " SP:", sampler, " Sr:", sampler.isRunning());
          if (!sampler.isRunning()) { //until we figure out why it stops
            sampler.start();
            dbg("!SE:", sampler.elapsed(), " SP:", sampler, " Sr:", sampler.isRunning());
          }
          break;
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
        case 'i':
          scanI2C(dbg, 0x30, 0x20);
          break;
        case 'p':
          reportPolls = !reportPolls;
          break;
        default:
          dbg(char(one), " means nothing to me.");
          break;
      }
    }
  }
}

#if 0

readRangeResult timing analysis (3.66 ms):
  VL53L0X_GetRangingMeasurementData(pMyDevice, &measure);
  VL53L0X_ReadMulti(Dev, 0x14, localBuffer, 12); comments say 14 bytes, code says 12. perhaps the 14 includes address + index ?
  VL53L0X_GETPARAMETERFIELD(Dev, XTalkCompensationRateMegaCps, XTalkCompensationRateMegaCps);
  VL53L0X_GETPARAMETERFIELD(Dev, XTalkCompensationEnable, XTalkCompensationEnable);
  VL53L0X_get_pal_range_status(Dev, DeviceRangeStatus, SignalRate, EffectiveSpadRtnCount, pRangingMeasurementData, &PalRangeStatus);
  VL53L0X_WrByte(Dev, 0xFF, 0x01);
  VL53L0X_RdWord(Dev, VL53L0X_REG_RESULT_PEAK_SIGNAL_RATE_REF, &tmpWord);
  VL53L0X_WrByte(Dev, 0xFF, 0x00);
  VL53L0X_GetLimitCheckEnable
  VL53L0X_calc_sigma_estimate
  VL53L0X_GetLimitCheckEnable
  VL53L0X_SETARRAYPARAMETERFIELD


  VL53L0X_ClearInterruptMask(pMyDevice, 0);


qtpy build adafruit library:
  Sketch uses 34900 bytes (13 % ) of program storage space. Maximum is 262144 bytes.
avr - leonardo:
  Sketch uses 22654 bytes (79 % ) of program storage space. Maximum is 28672 bytes.
  Global variables use 1301 bytes (50 % ) of dynamic memory, leaving 1259 bytes for local variables. Maximum is 2560 bytes.
D1 - lite :
  Sketch uses 288657 bytes (30 % ) of program storage space. Maximum is 958448 bytes.
    Global variables use 30300 bytes (36 % ) of dynamic memory, leaving 51620 bytes for local variables. Maximum is 81920 bytes.


#endif
