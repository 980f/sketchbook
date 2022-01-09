/*
  Evaluation program for VL53L0X device and library

  Original example code is mostly still present as 'sample slowly'

  With 'continuous' sampling set to 100ms it took either 4 or 5 20.4 ms polls of the device to detect that data was present.
  Each poll attempt took 0.4 ms, presumably in I2C activity. I haven't chased down whether 100kbaud or 400k baud is used.

  There is an indicator bit coming from the device, we should see if it can be set to identify read data available.
  !Upon reading the manual they have no clue as to what the pin does, they document it being a threshold detect in continuous ranging mode but there is not a peep out of them about how it behaves in single measurement mode.
  We will have to experiment. Note: their (ST's) function documentation is 99% boiler plate and 0% information, it is not worth reading, often is quite wrong when you read the code.
  !GPIO has a 'data available' mode, should use that for most applications, the threshold stuff is for replacing presence detectors.



  The actual read of data took a hefty 3.66 ms, an unpleasant amount of time. I will have to find how much of that is device time and how much postprocessing on the Arduino.
  Floating point is avoided in stm's code so that is not likely to be the problem. Bad programming is far more likely.
  !It appears that they read all sorts of quality metrics along with the actual value of interest. Using a SAMD device we should be able to do the readings in hardware instead of spinning on the I2C interface.
  In fact even using interrupts per byte is viable compared to blocking the whole processor to read a bunch of I2C data.
  Their code accumulates error responses from the I2C driver, but ignores them proceeding to do successive operations unconditionally.

  The I2C-DMA facility does multi-byte transfers, we should be able to queue up multiple I2C transactions to be done in sequence via interrupts on DMA completion.
  Time to read the arduino zeroDMA module.


  From the datasheet 20ms is fastest useful cycle, 33ms good compromise between speed and accuracy, 200ms best accuracy.
  For simplest and fastest use firing off single cycles and reading results a safe time after they should be present gets rid of the overhead of checking for completion status.
  Hopefully that mode makes the interrupt work as some diagrams show but no code documentation describes in which case we could go ahead and poll rather than time and hope.

*/
#include "vl53l0x_platform_log.h" //initLogging
/////////////////////////
//pin used to receive 'GPIO1' signal
#define VLGPIO1pin 3



/////////////////////////
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


#include "digitalpin.h"  //for interrupt signal, even though we will not use it as an interrupt due to response code taking way too long for an isr.
const DigitalInput devReady(VLGPIO1pin);//high active


////////////////////////////////
#include "millievent.h"

//whether to let device sample continuously or to ask for samples. controlled by 'c' and 's' commands:
bool devicePaces = true;
MilliTick sampleInterval=33;

///////////////////////////////
//time execution of api calls, some take many milliseconds.
MicroStopwatch stopwatch;

//wrap a function call with this, so we can make timing conditional someday via nulling the macro content
#define TIMED(...) \
  stopwatch.start();\
  __VA_ARGS__ \
  ; stopwatch.stop();


void reportTime(const char *prefix, const char *suffix = " us.") {
  Serial.print(prefix);
  Serial.print(stopwatch);
  Serial.println(suffix);
}

//////////////////////////

void report(unsigned millimeters) {
  static bool OOR = false;
  if (changed(OOR, millimeters == 65535) || !OOR) {
    reportTime("Access: ", " us");
    if (OOR) {
      Serial.println("Out of range.");
    } else {
      Serial.print("Distance: mm:"); Serial.print(millimeters);
      Serial.print("\tin:");
      Serial.println(float(millimeters) / 25.4);
    }
  }
}

////////////////////////////////////

///////////////////////////////
#include "vl53ranger.h"
using  namespace VL53L0X; //for all the enums
VL53Ranger ranger;

void startContinuous() {

  TIMED(
    ranger.arg.sampleRate_ms = sampleInterval;
    ranger.arg.operatingMode = NonBlocking::DataStream;
    ranger.startProcess(NonBlocking::Operate, MilliTicked.recent());
  );
  reportTime("startRangeContinuous ");

}

void stopContinuous() {
  TIMED(
    ranger.arg.operatingMode = NonBlocking::OnDemand;
    ranger.startProcess(NonBlocking::Operate, MilliTicked.recent());
  );
  reportTime("stopRangeContinuous ");
}

bool reportPolls = false;

//////////////////////////
void setup() {
  Serial.begin(115200);

  //up to 64 chars will buffer waiting for usb serial to open
  Serial.println("VL53L0X tester (github/980f)");

  //  connectionPacer.start();
    ranger.agent.arg.sampleRate_ms = 33;// a leisurely rate. Since it is nonzero continuous measurement will be initiated when the device is capable of it
  ranger.agent.arg.gpioPin = 3;//todo: #define near top of file/class
  VL53L0X::initLogging(); //api doesn't know how logging is configured

  ranger.setup(devicePaces ? NonBlocking::DataStream : NonBlocking::OnDemand);
}


void loop() {
  if (MilliTicked) {


    if (!Serial) {//make sure we have debug before we invoke activity
      return;
    }

    //  if (!connected) {
    //    if (connectionPacer.lap() > 100000) { //10Hz retry on connect
    //      if (!lox.begin()) {
    //        connectionPacer.start();
    //      } else {
    //        connected = true;
    //        if (devicePaces) {
    //          startContinuous();
    //        }
    //      }
    //    }
    //  }


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
        case 'p':
          reportPolls = !reportPolls;
          break;
      }
    }


    ranger.loop(MilliTicked.recent());//all work is done via callbacks to ranger.
    //
    //  if (!connected) {
    //    return;
    //  }
    //
    //  //we have Serial and are connected, and it is time to either expect a result or to ask for one
    //  if (devicePaces) {
    //    TIMED (bool measurementIsReady = lox.isRangeComplete());
    //
    //    if (!measurementIsReady) {
    //      if (reportPolls)
    //        reportTime("Poll took ");// ~0.4 ms, ~16 I2C bytes
    //      //perhaps increment sampler to slow down poll a bit, BUT it should be ready except for clock gain differences between us and VL53.
    //      return;
    //    } else {
    //      sampler += sampleInterval;
    //
    //      TIMED(unsigned millimeters = lox.readRangeResult();); // 3.66 ms!  ~147 bytes. DMA based I2C would be nice!
    //      report(millimeters);
    //    }
    //  } else {
    //    VL53L0X_RangingMeasurementData_t measure;
    //    sampler += sampleInterval;
    //    TIMED(lox.rangingTest(&measure, false);); // pass in 'true' to get debug data printout!
    //    //got 8190 when signal status was '4'
    //    report(measure.RangeStatus == 4 ? 65535 : measure.RangeMilliMeter); //65535 is what other mode gets us when signal is out of range
    //  }

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
  980f repackaging: 29580 (11%) 29568
  980f no blockers: 29592 -- larger when code is removed!? somehow the image gets smaller when some unused functions are not present to be discarded by the linker.
avr - leonardo:
  Sketch uses 22654 bytes (79 % ) of program storage space. Maximum is 28672 bytes.
  Global variables use 1301 bytes (50 % ) of dynamic memory, leaving 1259 bytes for local variables. Maximum is 2560 bytes.
  
D1 - lite :
  Sketch uses 288657 bytes (30 % ) of program storage space. Maximum is 958448 bytes.
    Global variables use 30300 bytes (36 % ) of dynamic memory, leaving 51620 bytes for local variables. Maximum is 81920 bytes.


#endif
