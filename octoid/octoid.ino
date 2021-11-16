/*
   inspird by OctoBanger_TTL, trying to be compatible with their gui but might need a file translator.
   There were unnecessary blocking delays in many places.

  todo: debate whether suppressing the trigger locally should also suppress the trigger output, as it presently does
  note: trigger held active longer than sequence results in a loop.  Consider adding config for edge vs level trigger.
  todo: package into a single class so that can coexist with other code, such as the flicker code.
  todo: receive program into ram, only on success burn eeprom. suppress operation during download OR use EEPROM in operation
  else: drop program buffer, so that other code has some resources. Present programming of EEPROM while running is not useful vs cost.
  todo: receive config into ram, only on success burn eeprom.
  test: instead of timeout on host abandoning program download allow a sequence of 5 '@' in a row to break device out of download.

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
#include "digitalpin.h"
#define BlinkerPin 13 //board led

#include <EEPROM.h>
using EEAddress = unsigned ;

//////////////////////////////
//MediaPlayer removed as it needs a bunch of rework and we don't actually have units with them present.
//we are going to abstract outputs and a reworked non-blocking player can be installed with a virtual boolean to trigger it.
//this frees up config space for per-unit maps without compiling, which is handy for dealing with burned out relays without redoing the program.

//////////////////////////////////////////////////////////////

struct VersionInfo {
  bool ok = false;//formerly init to "OK" even though it is not guaranteed to be valid via C startup code.
  //version info, sometimes only first 3 are reported.
  static const byte buff[5];//avr not the latest c++ so we need separate init = {8, 2, 0, 2, 6}; //holder for stamp, 8 tells PC app that we have 8 channels

  bool verify(EEAddress eeaddress) {
    for (int i = 0; i < sizeof(buff); i++)  {
      if (EEPROM.read(eeaddress++) != buff[i]) {
        return false;
      }
    }
    return true;
  }

  void burn(EEAddress eeaddress) {
    for (int i = 0; i < sizeof(buff); i++) {
      EEPROM.write(eeaddress++, buff[i]);
    }
  }

  void print(Stream &printer, bool longform = false) {
    for (int i = 0; i < longform ? sizeof(buff) : 3; i++) {
      if (i) {
        printer.print(".");
      }
      printer.print(buff[i]);
    }
  }

};

VersionInfo stamp;

const int TTL_COUNT = 8; //number of TTL outputs, an 8 here is why we call this 'Octobanger'
static const byte VersionInfo::buff[5] = {TTL_COUNT, 64, 0, 0, 0}; //holder for stamp, 8 tells PC app that we have 8 channels


//////////////////////////////////////////////////////////////
// abstracting output types:
// 00..7F indentify mechanism
// FF..80 invert activity level (1's complement)


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
      //todo: compile time plugin module of some sort
      //such as the legacy audio player which gets a pulse to fire it up.
      default:
        break;
    }
  }

  void configure(Definition adef) {
    def = adef;
  }

  byte code() const {
    return *reinterpret_cast<const byte *>(&def);
  }

};


/////////////////////////////////////////////////
// this struct contains all config that is saved in EEPROM
struct Opts  {
    static const EEAddress SAMPLE_END = 1000;

    //1019-1023 stamp
    static const EEAddress STAMP_OFFSET = 1024 - sizeof(stamp.buff);// ~1019; //start byte of stamp


    /** having obsoleted most of the configuration we still want to honor old programmer SW sending us a block of bytes.
        we will therefore interpret incoming bytes where we still have something similar
    */
    enum LegacyIndex {OutPins = 0, OutPolarities = 1, ResetDelay = 4, BootDelay = 5, TriggerPolarity = 7,};

    byte bootSeconds;
    byte deadbandSeconds;// like ResetDelay, time from last frame of sequence to new trigger allowed.

    Channel::Definition triggerIn;
    Channel::Definition triggerOut;
    Channel::Definition output[TTL_COUNT];

    //5 bytes left with legacy eeprom layout

  public:
    void save() const {
      EEPROM.put(SAMPLE_END, *this);
    }

    void fetch() {
      EEPROM.get(SAMPLE_END, *this);
    }


    bool read() {
      if (stamp.verify(STAMP_OFFSET)) {
        fetch();
        return true;
      } else {
        return false;
      }
    }

    void report(Stream & printer) {//legacy format
      printer.print(F("OctoBanger TTL v"));
      stamp.print(printer, false); //false is legacy of short version number, leaving two version digits for changes that don't affect configuration
      reportFrames(printer);

      printer.print(F("Reset Delay Secs: "));
      printer.println(deadbandSeconds);
      if (bootSeconds != 0)  {
        printer.print(F("Boot Delay Secs: "));
        printer.println(bootSeconds);
      }

      printer.println(F("Pinmap table no longer supported"));
      printer.print(F("Trigger Pin in: "));
      format_pin_print(triggerIn.index, printer);
      printer.print(F("Trigger Ambient Type: "));
      if (triggerIn.active) {//this might be perfectly inverted from legacy
        printer.println(F("Low (PIR or + trigger)"));
      } else {
        printer.println(F("Hi (to gnd trigger)"));
      }
      printer.print(F("Trigger Pin Out: "));
      format_pin_print(triggerOut.index, printer);

      printer.print(F("TTL PINS:  "));
      for (int i = 0; i < TTL_COUNT; i++) {
        if (!i) {
          printer.print(",");
        }
        printer.print(output[i].index);
      }
      printer.println();

      printer.print(F("TTL TYPES: "));
      for (int i = 0; i < TTL_COUNT; i++) {
        if (!i) {
          printer.print(",");
        }
        printer.print(output[i].active);
      }
      printer.println();
    }

    void format_pin_print(uint8_t inpin, Stream & printer) {
      if (inpin >= A0) {
        printer.print("A");
        printer.println((inpin - A0));
      } else {
        printer.println(inpin);
      }
    }

} __attribute__((packed)) O; //packed happens to be gratuitous at the moment, but is relied upon by the binary configuraton protocol.



