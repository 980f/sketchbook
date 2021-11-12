/*
   reworked for best practices of 2021 by github/980f (Andy H.)

   ) there were unnecessary blocking delays in many places. Then there is code to try to get back in synch with any host. This is beyond simple remediation.
   ) delays in Serial output are badly done, see https://www.arduino.cc/reference/en/language/functions/communication/serial/availableforwrite/ for how to check whether you are overrunning serial. Using blocking delays to block for a fixed time whether or not one needs to delay at all is just stupid.
   ) volatile is only needed if interrupt service routines manipulate the same multibyte values as code called from setup() or loop(). There is none so all volatiles are superfluous.
   ) factored the crap out of the code to make variable names simpler and as preparation for making it a module that can be used from other programs.

  todo: the two trigger outputs have gotten messed up, need to read original code to fix 'em
  note: a suppressed trigger suppresses the trigger out.
  note: trigger held active results in a loop.  Consider adding config for edge vs level trigger.
  todo: package into a single class so that can coexist with other code, such as the flicker code.
  todo: receive program into ram, only on success burn eeprom. suppress operation during download OR use EEPROM in operation
  todo: receive config into ram, only on success burn eeprom.
  todo: restore timeout on host abandoning program download. Even better is to setup a resynch sequence such as 4 '@' in a row to break out of a download without some timeout

  Timer tweaking:
  The legacy technique of using a tweak to the millis/frame doesn't deal with temperature and power supply drift which, only with initial error.

  If certain steps need to be a precise time from other steps then use a good oscillator.
  In fact, twiddling the timer reload value of the hardware timer fixes the frequency issue for the whole application.
  todo: finish up the partial implementation of FrameSynch class.


  Original file header, downloaded 12nov2021
*************Octo Banger TTL*********
  01/21/2017 this is a Banger with 8 channels.
  Output is targeted at simple TTL for 2 of the the 4 relay modules
*** 01/22/2017 Adding frame-based state data storage per Joe AKA wickedbeernut

  I am going to start by fixing FPS at 20fps, or 50ms per frame.
  frame state data is going to be stored in pairs, where:
  1) byte 1 represents the state byte (like always)
  2) byte 2 represents the delay (in number of frames) to persist the current state
    before moving on to the next pair
  The above approach will also free up another byte of eeprom that represents FPS, since
  it is now fixed.

		//20170122 MJN testing with a metronome track tonight is revealing an innacuracy in
		// the 50 ms per frame deal. Maybe a flaw with millis() or timer 0?
		//Anyway, using 49.595 below seems to make it dead on 20fps
		//changed from 49.6 to 49.595 based on this post: http://www.avrfreaks.net/forum/uno-clock-running-slow
		//user Gavin Andrews measured with a scope and found this:
		//Firstly I realised that it wasn't running fast it was running slow... If a real worldworld 60 seconds measures as 59.514 seconds Arduino time that must mean the Arduino clock is SLOW not fast.
        //They concluded that this may be a clone issue.  If this is the case, should I allow
		// for configurable compensation?
  THE CONSTANT MILLIS_PER_FRAME IS NOW A FLOAT TO COMPENSATE FOR THE ABOVE.
  TWEAK IF YOUR TIMING IS OFF.
  09/04/2018 - K. Short:  Changed type declaration of variable "sample_ticker" from "uint8_t" to " unsigned int" to ensure
                        play_sequence exits (endless looping). For example, if "O.hi_sample" is 150, it will wrap the variable
                        (upper bound was 254) and never exit the "while" scare sequence!
*/
#define arraySize(arrayname) sizeof(arrayname)/sizeof(arrayname[0])

//tidbits of timer support to match 980f's milli services, eventually use those instead of inlining pieces here.
using MilliTick = unsigned long;

#define BlinkerPin 13 //board led

#include <EEPROM.h>


//////////////////////////////

#if __has_include("MiniMedia.h")
#include "MiniMedia.h"  //uses low level avr asm code to make a software uart. Needs some help with indentation ;)
#else  //stub the class. //todo: remove totally once we generalize outputs.
struct MiniMedia {  //stub class.
  void Init(...) {}
  void PlayAmbient() {}
  void ReportConfig() {}
  void PlayScare() {}
};
#endif

//some kind of audio player:
MiniMedia audio;
//////////////////////////////////////////////////////////////

