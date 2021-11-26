#pragma once

/*
   inspird by OctoBanger_TTL, trying to be compatible with their gui but might need a file translator.
   Octobanger had blocking delays in many places, and halfbaked attempts to deal with the consequences.
   If the human is worried about programming while the device is operational they can issue a command to have the trigger ignored.
   By ignoring potential weirdness from editing a program while it is running 1k of ram is freed up, enough for this functionality to become a part of a much larger program.

  test: guard against user spec'ing rx/tx pins
  todo: debate whether suppressing the trigger locally should also suppress the trigger output, as it presently does
  note: trigger held active longer than sequence results in a loop.  Consider adding config for edge vs level trigger.
  test: package into a single class so that can coexist with other code, such as the flicker code.
  test: receive config into ram, only on success burn eeprom.
  test: instead of timeout on host abandoning program download allow a sequence of 5 '@' in a row to break device out of download.
  todo: add abort/disable input signal needed for recovering from false trigger just before a group arrives. Presently must use hardware reset and the startup delay is then an issue.
  todo: temporary mask via pin config, ie a group of 'bit bucket'

  Timer tweaking:
  The legacy technique of using a tweak to the millis/frame doesn't deal with temperature and power supply drift which, only with initial error.
  This is implemented while also allowing the frame time to be dynamically set to something other than 50ms.

  If certain steps need to be a precise time from other steps then use a good oscillator.
  In fact, twiddling the timer reload value of the hardware timer fixes the frequency issue for the whole application.
  todo: add external reference timer to FrameSynch class.

*/

///////////////////////////////////
//build options, things that we don't care to make runtime options
//FrameTweaking 1 enables emulation of OB's timing adjustment, 0 ignores OB's config.  (saves 1 byte config, 50 bytes rom. Might not be worth the lines of source code.
#ifndef FrameTweaking
#warning Enabling frame timing tweak module
FrameTweaking 1
#endif

#ifndef SAMPLE_END
#warning Defaulting sample storage space to 1000 bytes/ 500
#define SAMPLE_END 1000
#endif

#ifndef TTL_COUNT
@warning Setting number of controls to legacy value of 8
#define TTL_COUNT 8
#endif

//convert define into a real shared variable:
const int LedPinNumber =
#ifdef LED_BUILTIN
  LED_BUILTIN;
#else
  NaV;
#endif


const int NumControls = TTL_COUNT; //number of TTL outputs, an 8 here is why we call this 'Octobanger'
//eeprom memory split:

#include <EEPROM.h>
using EEAddress = uint16_t ;//todo: EEPROM.h has helper classes for what we are doing with this typedef

/////////////////////////////////
// different names for HardwareSerial depending upon board
// RealSerial is output for programmed device
// CliSerial is input for banger
#if defined(AVR_LEONARDO)
#define RealSerial Serial1
#define CliSerial SerialUSB
#else  //same as default: #elif defined(ARDUINO_AVR_NANO)
#define CliSerial Serial
#define RealSerial Serial
#endif

//for some platforms we must exclude D0 and D1 from controls.
#ifndef GuardRxTx
#if RealSerial != CliSerial
#define GuardRxTx 0
#else
#define GuardRxTx 2
#endif
#endif

////////////////////////////////////////////////////////////
//C++ language missing pieces:
//number of array elements, rather than sharing a #defined declaration and presuming it is used as its name suggests
#define arraySize(arrayname) sizeof(arrayname)/sizeof(*arrayname)
//for when an unsigned integer variable has no value, we use the least useful value:  (Not a Value)
#define NaV ~0U
////////////////////////////////////////////////////////////
#include "millievent.h"   //convenient tools for millis()
#include "chainprinter.h" //less verbose code in your module when printing, similar to BASIC's print statement. Makes for easy #ifdef'ing to remove debug statements.
#include "digitalpin.h"   //
#include "edgyinput.h"
#include "twiddler.h" //wiggle millis per frame to get an average close to some reference clock's opinion of what a millisecond is.

//////////////////////////////

namespace Octoid {

using VersionTag = byte[5]; //version info allocation

//still keeping old EEPROM layout for simpler cloning to existing hardware
enum EEAlloc {//values are stepping stone to reorganization.
  ProgStart = 0,
  ConfigurationStart = ProgStart + 2 * 500,  //legacy 500 samples for 1k EEProm
  ProgEnd = ConfigurationStart,
  ProgSize = ProgStart - ProgEnd,

  StampEnd = E2END + 1, //E2END is address of last byte
  StampStart = StampEnd - sizeof(VersionTag),
  StampSize = StampStart - StampEnd,