#include "edgyinput.h"

///////////////////////////////////////////
struct Trigger {
  bool IS_HOT;
  //debouncer generalized:
  EdgyInput<bool> trigger;
  //polarity and pin
  ConfigurablePin triggerPin;
  //polarity and pin
  ConfigurablePin triggerOut;
  //timer for ignoring trigger
  MilliTick suppressUntil = 0;
  //timer for triggerOut pulse stretch
  MilliTick removeout = 0;

  //todo: add config for trigger in debounce time
  static const unsigned DEBOUNCE = 5 ; // how many ms to debounce
  //todo: add config for trigger out width
  static const unsigned OUTWIDTH = 159 ; //converts input edge to clean fixed width active low

  /** ignore incoming triggers for a while. */
  void suppress(unsigned shortdelay) {
    suppressUntil = millis() + shortdelay;
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
      suppress(30000); //this will give the user 30 seconds to upload a different config, such as one with a different polarity trigger,
      //before getting stuck in an endless trigger loop. (which would not be a problem if commands were not blocked while running.)
      CliSerial.println(F("Trigger detected on startup-"));
      CliSerial.println(F("Going cold for 30 secs"));
    } else {
      suppressUntil = 0;
    }
    IS_HOT = true;
  }

  /** @returns true while trigger is active */
  bool check(MilliTick now) {
    if (timerDone(removeout , now) ) {
      triggerOut = 0;
    }
    if (!IS_HOT) {
      return false;
    }
    trigger(triggerPin);//debounce even if not inspecting
    if (now < suppressUntil) {
      return false;
    }
    if (suppressUntil) {
      suppressUntil = 0;
      CliSerial.println(F("Ready for Trigger"));
    }

    if (trigger) { //then we have a trigger
      //todo: do not repeat this until trigger has gone off.
      triggerOut = 1;
      removeout = now + OUTWIDTH;
      return true;
    }
    return false;
  }
} T;

/////////
//

struct FrameSynch {
  static const float MILLIS_PER_FRAME = 50.0; //49.595; //default is 20 frames per sec (50 ms each)
  /** millitick expected at given frame from sequence start */
  MilliTick operator()(uint32_t frame, MilliTick startingat) {
    return startingat + round(frame * MILLIS_PER_FRAME);//legacy issue: using round() makes minor changes to timing compared to legacy, with less cumulative error
  }

  float nominal(uint32_t frames) {
    return round(frames * MILLIS_PER_FRAME / 1000.0);
  }

  FrameSynch() {}

};

//IDE failed to make a prototype for this:
void reportFrames(Stream &printer);