struct VersionInfo {
  bool ok = false;//formerly init to "OK" even though it is not guaranteed to be valid via C startup code.
  bool had_bad_pin_map = false;
  //version info, sometimes only first 3 are reported.
  static const byte buff[5];//avr not the latest c++ so we need separate init = {8, 2, 0, 2, 6}; //holder for stamp, 8 tells PC app that we have 8 channels

  bool verify(unsigned eeaddress) {
    for (int i = 0; i < sizeof(buff); i++)  {
      if (EEPROM.read(eeaddress++) != buff[i]) {
        return false;
      }
    }
    return true;
  }

  void burn(unsigned eeaddress) {
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
static const byte VersionInfo::buff[5] = {8, 2, 0, 2, 6}; //holder for stamp, 8 tells PC app that we have 8 channels


//1019-1023 stamp
static const int STAMP_OFFSET = 1024 - sizeof(stamp.buff);// ~1019; //start byte of stamp

//////////////////////////////////////////////////////////////
//replaces: #include "PinMaps.h"
// 8 = 8 channels
const int TTL_COUNT = 8; //number of TTL outputs, an 8 here is why we call this 'Octobanger'

using Pinset = byte[11];
static const Pinset PIN_MAPS[] = {
  { 2, 3, 4, 5, 6, 7, 8, 9,
    11, 10, 12
  },
  { 7, 6, 5, 4, 8, 9, 10, 11,
    A0, A1, A2
  },  // Shield
#if __has_include("my_pinsets.h")
  {
#include "my_pinsets.h"
  };
#endif
};
const unsigned PIN_MAP_COUNT = arraySize(PIN_MAPS); //default, shield , custom


struct PinMappings {
  static const int TRIGGER_PIN_COUNT = 3; //in , daisy chain, audio

  int out(unsigned which) const {
    return PIN_MAPS[current][which];
  }

  int trigger(unsigned which) const {
    return PIN_MAPS[current][which + TTL_COUNT];
  }

  byte selectPins(byte preferred) {
    if (preferred > (PIN_MAP_COUNT - 1))  {
      return current = 0; //revert to default on a bad setting
    }
    //if someone sets to custom, and the custom pin buffer is not set to a legitimate pin, revert to default
    for (int i = 0; i < TTL_COUNT; i++) {
      auto pincode = PIN_MAPS[preferred ][i];
      //allow D2 thru A7 (A6 and A7 exist on the nano)
      if ((pincode < 2) || (pincode > A7)) { //2 should probably be D2, this is an attempt to protect the serial port.
        return current = 0;
      }
    }
    return current = preferred;
  }

  mutable byte current = 0;

  void setModes() const {
    for (int i = 0; i < TTL_COUNT; i++) {
      pinMode(out(i), OUTPUT);
    }

    pinMode(trigger(2), OUTPUT); //always low active audio
  }

};

const PinMappings pin;


///////////////////////////////////////////
struct TriggerInput {
  bool highactive;
  bool IS_HOT;
  bool lastStable;
  unsigned changing;
  unsigned inpin;
  unsigned outpin;
  MilliTick suppressUntil;
  MilliTick lastChecked;
  MilliTick removeout;

  //todo: add config for trigger in debounce time
  static const unsigned DEBOUNCE = 5 ; // how many ms to debounce
  //todo: add config for trigger out width
  static const unsigned OUTWIDTH = 159 ; //converts input edge to clean fixed width active low

  /** ignore incoming triggers for a while. */
  void suppress(unsigned shortdelay) {
    suppressUntil = millis() + shortdelay;
  }

  /** syntactic suga for "I'm not listening"*/
  bool operator ~() {
    return suppressUntil > 0;
  }

  void setup(unsigned inat, unsigned outat, bool fireif ) {
    inpin = inat;
    outpin = outat;
    highactive = fireif; //todo: may have inverted config value

    pinMode(inpin, highactive ? INPUT : INPUT_PULLUP );
    pinMode(outpin, OUTPUT); //always low active daisy chain

    changing = 0;
    lastStable = digitalRead(inpin) == highactive;

    if (lastStable)  {
      suppress(30000); //this will give the user 30 seconds to upload a different config, such as one with a different polarity trigger,
      //before getting stuck in an endless trigger loop. (which would not be a problem if commands were not blocked while running.)
      Serial.println(F("Trigger detected on startup-"));
      Serial.println(F("Going cold for 30 secs"));
    } else {
      suppressUntil = 0;
    }
    IS_HOT = true;
  }

  operator bool() {
    MilliTick now = millis(); //will be MilliTick.recent();
    if (removeout && now >= removeout) {
      digitalWrite(outpin, HIGH);
      removeout = 0;
    }
    if (!IS_HOT) {
      return false;
    }
    if (now < suppressUntil) {
      return false;
    }
    bool presently = digitalRead(inpin) == highactive;
    if (presently != lastStable) {
      if (lastChecked < now) {
        lastChecked = now;
        if (++changing >= DEBOUNCE) {
          lastStable = presently;
          changing = 0;
          if (lastStable) { //then we have a trigger
            digitalWrite(outpin, LOW);
            removeout = now + OUTWIDTH;
          } else { //trigger stably removed
            //nothing need be done
          }
        }
      }
    }
    return lastStable;
  }
} T;

//outside of Opts until configurable:
//if frame rate accuracy is needed then add a reference clock to the hardware, errors are likely to be temperature dependant.
static const float MILLIS_PER_FRAME = 50.0; //49.595; //default is 20 frames per sec (50 ms each)

static const float TIMING_OFFSETS[] = { -.405, 0};
static const int TIMING_OFFSET_TYPE_COUNT = arraySize(TIMING_OFFSETS);


struct FrameSynch {
  /** Internal and Generate use the legacy tweak on millis(). External watches a pin that is trusted to be glitch-free.
    The external synch will be used as an interruipt and the seuence step will execute in the ISR to minimize jitter */
  enum Mode {Internal, External, Generate} mode;
  /** millitick expected at given frame from sequence start */
  byte &tweakSelect;
  MilliTick operator()(uint32_t frame, MilliTick startingat) {
    return startingat + round(frame * (MILLIS_PER_FRAME + TIMING_OFFSETS[tweakSelect]));//legacy issue: using round() makes minor changes to timing compared to legacy, with less cumulative error
  }

  float nominal(uint32_t frames) {
    return round(frames * MILLIS_PER_FRAME / 1000.0);
  }

  FrameSynch(byte &tweaker): tweakSelect(tweaker) {}
};

//IDE failed to make a prototype for this:
void reportFrames(Stream &printer);

/////////////////////////////////////////////////
// this struct contains all config that is saved in EEPROM
struct Opts {
  static const int SAMPLE_END = 1000;
  uint16_t hi_sample;

  struct Slots { //legacy name
    byte pin_map = 0; //tells us what our pin mapping strategy is
    byte TTL_TYPE_BYTE = ~0U ; //will bust into bits
    byte mediaType = 0;
    byte mediaDelay = 0;
    byte RESET_DELAY_SECS = 30 ;
    byte BOOT_DELAY_SECS = 0 ;
    byte volume = 22 ; //if not 1..30 use 15
    byte trigger_type = 1 ;
    byte timingOffsetType = 0 ; //some arduinos have timing issues that must be offset, there are two options

    void makeValid() {
      pin_map = //we might choose to NOT coerce this value, as we now have a separate 'active pinmap' variable.
        pin.selectPins(pin_map);

      if (volume < 1 || volume > 30) {
        volume = 15;
      }

      if (timingOffsetType > TIMING_OFFSET_TYPE_COUNT - 1) {
        timingOffsetType = 0; //cannot point to non-existent element
      }
    }
  } B ;

  //there are spare eeprom locatins, ~8 last count

  bool ttlPolarity(unsigned which) const {
    return bitRead(B.TTL_TYPE_BYTE, which);
  }

  void makeValid() {
    B.makeValid();
    save();//in case we coerced a bad value
  }

  void save() const {
    EEPROM.put(SAMPLE_END, *this);
  }

  void fetch() {
    EEPROM.get(SAMPLE_END, *this);
    makeValid();
  }


  bool read() {
    if (stamp.verify(STAMP_OFFSET)) {
      fetch();
      return true;
    } else {
      return false;
    }
  }

  void report(Stream & printer) {
    printer.print(F("OctoBanger TTL v"));
    for (int i = 0; i < 3; i++) {
      printer.print(stamp.buff[i]);
      if (i < 2) {
        printer.print(".");
      } else {
        printer.println();
      }
    }
    printer.println(stamp.ok ? F("Config OK") : F("Config NOT FOUND, using defaults"));

    reportFrames(printer);

    printer.print(F("Reset Delay Secs: "));
    printer.println(B.RESET_DELAY_SECS);
    if (B.BOOT_DELAY_SECS != 0)  {
      printer.print(F("Boot Delay Secs: "));
      printer.println(B.BOOT_DELAY_SECS);
    }
    if (pin.current != B.pin_map) {
      printer.println(F("Invalid custom pinmap, using default."));
    } else {
      printer.print(F("Pin Map: "));
      switch (B.pin_map) {
        case 0:
          printer.println(F("Default_TTL")); break;
        case 1:
          printer.println(F("Shield")); break;
        case 2:
          printer.println(F("Custom")); break;
        default:
          printer.println(F("Unknown")); break;
      }
    }
    printer.print(F("Trigger Pin in: "));
    format_pin_print(pin.trigger(0), printer);
    printer.print(F("Trigger Ambient Type: "));
    if (B.trigger_type == 0) {
      printer.println(F("Low (PIR or + trigger)"));
    } else {
      printer.println(F("Hi (to gnd trigger)"));
    }
    printer.print(F("Trigger Pin Out: "));
    format_pin_print(pin.trigger(1), printer);
    printer.print(F("Media Serial Pin: ")); //bad text
    format_pin_print(pin.trigger(2), printer);

    audio.ReportConfig();

    printer.print(F("Timing Offset ms: "));
    printer.println(TIMING_OFFSETS[B.timingOffsetType], 3);

    printer.print(F("TTL PINS:  "));
    for (int i = 0; i < TTL_COUNT; i++) {
      if (!i) {
        printer.print(",");
      }
      printer.print(pin.trigger(i));
    }
    printer.println();
    printer.print(F("TTL TYPES: "));
    for (int i = 0; i < TTL_COUNT; i++) {
      if (!i) {
        printer.print(",");
      }
      printer.print(ttlPolarity(i));
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

} O;


FrameSynch fs(O.B.timingOffsetType);

////////////////////////////////////////////
// legacy snippets

//writes the bits of the current byte to our 8 TTL pins.
void OutputTTL(byte valIn) {
  bool val = 0;
  for (int x = 0; x < TTL_COUNT; x++) {
    digitalWrite(pin.out(x), bitRead(valIn, x) != O.ttlPolarity(x));// use of xor with booleans is deprecated, '!=' is the same thing.
  }
}

void set_ambient_out() { //set all outputs to default
  OutputTTL(0);
}

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

  unsigned current = ~0;
  unsigned used = 0;
  uint32_t frames = 0;
  MilliTick start = 0;
  MilliTick doNext = 0;

  //for recording
  bool amRecording = false;
  byte pattern;

  //cache of program content, will try to eliminate except as download buffer.
  struct Step {
    byte pattern;
    byte frames;
  }  //todo: order members by endianness of processor so as to match legacy processor's memory layout
  samples[SAMPLE_COUNT]; //working buffer so we don't read from eeprom constantly (which is silly as EEProm read is fast)


  void fetch(unsigned length) {
    //fetch whatever eeprom values we have into buffer
    if (length > 0)  {
      for (used = 0; used < SAMPLE_COUNT; ++used)    {
        EEPROM.get(used * 2, samples[used]);
      }
    }
  }

  void burn() {
    for (unsigned i = used; i < used; ++i)    {
      EEPROM.put(i * 2, samples[i]);
    }
  }


  uint32_t duration() {
    frames = 0; //1000 samples, 255 frames each possible
    for (int i = 0; i < used; i++) {
      frames += samples[i].frames ;
    }
    return frames;
  }

  void advance() {
    if (current > used) { //we should not be running
      return;
    }
    if (current == used) {
      finish();
    }
    auto sample = samples[current++];
    OutputTTL(sample.pattern);
    frames += sample.frames;
    doNext = fs(frames, start);
  }

  void finish() {
    set_ambient_out();
    audio.PlayAmbient();
    if (O.B.RESET_DELAY_SECS > 0) {
      T.suppress(round(O.B.RESET_DELAY_SECS * 1000));
      Serial.print(F("Waiting delay secs: "));
      Serial.println(O.B.RESET_DELAY_SECS);
    } else {
      Serial.println(F("Sequence complete, Ready"));
    }
  }

  /** common to start recording and start playing */
  void whenStarting() {
    start = millis();
    audio.PlayScare();
    digitalWrite(pin.trigger(2), LOW); //trigger audio ???
  }

  bool trigger() {
    if (amRecording) {
      startRecording();
      return true;
    }
    if (used == 0) {
      return false; //zero length program
    }

    if (current >= used) {
      current = 0;
      frames = 0;
      return true;
    } else { //retrigger not yet allowed
      return false;
    }
  }

  /** @returns whether a frame tick occured */
  bool check(MilliTick now) {
    if (doNext && now >= start + 100) {//abusing doNext to indicate both playback and recording
      digitalWrite(pin.trigger(2), HIGH); //fixed with daisy chain trigger
    }
    if (doNext && now >= doNext) {
      if (amRecording) {
        record(pattern);
      } else {
        advance();
      }
      return true;
    }
  }

  bool record(byte pattern) {
    if (current >= SAMPLE_COUNT) { //we should not be running
      return false;
    }
    if (doNext == 0) {
      return false;
    }
    auto &sample = samples[current];
    if (pattern != sample.pattern || sample.frames == 255) { //new because of change or max time reached
      ++current;
      if (current == SAMPLE_COUNT) {
        endRecording(true);
        return false;
      } else {//start new sample
        samples[current].pattern = pattern;
        samples[current].frames = 1;
      }
    } else {
      ++sample.frames;
      ++frames;
      doNext = fs(frames, start);
    }
  }

  void startRecording() {
    frames = 0;
    current = 0;
    samples[current].pattern = pattern;
    samples[current].frames = 1;//haven't actually sampled yet.
    doNext = fs(frames, start);
  }

  void endRecording(bool fromOverflow = false) {
    if (!fromOverflow) {
      ++current;//else we lose the accumulating frame
    }
    used = current;
    doNext = 0;//this ends recording
    amRecording = false;
  }

} S;

////////////////////////////////////////////
void reportFrames(Stream &printer) {
  printer.print(F("Frame Count: "));
  printer.println(O.hi_sample);//legacy 2X

  printer.print(F("Seq Len Secs: "));
  printer.println(fs.nominal(S.duration()));
}

////////////////////////////////////////////
// does not block, expectes its check() to be called frequently
struct CommandLineInterpreter {
  enum Expecting {
    At = 0,
    Letter,
    LoLength,
    HiLength,
    Datum
  } expecting;

  char pending;
  unsigned sofar;
  unsigned expected;
  unsigned sendingMemory = ~0;
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
      case 'H': //go hot
        T.IS_HOT = true;
        stream.println(F("Ready"));
        break;
      case 'C': //go cold
        T.IS_HOT = false;
        stream.println(F("Standby..."));
        break;
      case 'D': //send program eeprom contents back to Serial
        tx_memory();
        break;
      case 'F': //send just the config eeprom contents back to Serial
        tx_config();
        break;
      case 'S':  //expects 16 bit size followed by associated number of bytes, ~2*available program steps
        return true;//need more
      case 'U': //expects 16 bit size followed by associated number of bytes, ~9
        return true;//need more
      case 'P': //ping back
        O.report(stream);
        break;
      case 'T': //trigger test
        S.trigger();
        break;
      case 'M': //expects 1 byte of packed outputs //manual TTL state command
        return true;//need more
      case 'O': //is the stamp OK?
        stream.println(stamp.ok ? F("OK") : F("NO"));
        break;
      default:
        stream.print(F("unk char:"));
        stream.print(letter);
        clear_rx_buffer();//todo: debate this, disallows resynch
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
    int bytish = stream.read();
    if (bytish != ~0) {
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
          }
          break;
        case LoLength:
          expected = bytish;
          expecting = HiLength;
          break;
        case HiLength:
          expected += bytish;
          switch (pending) {
            case 'S': //receive step datum
              if (expected > 1000 ) {
                stream.print(F("Unknown program size received: "));
                stream.println(expected);
                onBadSizeGiven();
              } else {
                O.hi_sample = expected / 2;
              }
              break;
            case 'U'://receive config flag
              if (expected != sizeof(O.B) ) {
                stream.print(F("Unknown config length passed: "));
                stream.println(expected);
                onBadSizeGiven();
              }
              break;
          }
          expecting = Datum;
          break;
        case Datum: //incoming binary data has arrived
          switch (pending) {
            case 'S': //receive step datum
              EEPROM.write(sofar++, bytish);
              if (sofar >= expected ) {//then that was the lat byte
                saveConfig();//deferred in case we bail out on receive

                expecting = At;
                stream.print(F("received "));
                stream.print(O.hi_sample);
                reportFrames(stream);
                stream.println(F("Saved, Ready"));
              }
              break;
            case 'U'://receive config flag
              reinterpret_cast<byte *>(&O.B)[sofar++] = bytish;
              if (sofar >= sizeof(O.B)) {
                saveConfig();

                stream.print(F("Received "));
                stream.print(expected);
                stream.println(F(" config bytes"));
                stream.println(F("Please reconnect"));
                if (!stamp.ok)  {
                  O.hi_sample = 0;
                  saveConfig();
                }
                stamp.burn(STAMP_OFFSET);
                stamp.ok = O.read();
                set_ambient_out();
              }
              break;
            case 'M':
              OutputTTL(bytish);
              expecting = At;
              break;
          }
          break;
      }
    }

    if (sendingMemory < STAMP_OFFSET) {
      auto canSend = stream.availableForWrite();
      if (canSend) {
        while (canSend-- > 0) {
          stream.write(EEPROM.read(sendingMemory++));
          if (++sendingMemory >= STAMP_OFFSET) {
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
    stream.write(reinterpret_cast<const char *>(&O.B), sizeof(O.B));
  }

  //called at end of setup
  void setup() {
    //original did a clear_rx
    stream.println(F(".OBC")); //spit this back immediately tells PC what it just connected to
  }

};

CommandLineInterpreter cli{Serial}; //todo: allocate one each for USB if present and rx/tx.


///////////////////////////////////////////////////////
struct Blinker {
  //names presume it will mostly be off.
  //todo: add polarity for whether idles off versus on
  MilliTick offAt;//when will be done
  MilliTick onAt; //delayed start

  void onTick(MilliTick now) {
    if (offAt && now >= offAt) {
      offAt = 0;
      digitalWrite(BlinkerPin, LOW);
    }
    if (onAt && now >= onAt) {
      onAt = 0;
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
  void *tosend = nullptr;
  unsigned sendingLeft = 0;

  /** sends the '@' the letter and dependeing upon letter might send one or two more bytes from more */
  bool sendCommand(char letter, unsigned more = ~0 ) {
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

  bool sendChunk(char letter, void *thing, size_t sizeofthing) {
    tosend = thing;
    sendingLeft = sizeofthing;
    if (!sendCommand(letter, sendingLeft)) {
      sendingLeft = 0;
      return false;
    }
    return true;
  }


  //user msut confirm versions match before calling this
  bool sendConfig() {
    return sendChunk('U', &O.B, sizeof(O.B));
  }

  bool sendFrames() {
    return sendChunk('S', &S.samples, sizeof(S.samples));
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
    if (!target || !tosend || !sendingLeft) {
      return;
    }

    auto wrote = target->write( reinterpret_cast<byte *> (tosend), sendingLeft);//cast needed due to poor choice of type in Arduino stream library.
    tosend += wrote;
    sendingLeft -= wrote;
  }

};

///////////////////////////////////////////////////////

void setup() {
  //read config from eeprom
  stamp.ok = O.read();
  S.fetch(O.hi_sample / 2);
  //apply idle state to hardware
  pin.setModes();
  set_ambient_out();

  T.setup(pin.trigger(0), pin.trigger(1), O.B.trigger_type);
  blink.setup();

  audio.Init(pin.trigger(2), O.B.volume, O.B.mediaType, O.B.mediaDelay);
  audio.PlayAmbient();

  Serial.begin(115200); //we talk to the PC at 115200
  cli.setup();

  O.report(cli.stream);//must follow S init to get valid duration

  if (O.B.BOOT_DELAY_SECS > 0)  {
    T.suppress(O.B.BOOT_DELAY_SECS * 1000);
  }
  Serial.println(F("Ready"));
}

void loop() {
  auto now = millis();
  cli.check();
  if (S.check(now)) { //active frame event.
    //new frame
    if (S.amRecording) {
      //here is where we update S.pattern with data to record.
      // a 16 channel I2C box comes to mind, 8 data inputs, record, save, and perhaps some leds for recirding and playing
    }
  }

  //if recording panel is installed:


  blink.onTick(now);

  if (T) {//trigger is active
    if (S.trigger()) {//ignores trigger being active while sequence is active
      cli.stream.println(F("Playing sequence..."));
    }
  }

  if (~T) {//then it is suppressed or otherwise not going to fire
    if (!blink) {
      blink.pulse(450, 550);
    }
  }
}