  ConfigurationEnd = StampStart,
  ConfigurationSize = ConfigurationEnd - ConfigurationStart
};

struct VersionInfo {
  bool ok = false;//formerly init to "OK" even though it is not guaranteed to be valid via C startup code.
  //version info, sometimes only first 3 are reported.
  static const VersionTag buff ;//avr not the latest c++ so we need separate init = {8, 2, 0, 2, 6}; //holder for stamp, 8 tells PC app that we have 8 channels

  bool verify() {
    for (int i = 0; i < sizeof(VersionTag); i++)  {
      if (EEPROM.read(StampStart + i) != buff[i]) {
        return false;
      }
    }
    return true;
  }

  void burn() {
    EEPROM.put(StampStart, buff);
  }

  void print(Print &printer, bool longform = false) {
    for (int i = 0; i < longform ? StampSize : 3; i++) {
      if (i) {
        printer.print(".");
      }
      printer.print(buff[i]);
    }
  }

  void report(ChainPrinter & printer, bool longform) { //else legacy format
    printer(F("OctoBanger TTL v"));
    print(printer.raw, longform); //false is legacy of short version number, leaving two version digits for changes that don't affect configuration
  }
};

static const VersionTag VersionInfo::buff = {TTL_COUNT, 64, 0, 0, 0};

//////////////////////////////////////////////////////////////
// abstracting output types to allow for things other than pins.
// first group is pins, so config values match legacy
// planning on I2C GPIO
// then SPI GPIO
// last group is interception point for custom modules:

/** linker hook for sequenced actions, 32 flavors available */
__attribute__((weak)) void octoid(unsigned pinish, bool action) {
  //declare a function in your code with the above signature without the 'weak' stuff and
  //it will get called with a value 0.31 and a boolean that can be inverted by the fx artist.
  //if you need any configuration you will have to make more major changes to the octoblast defined concept.
}

__attribute__((weak)) void octoidMore(EEAddress start, byte sized,  bool writeit) {
  //if writeit then EEPROM.put(start,*yourcfgobject);
  // else EEPROM.get(start,*yourcfgobject);
  //but yourcfgobject must be no bigger than sized.
}

//todo: compile time hook for configuration EEPROM allocation.

//////////////////////////////////////////////////////
//output descriptors for the 8 bits of program data, abused for trigger in and out as well.
struct Channel {
  struct Definition { //presently 4 categories with up to 32 members in each, probably overkill.
    unsigned index: 5; //value used by group
    unsigned group: 2; //derived from config code
    unsigned active: 1; //polarity to send for active
  } def;

  void operator = (bool activate) {
    bool setto = activate ? def.active : ! def.active  ;
    switch (def.group) {
      case 0://pin
        //guard tx/rx pins if needed.
        if (def.index < GuardRxTx || def.index == NaV) {
          //disabled channel, do nothing
        } else {
          digitalWrite(def.index, setto);
        }
        break;
      case 1://PCF858x (I2C)
        //todo: implement one
        break;
      case 2://MCP2xs17 SPI
        //todo: implement one
        break;
      case 3:
        octoid(def.index, setto);//hook for custom outputs
        break;
      default:
        break;
    }
  }

  void configure(Definition adef) {
    def = adef;
  }

};

/////////////////////////////////////////////////
struct FramingOptions {
  //measure frame via a program that outputs a known pulse then send us the int and 256*fract of the mills per frame.
  byte Millis;   //if clock is slow such as 49.123 ms per frame then make this 49 ...
#if FrameTweaking
  byte Fraction;  // ... and this  .123 * 256
  void fromLegacy(byte ancient) {
    if (ancient) { //if was non-zero then 2nd tweak was selected which had a value of 0.
      Millis = 50;
      Fraction = 0;
    } else {
      Millis = 49;
      Fraction = 256 * (1 - 0.405); //legacy tweak
    }
  }

  byte toLegacy() const {
    return Fraction == 0; //coincidentally produces the desired value.
  }
#else
  void fromLegacy(byte ancient) {
    Millis = 50;
  }

  byte toLegacy() const {
    return 1; //report always using the timing that was hardcoded as 50.0
  }
#endif


};

/////////////////////////////////////////////////
// this struct contains all config that is saved in EEPROM
struct Opts  {

    VersionInfo stamp;

    //the legacy EEPROM layout had 19 bytes of config of which 11 were used.
    //we are sticking with that at the moment and we are using 12 bytes so we have 7 spare.

    /** having obsoleted most of the configuration we still want to honor old programmer SW sending us a block of bytes.
        we will therefore interpret incoming bytes where we still have something similar

    */
    enum LegacyIndex {//the enum that they should have created ;)
      PIN_MAP_SLOT = 0,  //add 1002 to get offset in OctoBlaster_TTL.
      TTL_TYPES_SLOT,
      MEDIA_TYPE_SLOT,
      MEDIA_DELAY_SLOT,
      RESET_DELAY_SLOT,
      BOOT_DELAY_SLOT ,
      VOLUME_SLOT,
      TRIGGER_TYPE_SLOT ,
      TIMING_OFFSET_TYPE_SLOT ,
      MEM_SLOTS  //let the compile count for us
    };