FrameSynch fs;

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

    //step timer
    MilliTick start = 0;
    uint32_t frames = 0;

    MilliTick doNext = 0;

    //for recording
    bool amRecording = false;
    byte pattern;

    bool advance() {
      if (playing.next()) {
        Send(playing.pattern);
        frames += playing.frames;
        doNext = fs(frames, start);
        return true;
      } else {
        finish();
        doNext = 0; //deadband uses trigger suppression, it is not a sequencer timed step.
        return false;
      }
    }

    bool trigger(MilliTick now) {
      start = now; //whether recording or playing
      frames = 0; //part of frame synch

      bool canPlay = playing.begin(); //clears counters etc., reports whether there is something playable
      if (amRecording) {//ignore canPlay
        playing.start(pattern);
        doNext = fs(frames, start);
        return true; //we are recording
      } else {
        return advance();//applies first step and computes time at which next occurs
      }
    }

    /** @returns whether a frame tick occured */
    bool check(MilliTick now) {
      if (timerDone(doNext , now)) {
        if (amRecording) {
          return record(pattern);
        } else {
          return advance();
        }
      }
    }


    void finish() {
      Send(0);
      if (O.deadbandSeconds) {
        T.suppress(round(O.deadbandSeconds * 1000));
        CliSerial.print(F("Waiting delay secs: "));
        CliSerial.println(O.deadbandSeconds);
      } else {
        CliSerial.println(F("Sequence complete, Ready"));
      }
    }


    /** @returns where it is still recording. */
    bool record(byte pattern) {
      if (!playing) { //we should not be running
        return false;
      }
      if (playing.record(pattern)) { //still have room to alter present playing record
        doNext = fs(++frames, start);
      } else {
        endRecording();
      }
      return true;
    }

    void endRecording() {
      playing.end();
      doNext = 0;//this ends recording
      amRecording = false;
      finish();
    }

    /** @returns total sequence time in number of frames , caches number of steps in 'used' */
    uint32_t duration(unsigned &used) {
      frames = 0; //1000 samples, 255 frames each possible
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

} S;

