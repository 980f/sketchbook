/*
   inspird by OctoBanger_TTL, trying to be compatible with their gui but might need a file translator.
   There were unnecessary blocking delays in many places.

  todo: debate whether suppressing the trigger locally should also suppress the trigger output, as it presently does
  note: trigger held active longer than sequence results in a loop.  Consider adding config for edge vs level trigger.
  todo: package into a single class so that can coexist with other code, such as the flicker code.
  todo: receive config into ram, only on success burn eeprom.
  test: instead of timeout on host abandoning program download allow a sequence of 5 '@' in a row to break device out of download.
  todo: abort/disable input signal needed for recovering from false trigger just before a group arrives.

  Timer tweaking:
  The legacy technique of using a tweak to the millis/frame doesn't deal with temperature and power supply drift which, only with initial error.

  If certain steps need to be a precise time from other steps then use a good oscillator.
  In fact, twiddling the timer reload value of the hardware timer fixes the frequency issue for the whole application.
  todo: finish up the partial implementation of FrameSynch class.

*/

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


////////////////////////////////////////////////////////////
//C++ language missing pieces:
#define arraySize(arrayname) sizeof(arrayname)/sizeof(arrayname[0])
//for when an integer has no value, we use the least useful value:  (Not a Value)
#define NaV ~0U
////////////////////////////////////////////////////////////
//tidbits of timer support to match 980f's milli services, eventually use those instead of inlining pieces here.
using MilliTick = decltype(millis());//unsigned long;

/** test and clear on a timer value */
bool timerDone(MilliTick &timer, MilliTick now) {
  if (timer && timer <= now) {
    timer = 0;
    return true;
  } else {
    return false;
  }

}

///////////////////////////
#include "chainprinter.h"   //less verbose code in your module when printing, similar to BASIC's print statement.

void reportFrames(ChainPrinter & printer);//IDE fails to make prototype
///////////////////////////
#include "digitalpin.h"

const DigitalOutput BlinkerLed(13); //board led

#include <EEPROM.h>
using EEAddress = uint16_t ;

//////////////////////////////

struct VersionInfo {
  bool ok = false;//formerly init to "OK" even though it is not guaranteed to be valid via C startup code.
  //version info, sometimes only first 3 are reported.
  static const byte buff[5];//avr not the latest c++ so we need separate init = {8, 2, 0, 2, 6}; //holder for stamp, 8 tells PC app that we have 8 channels


  //1019-1023 stamp
  static const EEAddress OFFSET = 1024 - sizeof(buff);// ~1019; //start byte of stamp


  bool verify() {
    for (int i = 0; i < sizeof(buff); i++)  {
      if (EEPROM.read(OFFSET + i) != buff[i]) {
        return false;
      }
    }
    return true;
  }

  void burn() {
    EEPROM.put(OFFSET, buff);
  }