    Channel::Definition output[NumControls];
    Channel::Definition triggerOut;
    Channel::Definition triggerIn;
    byte bootSeconds;
    byte deadbandSeconds;// like ResetDelay, time from last frame of sequence to new trigger allowed.
    FramingOptions frame;

  public:
    void save() const {
      EEPROM.put(EEAlloc::ConfigurationStart, *this);
    }

    void fetch() {
      EEPROM.get(EEAlloc::ConfigurationStart, *this);
    }

    /** from EEPROM to object IFFI vresion stamp verifies */
    bool read() {
      if (stamp.verify()) {
        fetch();
        return true;
      } else {
        return false;
      }
    }


    byte legacy(unsigned index) {
      byte packer = 0;
      switch (index) {
        case PIN_MAP_SLOT: //
          return 2;//custom config
        case TTL_TYPES_SLOT: //polarities packed
          for (unsigned i = arraySize(output); i-- > 0;) {
            auto pindef = output[i];
            packer |= (pindef.active ) << i;
          }
          return packer;
        case MEDIA_TYPE_SLOT:
        case MEDIA_DELAY_SLOT:
          return 0;//no longer supported
        case RESET_DELAY_SLOT:
          return deadbandSeconds;
        case BOOT_DELAY_SLOT:
          return bootSeconds;
        case VOLUME_SLOT:
          return 15;// a 0 annoys the old code
        case TRIGGER_TYPE_SLOT:
          return triggerIn.active;
        case TIMING_OFFSET_TYPE_SLOT:
          //0 was a value for a slow local clock, 1 for right on:
          return frame.toLegacy();
        default:
          return 0;
      }
    }

    void setLegacy(unsigned index, byte ancient) {
      switch (index) {
        case PIN_MAP_SLOT:
          if (ancient == 0) {
            for (unsigned i = arraySize(output); i-- > 0;) {
              auto pindef = output[i];
              pindef.index = i + 2; // 2 through 9 in order, that was convenient of them.
            }
            triggerOut.index = 10;
            triggerIn.index = 11;
            //media pin no longer builtint
          } else if (ancient == 1 ) {
            for (unsigned i = arraySize(output); i-- > 0;) {
              auto pindef = output[i];
              pindef.index = i >= 4 ? i + 4 : 7 - i; // { D7,  D6,  D5,  D4,  D8,  D9, D10, D11,    A0,    A1,    A2},
            }
            triggerOut.index = A1;
            triggerIn.index = A0;
            //media pin no longer builtin
          }
          break;
        case TTL_TYPES_SLOT: //polarities packed
          for (unsigned i = arraySize(output); i-- > 0;) {
            auto pindef = output[i];
            pindef.active  = bool(bitRead(ancient, index));
          } break;
        case MEDIA_TYPE_SLOT:
        case MEDIA_DELAY_SLOT:
          break;
        case RESET_DELAY_SLOT:
          deadbandSeconds = ancient;
          break;
        case BOOT_DELAY_SLOT:
          bootSeconds = ancient;
          break;
        case VOLUME_SLOT:
          break;
        case TRIGGER_TYPE_SLOT:
          triggerIn.active = ancient != 0;
          break;
        case TIMING_OFFSET_TYPE_SLOT:
          frame.fromLegacy(ancient);
          break;
      }
    }

    void reportPins(Print &printer, const char*header, bool polarity) {
      printer.print(header);
      for (unsigned i = 0; i < arraySize(output); ++i) {
        if (!i) {
          printer.print(",");
        }
        auto pindef = output[i];
        printer.print(polarity ? pindef.active : pindef.index);
      }
      printer.println();

    }

    void report(ChainPrinter & printer) {//legacy format
      printer(F("Reset Delay Secs: "), deadbandSeconds);
      if (bootSeconds != 0)  {
        printer(F("Boot Delay Secs: "), bootSeconds);
      }

      printer(F("Pinmap table no longer supported"));
      printer(F("Trigger Pin in: "), triggerIn.index);
      //this might be perfectly inverted from legacy"
      printer(F("Trigger Ambient Type: "), triggerIn.active ? F("Low (PIR or + trigger)") : F("Hi (to gnd trigger)"));

      printer(F("Trigger Pin Out: "), triggerOut.index); //todo:1 show extension of polarity

      reportPins(printer.raw, F("TTL PINS:  "), false);
      reportPins(printer.raw, F("TTL TYPES: "), true);
    }

} __attribute__((packed)); //packed happens to be gratuitous at the moment, but is relied upon by the binary configuraton protocol.




///////////////////////////////////////////
struct Trigger {
  //debouncer generalized:
  EdgyInput<bool> trigger;
  //polarity and pin
  ConfigurablePin triggerPin;
  //polarity and pin
  ConfigurablePin triggerOut;
  //timer for ignoring trigger, NaV is effectively forever, 49 days.
  OneShot suppressUntil;
  //timer for triggerOut pulse stretch
  OneShot removeout;