////////////////////////////////////////////
void reportFrames(Stream & printer) {
  unsigned used = 0;
  auto duration = S.duration(used);
  printer.print(F("Frame Count: "));
  printer.println(used * 2); //legacy 2X

  printer.print(F("Seq Len Secs: "));
  printer.println(fs.nominal(duration));
}

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

  /** comm stream, usually a HardwareSerial but could be a SerialUSB or some custom channel such as an I2C slave */
  Stream &stream;

  CommandLineInterpreter (Stream &someserial): stream(someserial) {}

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
        stream.println(stamp.ok ? F("OK") : F("NO"));
        break;
      case 'H': //go hot
        T.IS_HOT = true;
        stream.println(F("Ready"));
        break;
      case 'C': //go cold
        T.IS_HOT = false;
        stream.println(F("Standby..."));
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
        O.report(stream);
        break;
      case 'S':  //expects 16 bit size followed by associated number of bytes, ~2*available program steps
        return true;//need more
      case 'U': //expects 16 bit size followed by associated number of bytes, ~9
        return true;//need more
      case 'M': //expects 1 byte of packed outputs //manual TTL state command
        return true;//need more
      case '@':
        break;
      default:
        stream.print(F("unk char:"));
        stream.print(letter);
        //        clear_rx_buffer();//todo: debate this, disallows resynch
        break;
    }
    return false;
  }

  /* in the binary of a program two successive steps with count of 64 and the same pattern is gratuitous, it can be merged into a single step
      with count of 128. As such the sequence @@@@@ will always contain two identical counts with identical patterns regardless of where in the
      pattern/count alternation we are.
  */
  unsigned resynch = 0;
  static const unsigned Resunk = 5;


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
          if (bytish == '@') {
            expecting = Letter;
          }
          break;
        case Letter:
          if (onLetter(bytish)) {
            pending = bytish;
            sofar = 0;
            expected = 0;
            switch (pending) {
              case 'S':
              case 'U':
                expecting = LoLength;
                break;
              case 'M':
                expecting = Datum;
                break;
            }
          } else {
            expecting = At;
          }
          break;
        case LoLength:
          lowbytedata = bytish;
          expecting = HiLength;
          break;
        case HiLength:
          expected = word(bytish, lowbytedata);
          switch (pending) {
            case 'S': //receive step datum
              if (expected > 1000 ) {
                stream.print(F("Unknown program size received: "));
                stream.println(expected);
                onBadSizeGiven();
              }
              break;
            case 'U'://receive config flag
              if (expected != sizeof(O) ) {
                stream.print(F("Unknown config length passed: "));
                stream.println(expected);
                onBadSizeGiven();
              }
              break;
          }
          expecting = Datum;
          break;
        case Datum: //incoming binary data has arrived
          if (bytish == '@') {
            if (++resynch >= Resunk) {
              resynch = 0;
              expecting = At;
              sendingMemory = NaV;
              return;
            }
          } else {
            resynch = 0;
          }
          switch (pending) {
            case 'S': //receive step datum
              EEPROM.write(sofar++, bytish); //todo: receive to ram then burn en masse.
              if (sofar >= expected ) {//then that was the lat byte
                saveConfig();//deferred in case we bail out on receive
                expecting = At;
                stream.print(F("received "));
                stream.print(sofar);
                reportFrames(stream);
                stream.println(F("Saved, Ready"));
              }
              break;
            case 'U'://receive config flag
              if (expected == 9) { //legacy parse
                switch (sofar) {

                    break;
                }
              } else {
                reinterpret_cast<byte *>(&O)[sofar++] = bytish;
              }

              if (sofar == expected) {
                saveConfig();

                stream.print(F("Received "));
                stream.print(expected);
                stream.println(F(" config bytes"));
                //                stream.println(F("Please reconnect"));
                //                if (!stamp.ok)  {
                //                  O.hi_sample = 0;
                //                  saveConfig();
                //                }
                //                stamp.burn(STAMP_OFFSET);
                //                stamp.ok = O.read();
                //                set_ambient_out();
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

    if (sendingMemory < O.STAMP_OFFSET) {
      auto canSend = stream.availableForWrite();
      if (canSend) {
        while (canSend-- > 0) {
          stream.write(EEPROM.read(sendingMemory++));
          if (++sendingMemory >= Opts::STAMP_OFFSET) {
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
    //    stream.write(1000, sizeof(O));
  }

  //called at end of setup
  void setup() {
    //original did a clear_rx
    stream.println(F(".OBC")); //spit this back immediately tells PC what it just connected to
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
      digitalWrite(BlinkerPin, LOW);
    }
    if (timerDone(onAt , now)) {
      digitalWrite(BlinkerPin, HIGH);
    }
  }

  void pulse(unsigned on, unsigned off = 0) {
    MilliTick now = millis();
    /** reasoning: we set an off to guarantee pulse gets seen so expend that first.*/
    onAt = now + off;
    offAt = now + on + off;
    onTick(now);//in case we aren't delaying it at all.
  }

  /** @returns whether a pulse is in progress */
  operator bool()const {
    return offAt == 0 && onAt == 0;
  }

  void setup() {
    pinMode(BlinkerPin, OUTPUT);
    digitalWrite(BlinkerPin, LOW);
  }

} blink;

///////////////////////////////////////////////////////
/** transmit progam et al to another octoid

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
    tosend = eeaddress;
    sendingLeft = sizeofthing;
    if (!sendCommand(letter, sendingLeft)) {
      sendingLeft = 0;
      return false;
    }
    return true;
  }


  //user must confirm versions match before calling this
  bool sendConfig() {
    //todo: legacy export
    if (legacy) {
      return false; //big issue: can't send from EEPROM as we have modified content, must intercept in sender
    }
    return sendChunk('U', 1000, sizeof(O));//todo: symbol
  }

  bool sendFrames() {
    return sendChunk('S', 0, 1000);//send all, not just 'used'
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
      byte chunk[sizeof(Opts)];//want to send Opts as one chunk is not in legacy mode
      unsigned chunker = 0;
      if (legacy && tosend >= 1000) {
        //todo: fill in chunk with legacy pattern
      } else {
        for (; chunker < sizeof(chunk) && chunker < sendingLeft; ++chunker) {
          chunk[chunker] = EEPROM.read(tosend + chunker);
        }

      }
      auto wrote = target->write(chunk, chunker);
      tosend += wrote;
      sendingLeft -= wrote;
    }

  }

};

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

  T.setup( O);
  blink.setup();


  CliSerial.begin(115200); //we talk to the PC at 115200
  if (CliSerial != RealSerial) {
    RealSerial.begin(115200); //we talk to the PC at 115200
  }

  cli.setup();

  O.report(cli.stream);//must follow S init to get valid duration

  if (O.bootSeconds > 0)  {
    T.suppress(O.bootSeconds * 1000);
  } else {
    cli.stream.println(F("Ready"));
  }
  cli.stream.println(F("Alive"));
}

void loop() {
  auto now = millis();
  blink.onTick(now);

  cli.check();//unlike other checks this guy doesn't need any millis.
  if (S.check(now)) { //active frame event.
    //new frame
    if (S.amRecording) {
      //here is where we update S.pattern with data to record.
      // a 16 channel I2C box comes to mind, 8 data inputs, record, save, and perhaps some leds for recirding and playing
    }
  }

  if (T.check(now)) {//trigger is active
    if (S.trigger(now)) {//ignores trigger being active while sequence is active
      cli.stream.println(F("Playing sequence..."));
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