  void print(Print &printer, bool longform = false) {
    for (int i = 0; i < longform ? sizeof(buff) : 3; i++) {
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

VersionInfo stamp;

const int TTL_COUNT = 8; //number of TTL outputs, an 8 here is why we call this 'Octobanger'
static const byte VersionInfo::buff[5] = {TTL_COUNT, 64, 0, 0, 0}; //holder for stamp, 8 tells PC app that we have 8 channels


//////////////////////////////////////////////////////////////
// abstracting output types to allow for things other than pins.
// first group is pins, so config values match legacy
// planning on I2C GPIO
// then SPI GPIO
// and then interception point for custom modules:

/** linker hook for sequenced actions, 32 flavors available */
__attribute__((weak)) void octoid(unsigned pinish, bool action) {
  //declare a function in your code with the above signature without the 'weak' stuff and
  //it will get called with a value 0.31 and a boolean that can be inverted by the fx artist.
  //if you need any configuration you will have to make more major changes to the octoblast defined concept.
}


using EEAddress = uint16_t ; //how is it necessary to repeat this? How does the prior definition go out of scope!?!?

__attribute__((weak)) void octoidMore(EEAddress start, byte sized,  bool writeit) {
  //if writeit then EEPROM.put(start,*yourcfgobject);
  // else EEPROM.get(start,*yourcfgobject);
  //but yourcfgobject must be no bigger than sized.
}

//todo: compile time hook for configuration EEPROM allocation.

//////////////////////////////////////////////////////
//output descriptors for the 8 bts of program data
struct Channel {
  struct Definition {
    unsigned index: 5; //value used by group
    unsigned group: 2; //derived from config code
    unsigned active: 1; //polarity to send for active
  } def;

  void operator = (bool activate) {
    bool setto = activate ^ def.active ;
    switch (def.group) {
      case 0://pin
        digitalWrite(def.index, setto);
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
// this struct contains all config that is saved in EEPROM
struct Opts  {
    static const EEAddress SAMPLE_END = 1000;//todo: subtract Opts size and config hooker size from stamp, truncate to even.
    //the legacy EEPROM layout had 19 bytes of config of which 11 were used.
    //we are sticking with that at the moment and we are using 12 bytes so we have 7 spare.

    /** having obsoleted most of the configuration we still want to honor old programmer SW sending us a block of bytes.
        we will therefore interpret incoming bytes where we still have something similar

    */
    enum LegacyIndex {//the enum that they should have created ;)
      PIN_MAP_SLOT = 0,
      TTL_TYPES_SLOT,
      MEDIA_TYPE_SLOT,
      MEDIA_DELAY_SLOT,
      RESET_DELAY_SLOT,
      BOOT_DELAY_SLOT ,
      VOLUME_SLOT,
      TRIGGER_TYPE_SLOT ,
      TIMING_OFFSET_TYPE_SLOT ,
      MEM_SLOTS
    };

    Channel::Definition output[TTL_COUNT];
    Channel::Definition triggerIn;
    Channel::Definition triggerOut;
    byte bootSeconds;
    byte deadbandSeconds;// like ResetDelay, time from last frame of sequence to new trigger allowed.

  public:
    void save() const {
      EEPROM.put(SAMPLE_END, *this);
    }

    void fetch() {
      EEPROM.get(SAMPLE_END, *this);
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
          for (int i = 0; i < TTL_COUNT; i++) {
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
          return 0; //we don't support turning a tweak on or off. Might recycle as external frame sync enable
        default:
          return 0;
      }
    }

    void setLegacy(unsigned index, byte ancient) {
      switch (index) {
        case PIN_MAP_SLOT: //
          break;
        case TTL_TYPES_SLOT: //polarities packed
          for (int i = 0; i < TTL_COUNT; i++) {
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
          break;
      }
    }

    void reportPins(Print &printer, const char*header, bool polarity) {
      printer.print(header);
      for (int i = 0; i < TTL_COUNT; i++) {
        if (!i) {
          printer.print(",");
        }
        auto pindef = output[i];
        printer.print(polarity ? pindef.active : pindef.index);
      }
      printer.println();

    }

    void report(ChainPrinter & printer) {//legacy format
      //      printer(F("OctoBanger TTL v"));
      //      stamp.print(printer.raw, false); //false is legacy of short version number, leaving two version digits for changes that don't affect configuration
      //
      //      reportFrames(printer);  //legacy had the sequencer table size cached on this object. This is inconvenient.

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

    //    void format_pin_print(uint8_t inpin, Stream & printer) {
    //      if (inpin >= A0) {
    //        printer.print("A");
    //        printer.println((inpin - A0));
    //      } else {
    //        printer.println(inpin);
    //      }
    //    }

} __attribute__((packed)) O; //packed happens to be gratuitous at the moment, but is relied upon by the binary configuraton protocol.



#include "edgyinput.h"

///////////////////////////////////////////
struct Trigger {
  //debouncer generalized:
  EdgyInput<bool> trigger;
  //polarity and pin
  ConfigurablePin triggerPin;
  //polarity and pin
  ConfigurablePin triggerOut;
  //timer for ignoring trigger, NaV is effectively forever, 49 days.
  MilliTick suppressUntil = 0;
  //timer for triggerOut pulse stretch
  MilliTick removeout = 0;

  //todo: add config for trigger in debounce time
  static const unsigned DEBOUNCE = 5 ; // how many ms to debounce
  //todo: add config for trigger out width
  static const unsigned OUTWIDTH = 159 ; //converts input edge to clean fixed width active low

  /** ignore incoming triggers for a while, if NaV then pratically forever. 0 might end up being 1 ms, but not guaranteed. */
  void suppress(unsigned shortdelay) {
    suppressUntil = shortdelay + ((shortdelay != NaV) ? millis() : 0);
  }

  /** syntactic sugar for "I'm not listening"*/
  bool operator ~() {
    return suppressUntil > 0;
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
      suppressUntil = 0;
    }
  }

  /** @returns true while trigger is active */
  bool check(MilliTick now) {
    if (timerDone(removeout , now) ) {
      triggerOut = 0;
    }
    trigger(triggerPin);//debounce even if not inspecting
    if (now < suppressUntil) {
      return false;
    }
    if (suppressUntil) {
      //here is where we would notify that we are now live
      suppressUntil = 0;
    }

    if (trigger) { //then we have a trigger
      //todo: do not repeat this until trigger has gone off.
      triggerOut = 1;
      removeout = now + OUTWIDTH;
      return true;
    }
    return false;
  }

  MilliTick liveIn() const {
    auto now = millis();
    return (suppressUntil > now) ? suppressUntil - now : 0;
  }

} T;

/////////
//

struct FrameSynch {
  static const float MILLIS_PER_FRAME = 50.0; //49.595; //default is 20 frames per sec (50 ms each)

  //step timer
  MilliTick start = 0;
  uint32_t frames = 0;

  MilliTick doNext = 0;

  /** millitick expected at given frame from sequence start */
  MilliTick operator()(uint32_t frame, MilliTick startingat) {
    return startingat + round(frame * MILLIS_PER_FRAME);//legacy issue: using round() makes minor changes to timing compared to legacy, with less cumulative error
  }

  float nominal(uint32_t frames) {
    return round(frames * MILLIS_PER_FRAME / 1000.0);
  }

  void begin(MilliTick now) {
    start = now;
    frames = 0;
  }

  bool frameDone( MilliTick now) {
    return doNext <= now;
  }

  void next(byte stepframes = 1) {
    frames += stepframes;
    doNext = (*this)(frames, start);
  }

  void stop() {
    doNext = 0;
  }

  FrameSynch() {}

};

////////////////////////////////////////////

/** saveConfig uses EEPROM.put which in turn only writes to the EEPROM when there is a change.
  As such it is cheap to call it when even only one member might have changed, it won't exhaust the EEPROM.
*/
void saveConfig() {
  O.save();
}

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

    static const int SAMPLE_COUNT = Opts::SAMPLE_END / 2;

    Channel output[sizeof(Opts::output)];

    void Send(uint8_t packed) {
      for (unsigned bitnum = TTL_COUNT; bitnum-- > 0;) {
        output[bitnum] = bool(bitRead(packed, bitnum));//grrr, Arduino guys need to be more type careful.
      }
    }
    struct Step {
      byte pattern;
      byte frames;
      static const byte MaxFrames = 255; //might reserve 255 for specials as well as 0

      void get(unsigned nth) {
        EEPROM.get(nth * 2, *this);
      }
      void put(unsigned nth) {
        EEPROM.put(nth * 2, *this);
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
        mark here as loop start else is zero

      */

    } __attribute__((packed));

    class Pointer: public Step {
        unsigned index = NaV;
      public:

        bool haveNext() const {
          return index < SAMPLE_COUNT ;
        }

        bool next() { //works like sql resultset. I don't like that but it is cheap to implement.
          if (haveNext() && !isStop()) {
            get(index++);
            return true;
          }
        }

        void operator =(unsigned nth) {
          index = nth;
          if (haveNext()) {
            get(index);
          } else {
            stop();
          }
        }

        /** @returns index of what next call to next() will load */
        operator unsigned() {
          return index;
        }

        bool begin() {
          index = 0;
          Step::get(0);//peek
          return !isStop();
        }

        void put() {
          //todo: need special case somewhere to put a stop at first location before we have begun.
          Step::put(index - 1); //save old
        }

        void end() {
          if (index > 0) {
            Step::put(index - 1);
            if (haveNext()) {//not past end
              stop();
              Step::put(index++);
            }
          } else {
            stop();
            Step::put(0);
          }
        }

        /** updates curent record or moves to new one as needed.
          @returns whether it is safe to call record again. */
        bool record(byte newpattern) {
          if (pattern == newpattern  && frames < MaxFrames) {//can stretch current step
            ++frames;
            return true;//can still record
          }

          Step::put(index - 1); //save old
          ++index;
          if (haveNext()) {
            start(newpattern);
            return true;
          }
          return false;
        }

    }; //end class Pointer

    Pointer playing;

    //moving frame timing logic into class that will later provide for watching an external clock source, not local millis()
    FrameSynch fs;


    //for recording
    bool amRecording = false;
    byte pattern;

    bool advance() {
      if (playing.next()) {
        Send(playing.pattern);
        fs.next(playing.frames);
        return true;
      } else {
        finish();
        fs.stop(); //deadband uses trigger suppression, it is not a sequencer timed step.
        return false;
      }
    }

    bool trigger(MilliTick now) {
      fs.begin(now);
      //todo:0 if external frame clock we must wait for it
      bool canPlay = playing.begin(); //clears counters etc., reports whether there is something playable
      if (amRecording) {//ignore canPlay
        playing.start(pattern); //todo: wait until fs gives us a first frame
        return true; //we are recording
      } else {
        return advance();//applies first step and computes time at which next occurs
      }
    }

    /** @returns whether a frame tick occured */
    bool check(MilliTick now) {
      if (fs.frameDone(now)) {
        if (amRecording) {
          return record(pattern);//makes fs give us a true one frame time from now
        } else {
          return advance();//makes fs give is a true after multiple frame times from now.
        }
      }
    }

    void finish() {
      Send(0);
      if (O.deadbandSeconds) {
        T.suppress(round(O.deadbandSeconds * 1000));
        return true;
      }
    }


    /** @returns where it is still recording. */
    bool record(byte pattern) {
      if (!playing) { //we should not be running
        return false;
      }
      if (playing.record(pattern)) { //still have room to alter present playing record
        fs.next();
      } else {
        endRecording();
      }
      return true;
    }

    void endRecording() {
      playing.end();
      fs.stop();//stops frame ticking
      amRecording = false;
      finish();
    }

    /** @returns total sequence time in number of frames , caches number of steps in 'used' */
    uint32_t duration(unsigned &used) {
      uint32_t frames = 0;
      Pointer all;
      all.begin();
      while (all.next()) {
        frames += all.frames ;
      }
      used = all;
      return frames;
    }

    void configure(const Opts &O) {
      for (int i = sizeof(O.output); i-- > 0;) {
        output[i].configure(O.output[i]);
      }
    }


    void reportFrames(ChainPrinter & printer) {
      unsigned used = 0;
      auto frames = duration(used);
      printer(F("Frame Count: "), used * 2); //legacy 2X
      printer(F("Seq Len Secs: "), fs.nominal(frames));
    }


} S;

///////////////////////////////////////////////////////
/** transmit program et al to another octoid
*/
struct Cloner {
  Stream *target = nullptr;
  //background sending
  EEAddress tosend = NaV;
  unsigned sendingLeft = 0;
  bool legacy = false; //todo: default true once true case is coded

  /** sends the '@' the letter and dependeing upon letter might send one or two more bytes from more */
  bool sendCommand(char letter, unsigned more = NaV ) {
    unsigned also = 0;
    switch (letter) {
      case 'S': case 'U': also = 2; break;
      case 'M': also = 1; break;
    }
    if (target && target->availableForWrite() >= also) { //avert delays between essetial bytes of a command, for old systems that did blocking reception
      target->write('@');
      target->write(letter);
      while (also-- > 0) {
        target->write(more);//little endian
        more >>= 8;
      }
    } else {
      return false;
    }
  }

  bool sendChunk(char letter, unsigned eeaddress, size_t sizeofthing) {
    if (!sendCommand(letter, sendingLeft)) {
      return false;
    }
    startChunk(eeaddress, sizeofthing) ;
    return true;
  }

  void startChunk(unsigned eeaddress, size_t sizeofthing) {
    tosend = eeaddress;
    sendingLeft = sizeofthing;
  }


  //user must confirm versions match before calling this
  bool sendConfig() {
    return sendChunk('U', O.SAMPLE_END, legacy ? Opts::LegacyIndex::MEM_SLOTS : sizeof(O));
  }

  bool sendFrames() {
    return sendChunk('S', 0, O.SAMPLE_END);//send all, not just 'used'
  }

  //and since we are mimicing OctoField programmer:
  void sendTrigger() {
    sendCommand('T');
  }

  void sendPattern(byte pattern) {
    sendCommand('M', pattern);
  }

  /// call while
  void onTick(MilliTick ignored) {
    if (!target) {
      return;
    }

    while (sendingLeft) {
      //multiple of 4 at least as large as given:
      byte chunk[(sizeof(Opts) + 3) & ~3]; //want to send Opts as one chunk if not in legacy mode
      unsigned chunker = 0;
      if (legacy && tosend >= O.SAMPLE_END && tosend < VersionInfo::OFFSET) {
        auto offset = tosend - O.SAMPLE_END;
        for (; chunker < sizeof(chunk) && chunker < sendingLeft; ++chunker) {
          chunk[chunker] = O.legacy(offset + chunker);
        }
        //todo: fill in chunk with legacy pattern
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

  /** background send of eeprom content */
  EEAddress sendingMemory = NaV;

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

  CommandLineInterpreter (Stream &someserial): stream(someserial), printer(stream, true) {
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

  bool onLetter(char letter) {
    switch (letter) {
      case 'V': //return version
        stamp.print(stream);
        stream.println();
        break;
      case 'O': //is the stamp OK?
        printer(stamp.ok ? F("OK") : F("NO"));
        break;
      case 'H': //go hot
        T.suppress(0);
        printer(F("Ready"));
        break;
      case 'C': //go cold
        T.suppress(NaV);
        printer(F("Standing by..."));
        break;
      case 'T': //trigger test
        S.trigger(millis());
        break;
      case 'D': //send program eeprom contents back to Serial
        tx_memory();
        break;
      case 'F': //send just the config eeprom contents back to Serial
        tx_config();
        break;
      case 'P': //ping back
        stamp.report(printer, !cloner.legacy);
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
        if (resynch.isKey(letter)) {
          expecting = Letter;
        } else {
          printer(F("unk char:"), letter);
          //        clear_rx_buffer();//todo: debate this, disallows resynch
        }
        break;
    }
    return false;
  }

  /**
    usually returns quickly
    sometimes burns one EEProm byte
    at end of program load burns ~15.

    reports can take a long time at present, will tend to that later.

  */

  void check() {
    unsigned int bytish = stream.read(); //returns all ones on 'nothing there', traditionally quoted as -1 but that is the only negative value returned so let us use unsigned.
    if (bytish != NaV) {
      switch (expecting) {
        case At:
          if (resynch.isKey(bytish )) {
            expecting = Letter;
          }
          break;
        case Letter:
          if (onLetter(bytish)) {
            pending = bytish;
            sofar = 0;
            expected = 0;
            //expecting was set in onLetter to LoLength or Datum
          } else {
            pending = 0;
            expecting = At;
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
              if (expected > O.SAMPLE_END ) {
                printer(F("Unknown program size received: "), expected);
                onBadSizeGiven();
              }
              break;
            case 'U'://receive config flag
              if (expected != cloner.legacy ? Opts::LegacyIndex::MEM_SLOTS : sizeof(O) ) {
                printer(F("Unknown config length passed: "), expected);
                onBadSizeGiven();
              }
              break;
          }
          expecting = Datum;
          break;
        case Datum: //incoming binary data has arrived
          if (resynch(bytish )) {//will not return true on single byte of 'M'
            expecting = Letter;
            sendingMemory = NaV;
            return;
          }
          switch (pending) {
            case 'S': //receive step datum
              EEPROM.write(sofar++, bytish); //todo: receive to ram then burn en masse.
              if (sofar >= expected ) {//then that was the last byte
                saveConfig();//deferred in case we bail out on receive
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
                saveConfig();
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

    if (sendingMemory < VersionInfo::OFFSET) {
      auto canSend = stream.availableForWrite();
      if (canSend) {
        while (canSend-- > 0) {
          stream.write(EEPROM.read(sendingMemory++));
          if (++sendingMemory >= VersionInfo::OFFSET) {
            break;
          }
        }
      }
    }

  }

  void tx_memory() {
    sendingMemory = 0;
  }

  void tx_config() {
    //todo; legacy format
    //    stream.write(SAMPLE_END, sizeof(O));
  }

  //called at end of setup
  void setup() {
    //original did a clear_rx
    printer(F(".OBC")); //spit this back immediately tells PC what it just connected to
  }

};

CommandLineInterpreter cli{CliSerial}; //todo: allocate one each for USB if present and rx/tx.


///////////////////////////////////////////////////////
struct Blinker {
  //names presume it will mostly be off.
  //todo: add polarity for whether idles off versus on
  MilliTick offAt;//when will be done
  MilliTick onAt; //delayed start

  void onTick(MilliTick now) {
    if (timerDone(offAt , now)) {
      BlinkerLed = false;
    }
    if (timerDone(onAt , now)) {
      BlinkerLed = true;
    }
  }

  void pulse(unsigned on, unsigned off = 0) {
    MilliTick now = millis();
    /** reasoning: we set an off to guarantee pulse gets seen so expend that first.*/
    onAt = now + off;
    offAt = onAt + on;
    onTick(now);//in case we aren't delaying it at all.
  }

  /** @returns whether a pulse is in progress */
  operator bool() const {
    return offAt == 0 && onAt == 0;
  }


} blink;



///////////////////////////////////////////////////////
/** send a break to a hareware serial port, which excludes USB */

struct LineBreaker {
  HardwareSerial *port = nullptr;
  uint32_t baud;
  unsigned txpin = NaV;

  MilliTick breakMillis;

  void attach(HardwareSerial &port, uint32_t baudafter, unsigned pin, unsigned overkill = 1) {
    txpin = pin;
    baud = baudafter;
    breakMillis = ceil((11000.0F * overkill) / baud);//11 bits and 1000 millis per second over bits per second
    this->port = &port;
  }

  MilliTick breakEnds = 0;
  MilliTick okToUse = 0;

  void engage(MilliTick now) {
    if (port && txpin != NaV) {
      port->end();
      pinMode(txpin, OUTPUT); //JIC serial.end() mucked with it
      digitalWrite(txpin, HIGH);
      breakEnds = now + breakMillis;
    }
  }

  /** @returns true once when break is completed*/
  bool onTick(MilliTick now) {
    if (timerDone(breakEnds , now)) {
      digitalWrite(txpin, LOW);
      port->begin(baud);
      okToUse = now + 1; //too convoluted to wait just one bit time
      return false;
    }
    return timerDone(okToUse , now);
  }

};

///////////////////////////////////////////////////////

void setup() {
  //read config from eeprom
  stamp.ok = O.read();
  S.configure(O);
  //apply idle state to hardware

  S.Send(0);

  T.setup(O);

  CliSerial.begin(115200);
  if (CliSerial != RealSerial) {
    RealSerial.begin(115200);
  }

  cli.setup();

  O.report(cli.printer);//must follow S init to get valid duration

  if (O.bootSeconds > 0)  {
    T.suppress(O.bootSeconds * 1000);
  }

  if (~T) {
    cli.printer(F("Trigger will go hot in "), T.liveIn(), " ms");
  } else {
    cli.printer(F("Ready"));
  }
  cli.printer(F("Alive"));
}

void loop() {
  auto now = millis();
  blink.onTick(now);

  cli.check();//unlike other checks this guy doesn't need any millis.
  cli.cloner.onTick(now);
  if (S.check(now)) { //active frame event.
    //new frame
    if (S.amRecording) {
      //here is where we update S.pattern with data to record.
      // a 16 channel I2C box comes to mind, 8 data inputs, record, save, and perhaps some leds for recirding and playing
    }
  }

  if (T.check(now)) {//trigger is active
    if (S.trigger(now)) {//ignores trigger being active while sequence is active
      cli.printer(F("Playing sequence..."));
    }
  }

  //if recording panel is installed:
  //tood: debounce user input into pattern byte.



  if (~T) {//then it is suppressed or otherwise not going to fire
    if (!blink) {
      blink.pulse(450, 550);
    }
  }
}
//end of octoid, firmware that understands octobanger configuration prototocol and includes picoboo style button programming with one or two boards.