  //todo: add config for trigger in debounce time
  static const unsigned DEBOUNCE = 5 ; // how many ms to debounce
  //todo: add config for trigger out width
  static const unsigned OUTWIDTH = 159 ; //converts input edge to clean fixed width active low

  /** ignore incoming triggers for a while, if NaV then pratically forever. 0 might end up being 1 ms, but not guaranteed. */
  void suppress(unsigned shortdelay) {
    suppressUntil = shortdelay;
  }

  /** syntactic sugar for "I'm not listening"*/
  bool operator ~() {
    return suppressUntil.isRunning();
  }

  void setup(const Opts &O) {
    triggerPin.configure(O.triggerIn.index, O.triggerIn.active ? INPUT : INPUT_PULLUP, O.triggerIn.active);
    triggerOut.configure(O.triggerOut.index, OUTPUT, O.triggerOut.active);

    trigger.configure(DEBOUNCE);
    trigger.init(triggerPin);

    if (triggerPin)  {
      //todo: debate whether this is still needed at all since we now allow actions from host while sequencing.
      //an edge trigger would make this moot.
      suppress(2000);
    } else {
      suppressUntil = 0;//will go live on next tick, but not instantly
    }
  }

  /** @returns true while trigger is active */
  bool onTick() {
    if (removeout) {
      triggerOut = false;
    }
    trigger(triggerPin);//debounce even if not inspecting
    if (suppressUntil.isRunning()) {
      return false;
    }
    if (suppressUntil) {
      //here is where we would notify that we are now live
    }

    if (trigger) { //then we have a trigger
      //todo: do not repeat this until trigger has gone off.
      triggerOut = true;
      removeout = OUTWIDTH;
      return true;
    }
    return false;
  }

  MilliTick liveIn() const {
    return suppressUntil.due();
  }

};



/////////
//

#if FrameTweaking

/**
  To compensate for a slow processor oscillator occasionally remove a tick using standard PWM thinking.

  If we are only producing 99 frames when 100 were desired then we need to drop a frame's worth of millis every 99 frames.
  so we need to produce 50 adjustments in 99 frames which means bouncing between 49 and 50:
  49*50+ 50*50 = 99*50 ms in 100 frames.

  OctoBnger tweak was -0.4 ms per frame, 49.595 arduino millis()/frame, so our tick should be 49 plus 1 ~60% of the time.
  At present 8 bits of resolution on tweaking has been implemented, the limitation being in the configuration data allocation.
  For finer tweaking we can add more configuration bytes and get more precise than the stability of the oscillator over time and temperature.

  This whole clock adjust thing should be #ifdef'd.

*/


class FrameSynch {
    OneShot doNext;
  public:
    unsigned MILLIS_PER_FRAME;
    /** software pwm, generates a set of ones and zeroes that we add to the base amount.  */
    IntegerTwiddler tweak{0, 0};

    void operator =(bool startElseStop) {
      if (startElseStop) {
        doNext = 1; //will start with next millis tick
      } else {
        doNext = 0; //disables this kind of timer
      }
    }

    /** poll preferably at least every ms.
      @returns true if frame time is expired, and starts looking for next frame*/
    operator bool() {
      if (doNext) {//then a frame's worth of millis has passed since the last addignment to doNext.
        doNext = tweak(MILLIS_PER_FRAME); //always relative to NOW, not absolute start time
        return true;
      } else {
        return false;
      }
    }

    void setup(const FramingOptions &O) {
      MILLIS_PER_FRAME = O.Millis;
      tweak.setRatio(O.Fraction, 256 - O.Fraction); //the 256 comes from 2^(# of bits in fraction).
    }

    MilliTick nominal(uint32_t frames) const {
      return frames * MILLIS_PER_FRAME;
    };

};
#else
class FrameSynch {
  public:
    MonoStable MILLIS_PER_FRAME;//cheat, by using this name the config doesn't need to know which implementation of FrameSynch is active

    void operator =(bool startElseStop) {
      if (startElseStop) {
        MILLIS_PER_FRAME.start();
      } else {
        MILLIS_PER_FRAME.stop();
      }
    }

    operator bool() {
      return MILLIS_PER_FRAME.perCycle();//then a frame's worth of millis has passed since the last addignment to doNext.
    }

    void setup(const FramingOptions &O) {
      MILLIS_PER_FRAME = O.Millis;
    }


    MilliTick nominal(uint32_t frames) const {
      return frames * MilliTick(MILLIS_PER_FRAME);
    }

};
#endif

///////////////////////////////////
// a step towards having more than one sequence table in a processor.
/**
  if not recording then trigger() starts playback
  if are recording then trigger() starts recording with present state as first record

  To record:
  ensure .pattern is fresh (in case you don't debounce it while idle)
  set amRecoding (do not call startRecording())
  invoke trigger by whatever means you choose, such as the actual trigger or a cli "@T" or even calling trigger()
  while recording update .pattern

*/
struct Sequencer {
    Trigger &T;
    /** amount of time after end of program to ignore trigger */
    MilliTick deadtime;

    static const int SAMPLE_COUNT = EEAlloc::ProgSize / 2; //#truncating divide desired, do not round.

    Channel output[sizeof(Opts::output)];

    void Send(uint8_t packed) {
      for (unsigned bitnum = arraySize(output); bitnum-- > 0;) {
        output[bitnum] = bool(bitRead(packed, bitnum));//grrr, Arduino guys need to be more type careful.
      }
    }

    /** program/sequence step, a pattern and a duration in frames*/
    struct Step {
      byte pattern;
      byte frames;
      static const byte MaxFrames = 255; //might someday reserve 255 for specials as well as 0
      /** an array of objects in EEPROM starting at zero for convenience*/
      void get(unsigned nth) {
        EEPROM.get(nth * sizeof(Step), *this);
      }

      void put(unsigned nth) const {
        EEPROM.put(nth * sizeof(Step), *this);
      }

      //@returns whether this step is NOT the terminating step.
      operator bool()const {
        return frames != 0 || pattern != 0; //0 frames is escape, escape 0 is 'end of program'
      }

      /** set values to indicate program end */
      void stop() {
        frames = 0;
        pattern = 0;
      }

      void start(byte newpattern) {
        frames = 1;
        pattern = newpattern;
      }

      bool isStop() const {
        return frames == 0 && pattern == 0;
      }
      /**
        other potential 0 time commands:
        loop if trigger active (presently hard coded to do so)
        wait for trigger inactive (instead of edge trigger)
        mark 'here' as loop start else is zero

      */

    } __attribute__((packed));

    class Pointer: public Step {
        unsigned index = NaV;
      public:

        bool haveNext() const {
          return index < SAMPLE_COUNT ;
        }

        /** increment pointer then load members from EEPROM
          @returns whether a sample def was loaded, but it might be a stop marker. */
        bool next() { //works somewhat like sql resultset. I don't like that but it is cheap to implement.
          ++index;
          if (haveNext()) {
            get(index);
            return true;
          }
          return false;//ran off of the end of memory
        }

        /** gets value for nth step, and leaves point set to that, so next() will still be this one's value*/
        bool operator =(unsigned nth) {
          index = nth;
          if (haveNext()) {
            get(index);
            return !isStop();
          } else {
            stop();
            return false;
          }
        }

        /** @returns index of current values, NaV if out of storage range */
        operator unsigned() {
          return haveNext() ? index : NaV;
        }

        bool begin() {
          index = ~0;
          get(0);//peek
          return !isStop();
        }

        /** stores present value where we THINK it most likey came from. */
        void put() const {
          Step::put(index);
        }

        /** end a recording, preserving present value and generating a stop record */
        void end() {
          put();
          stop();
          ++index;//not using next() to not waste time with a get()
          if (haveNext()) {
            put();//write a stop to eeprom in case we quit recording before next frame gets us to some other Step::put
          }
        }

        /** updates curent record or moves to new one as needed.
          @returns whether it is safe to call record again. */
        bool record(byte newpattern) {
          if (pattern == newpattern  && frames < MaxFrames) {//can stretch current step
            ++frames;
            return true;//can still record
          }
          put();
          ++index;
          if (haveNext()) {
            start(newpattern);
            return true;
          }
          return false;
        }

    }; //end class Pointer

    Pointer playing;
    unsigned framecounter = NaV; //same value it will have when count is exhausted
    //frame ticks, which might be tracking an external signal.
    FrameSynch fs;

    //for recording
    bool amRecording = false;
    byte pattern;

    bool trigger() {
      fs = true; //next onTick will be very soon, we can wait for it before playing the step.
      framecounter = 0;
      return playing.begin(); //clears counters etc., reports whether there is something playable, does NOT apply the pattern
    }

    /** @returns whether a frame tick occured */
    bool onTick() {
      if (fs) {
        if (amRecording) {
          return record(pattern);//makes fs give us a true one frame time from now
        }
        if (framecounter-- > 0) {//still playing active frame
          return true;
        }
        while (playing.next()) { //if we haven't run off the end of the list
          if (playing.frames) {//if simple step
            Send(playing.pattern);
            framecounter = playing.frames - 1;//-1 because it will be a frame time before we get back into this code.
            return true;
          }
          //else pattern is a code that affects sequence, not outputs.
          switch (playing.pattern) {
            case 0: //stop with delay before next trigger allowed
              T.suppress(deadtime);
              break;//finish
            case 1://todo:f loop to mark if trigger still present
              break;
            case 2://todo:f mark location for loop while trigger
              break;
            default: //what do we do with invalid steps?
              break;
          }
        }
        finish();
        return false;
      }
    }

    void finish() {
      Send(0);
      fs = false;
    }

    /** @returns whether it is still recording. */
    bool record(byte pattern) {
      if (!playing) { //we should not be running
        return false;
      }
      if (playing.record(pattern)) { //still room to record
        return true;
      }
      endRecording();
      return false;
    }

    void endRecording() {
      playing.end();
      fs = false;
      amRecording = false;
      finish();
    }

    /** @returns total sequence time in number of frames , caches number of steps in 'used' */
    uint32_t duration(unsigned & used) const {
      uint32_t frames = 0;
      Pointer all;
      all.begin();
      while (all.next()) {
        frames += all.frames ;
      }
      used = all;
      return frames;
    }

    void configure(const Opts & O) {
      for (int i = sizeof(O.output); i-- > 0;) {
        output[i].configure(O.output[i]);
      }
      fs.setup(O.frame);
      deadtime = round(O.deadbandSeconds * 1000);
    }


    void reportFrames(ChainPrinter & printer) {
      unsigned used = 0;
      auto frames = duration(used);
      printer(F("Frame Count: "), used * 2); //legacy 2X
      MilliTick ms = fs.nominal(frames);
      unsigned decimals = ms % 1000;

      printer(F("Seq Len Secs: "), ms / 1000, decimals < 10 ? ".00" : decimals < 100 ? ".0" : ".", decimals);
    }

    Sequencer(Trigger &T): T(T) {}


};



///////////////////////////////////////////////////////
/** transmit program et al to another octoid
*/
struct Cloner {
  Stream *target = nullptr;
  //background sending
  EEAddress tosend = NaV;
  unsigned sendingLeft = 0;
  bool legacy = true;

  Opts &O; //lambda to get to one function needed was more expensive than a simple link (54/6).

  Cloner(Opts &O): O(O) {}

  /** sends the '@' the letter and depending upon letter might send one or two more bytes from more.
    @returns whether sequence was handed off to port. */
  bool sendCommand(char letter, unsigned more = NaV ) {
    unsigned also = 0;
    switch (letter) {
      case 'S': case 'U': also = 2; break;
      case 'M': also = 1; break;
    }
    if (target && target->availableForWrite() >= (2 + also)) { //avert delays between essetial bytes of a command, for old systems that did blocking reception
      target->write('@');
      target->write(letter);
      while (also-- > 0) {
        target->write(more);//little endian
        more >>= 8;
      }
      return true;
    } else {
      return false;
    }
  }

  /** send related prefix and set pointers for background sending */
  bool sendChunk(char letter, unsigned eeaddress, size_t sizeofthing) {
    if (!sendCommand(letter, sizeofthing)) {
      return false;
    }
    startChunk(eeaddress, sizeofthing) ;
    return true;
  }

  /** start background send of eeprom content */
  void startChunk(unsigned eeaddress, size_t sizeofthing) {
    tosend = eeaddress;
    sendingLeft = sizeofthing;
  }

  unsigned configSize() const {
    return legacy ? Opts::LegacyIndex::MEM_SLOTS : sizeof(Opts);
  }

  //user must confirm versions match before calling this
  bool sendConfig() {
    return sendChunk('U', EEAlloc::ConfigurationStart, configSize());
  }

  bool sendFrames() {
    return sendChunk('S', EEAlloc::ProgStart, EEAlloc::ProgSize);//send all, not just 'used'
  }

  //and since we are mimicing OctoField programmer:
  void sendTrigger() {
    sendCommand('T');
  }

  void sendPattern(byte pattern) {
    sendCommand('M', pattern);
  }

  /// call while
  void onTick() {
    if (!target) {
      return;
    }

    while (sendingLeft) {//push as much as we can into sender buffer
      //multiple of 4 at least as large as critical chunk:
      byte chunk[( max(sizeof(Opts), Opts::LegacyIndex::MEM_SLOTS) + 3) & ~3]; //want to send Opts as one chunk if not in legacy mode
      unsigned chunker = 0;
      if (legacy && tosend >= EEAlloc::ConfigurationStart && tosend < EEAlloc::ConfigurationEnd) { //if legacy format and sending config
        auto offset = tosend - ConfigurationStart;
        for (; chunker < sizeof(chunk) && chunker < sendingLeft; ++chunker) {
          chunk[chunker] = O.legacy(offset + chunker);
        }
      } else {
        for (; chunker < sizeof(chunk) && chunker < sendingLeft; ++chunker) {
          chunk[chunker] = EEPROM.read(tosend + chunker);
        }
      }
      auto wrote = target->write(chunk, chunker);
      tosend += wrote;
      sendingLeft -= wrote;
      if (wrote < chunker) { //then incomplete write, time to leave.
        break;
      }
    }
  }

};
////////////////////////////////////////////
// does not block, expectes its check() to be called frequently
struct CommandLineInterpreter {
  Sequencer &S;
  Opts O; //options are mostly part of the command interface.
  /** command protocol state */
  enum Expecting {
    At = 0,
    Letter,
    LoLength,
    HiLength,
    Datum
  } expecting;

  /** partially completed command, might linger after command */
  char pending;

  byte lowbytedata;
  //quantity received
  unsigned sofar;
  /** amount to receive */
  unsigned expected;

  /* in the binary of a program two successive steps with count of 64 and the same pattern is gratuitous, it can be merged into a single step
      with count of 128. As such the sequence @@@@@ will always contain two identical counts with identical patterns regardless of where in the
      pattern/count alternation we are.
  */
  struct Resynch {
    char keychar = '@';
    unsigned inarow = 0;
    static const unsigned enough = 5;
    bool isKey(char incoming)const {
      return incoming == keychar;
    }
    bool operator()(char incoming) {
      if (!isKey(incoming)) {
        inarow = 0;
        return false;
      }
      if ( ++inarow >= enough) {
        inarow = 0;
        return true;
      }
      return false;
    }
  } resynch;


  /** comm stream, usually a HardwareSerial but could be a SerialUSB or some custom channel such as an I2C slave */
  Stream &stream;

  ChainPrinter printer;

  Cloner cloner;

  CommandLineInterpreter (Stream &someserial, Sequencer &S): S(S), stream(someserial), printer(stream, true /* auto crlf*/) , cloner(O) {
    cloner.target = &someserial;
    cloner.legacy = true;
  }

  void onBadSizeGiven() {
    //todo: send error message
    clear_rx_buffer();
    expected = 0;
    expecting = At;
  }

  void clear_rx_buffer() {
    while (stream.available() != 0) {
      stream.read();
    }
  }


  /** @returns whether state machine is already set, else caller should set it to expect an At. */
  bool onLetter(char letter) {
    if (resynch.isKey(letter)) { // @@ is the same as a single @, recursively.
      expecting = Letter;
      return true; // ths is a bit hacky, but saves some code space
    }

    switch (letter) {
      case 'V': //return version
        O.stamp.print(stream);
        stream.println();
        break;
      case 'O': //is the stamp OK?
        printer(O.stamp.ok ? F("OK") : F("NO"));
        break;
      case 'H': //go hot
        S.T.suppress(0);
        printer(F("Ready"));
        break;
      case 'C': //go cold
        S.T.suppress(NaV);
        printer(F("Standing by..."));
        break;
      case 'T': //trigger test
        S.trigger();
        break;
      case 'D': //send program eeprom contents back to Serial
        //todo: if cloner is still busy either queue this up or ignore it
        cloner.startChunk(0, EEAlloc::ConfigurationStart); //todo: clip to frames used?
        break;
      case 'F': //send just the config eeprom contents back to Serial
        //todo: if cloner is still busy either queue this up or ignore it
        cloner.startChunk(EEAlloc::ConfigurationStart, cloner.configSize());
        break;
      case 'P': //ping back
        //todo: if not enough bytes available on output do not send anything
        O.stamp.report(printer, !cloner.legacy);
        S.reportFrames(printer);
        O.report(printer);
        break;
      case 'S':  //expects 16 bit size followed by associated number of bytes, ~2*available program steps
        [[fallthrough]] //        return true;//need more
      case 'U': //expects 16 bit size followed by associated number of bytes, ~9
        expecting = LoLength;
        return true;//need more
      case 'M': //expects 1 byte of packed outputs //manual TTL state command
        expecting = Datum;
        return true;//need more
      default:
        printer(F("unk char:"), letter);
        break;
    }
    //unless command needs more parameters prepare for the next one.
    pending = 0;
    expecting = At;
    return false;
  }

  /**
    usually returns quickly
    sometimes burns one EEProm byte
    at end of program load burns ~15.

  */

  void check() {
    unsigned int bytish = stream.read(); //returns all ones on 'nothing there', traditionally quoted as -1 but that is the only negative value returned so let us use unsigned.
    if (bytish != NaV) {
      switch (expecting) {
        case At:
          if (resynch.isKey(bytish)) {
            expecting = Letter;
          }
          break;
        case Letter:
          if (onLetter(bytish)) {//true when expecting to receive more related to the command
            pending = bytish;
            sofar = 0;
            expected = 0;
            //expecting was set in onLetter to LoLength or Datum
          }
          break;
        case LoLength:
          lowbytedata = bytish;
          expecting = HiLength;
          break;
        case HiLength:
          expected = word(bytish, lowbytedata);//maydo: treat key char as unreasonable high byte.
          switch (pending) {
            case 'S': //receive step datum
              if (expected > EEAlloc::ConfigurationStart) {
                printer(F("Unknown program size received: "), expected);
                onBadSizeGiven();
              }
              break;
            case 'U'://receive config flag
              if (expected != cloner.configSize()) {//our legacy local and remote are out of synch (or comm error)
                printer(F("Unknown config length passed: "), expected);
                onBadSizeGiven();
              }
              break;
          }
          expecting = Datum;
          break;
        case Datum: //incoming binary data has arrived
          if (resynch(bytish )) {//will not return true on single byte of 'M'
            expecting = At;
            return;
          }
          switch (pending) {
            case 'S': //receive step datum
              EEPROM.write(sofar++, bytish); //todo: receive to ram then burn en masse.
              if (sofar >= expected ) {//then that was the last byte
                expecting = At;
                printer(F("received "), sofar);
                S.reportFrames(printer);
                printer(F("Saved, Ready"));
              }
              break;
            case 'U'://receive config flag
              if (cloner.legacy) { //legacy parse
                O.setLegacy( sofar++, bytish);
              } else {
                reinterpret_cast<byte *>(&O)[sofar++] = bytish;
              }

              if (sofar == expected) {
                O.save();
                printer(F("Received "), expected, F(" config bytes"));
                setup();
              }
              break;
            case 'M':
              S.Send(bytish);
              expecting = At;
              break;
          }
          break;
      }
    }
  }

  //call at end of Arduino's setup()
  void setup() {
    //original did a clear_rx
    printer(F(".OBC")); //spit this back immediately, tells PC what it just connected to
  }

};


///////////////////////////////////////////////////////
const DigitalOutput BlinkerLed (LedPinNumber); //being outside of the Blinker class saves 16 bytes, wtf? Absolute addressing must be cheaper than base+offset!

struct Blinker {
  //names presume it will mostly be off.
  //todo: add polarity for whether idles off versus on
  OneShot offAt;//when will be done
  OneShot onAt; //delayed start

  void onTick() {
    if (offAt) {
      BlinkerLed = false;
    }
    if (onAt) {
      BlinkerLed = true;
    }
  }


  void pulse(unsigned on, unsigned off = 0) {
    /** reasoning: we set an off to guarantee pulse gets seen so expend that first.*/
    onAt = off;
    offAt = off + on;
    onTick();//in case we aren't delaying it at all.
  }

  /** @returns whether a pulse is in progress */
  operator bool() const {
    return offAt.isRunning() || onAt.isRunning();
  }

} blink;

//someone put a macro in global namespace, I don't want to rename my object so
#undef cli
///////////////////////////////////////////////////////
struct Blaster {
  Sequencer S;
  Trigger T;
  CommandLineInterpreter cli; //todo: allocate one each for USB if present and rx/tx.

  Blaster (): S(T), cli(CliSerial, S) {}

  void setup() {
    //read config from eeprom
    cli.O.stamp.ok = cli.O.read();
    cli.S.configure(cli.O);
    //apply idle state to hardware
    cli.S.Send(0);

    T.setup(cli.O);

    CliSerial.begin(115200);
    if (CliSerial != RealSerial) {
      RealSerial.begin(115200);
    }

    cli.setup();

    cli.O.report(cli.printer);//must follow S init to get valid duration

    if (cli.O.bootSeconds > 0)  {
      T.suppress(cli.O.bootSeconds * 1000);
    }

    if (~T) {
      cli.printer(F("Trigger will go hot in "), T.liveIn(), " ms");
    } else {
      cli.printer(F("Ready"));
    }
    cli.printer(F("Alive"));
  }

  void loop(bool ticked) {
    cli.check();//unlike other checks this guy doesn't need any millis, we want it to be rapid response

    if (MilliTicked.ticked()) { //once per millisecond, be aware that it will occasionally skip some
      blink.onTick();

      cli.cloner.onTick();//background transmission
      if (S.onTick()) { //active frame event.
        if (S.amRecording) {
          //here is where we update S.pattern with data to record.
        }
      }

      if (T.onTick()) {//trigger is active
        if (S.trigger()) {//ignores trigger being active while sequence is active
          cli.printer(F("Playing sequence..."));
        }
      }

      //if recording panel is installed:
      //todo: debounce user input into pattern byte.
      // a 16 channel I2C box comes to mind, 8 data inputs, record, save, and perhaps some leds for recording and playing

      if (~T) {//then it is suppressed or otherwise not going to fire
        if (!blink) {
          blink.pulse(350, 650);
        }
      }
    }

  }

} ;
}
//end of octoid, firmware that understands octobanger configuration prototocol and includes picoboo style button programming with one or two boards.
