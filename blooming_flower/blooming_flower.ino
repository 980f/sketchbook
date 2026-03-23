////////////////////////////////////////////////
// Blooming flower mechanism, first used for Quest Night 2026 Swamping puzzle
//
// full control pins:
// motor board 3,4,(5,6,)7,8,(9,10,)11,12
// moisture sensor A0
// homeSensor ??
//
//
////////////////////////////////////////////////
////////////////////////////////////////////////
//timer version: InMotion is output to enable flower power
const int InMotion = 10;  //D10;
//forceOn is for testing, it bypasses most other logic
// and now it is 'puzzle reset', but I ain't changing the name until it actually works
const int forceOn = 8;  //D8
//low when flower is closed, else somewhat high (2.5V in first system, maybe wrong zener was used?)
//CAVEAT: reports high when power is off! Cannot check home without the device definitely being on (relay has to have activated, not just been told to be active).
const int homeSensor = 6;  //D6
////////////////////////////////////////////////
//made a separate function for debugging ESP32duino boot loops
void setupPin(int dx) {
  Serial.print("\ttaking pin ");
  Serial.print(dx);
  Serial.print('\t');
  pinMode(dx, OUTPUT);
}

void power(bool beon) {
  Serial.print("\tpower:");
  Serial.print(beon);
  digitalWrite(InMotion, beon);
}

//andy has a library that makes millisecond timing compact, this is a tiny bit of it.
using MilliTick = unsigned long;
MilliTick milliTicker = 0;

#include <EEPROM.h>  //for tweaking parameters without downloading a new program

/////////////////////////////////////
//things that should be saved and restored from eeprom:
struct Puzzle {
  //wetness above required+hysteresis starts opening, below required-hysteresis to start closing if it was opening.
  unsigned WetnessRequired;

  //motor tuners:  The below should be floats but making them ints allows for shared tweaking code, and is resolution enough for this puzzle.
  unsigned Acceleration;  //steps per second per second
  unsigned MaxSpeed;      //steps per second
                          //below is temporarily abused to be ms needed to go from homed to open.
  unsigned trackLength;   //number of steps from fully closed to fully open
  unsigned sensorPin;
  //how often to read and report on the wetness sensor
  unsigned sensorSamplingRate;
  //how many samples in a row that must match before a change is reacted to
  unsigned sensorFilter;

  bool isWet(int wetness) {
    return wetness < WetnessRequired;
  }

  // bool isDry(int wetness) {
  //   return wetness > (WetnessRequired + WetnessHysteresis);
  // }

  void builtins() {
    trackLength = 5270;  //was number of steps from fully closed to fully open, temporarily is milliseconds needed to go from homed to open
    sensorPin = A0;
    sensorSamplingRate = 333;
    sensorFilter = 3;  // this many in a row the same or no change happened
    WetnessRequired = 600;

    MaxSpeed = 100;
    Acceleration = MaxSpeed;  //defeats acceleration feature
  }

  void info() {
    Serial.print("\nPuzzle Parameters:");
    Serial.print("\nrate\tfilt\tWet\tLen\tAcc\tSpd");
    Serial.print("\n");
    Serial.print(sensorSamplingRate);
    Serial.print("\t");
    Serial.print(sensorFilter);

    Serial.print("\t");
    Serial.print(WetnessRequired);

    Serial.print("\t");
    Serial.print(trackLength);

    Serial.print("\t");
    Serial.print(Acceleration);
    Serial.print("\t");
    Serial.print(MaxSpeed);
  }

  void load() {
    EEPROM.get(0, *this);
  }

  void save() {
    EEPROM.put(0, *this);
#ifdef ESP32
    EEPROM.commit();
#endif
  }

  void setup() {
#ifdef ESP32
    EEPROM.begin((sizeof(Puzzle) + 63) & ~63);  //round up to block of 64 as that is optimal on ESP32
#endif
    load();
  }

} puzzle;


struct Sensor {
  int ain;
  MilliTick readInterval;  //how often to read the sensor, so that diagnostics don't swamp us.
  unsigned reading;        //raw analog value
  MilliTick read;          //time sensor was last read

  bool operator>(unsigned threshold) {
    return reading > threshold;
  }

  bool operator<(unsigned threshold) {
    return !operator>(threshold);
  }

  Sensor(int ainPin)
    : ain(ainPin),
      readInterval(1000),  //should always be overwritten before 'real' code runs
      reading(~0),         //init to something wildly impossible
      read(0) {
    //nothing needed here
  }

  void setup() {
    pinMode(ain, INPUT);  //in case we dynamically reassign pins.
    readInterval = puzzle.sensorSamplingRate;
    Serial.println("\nfirst reading of sensor");
    reading = analogRead(ain);
    read = millis();
  }

  bool onTick(MilliTick now) {
    if (now > read + readInterval) {
      read = now;
      reading = analogRead(ain);
      return true;
    }
    return false;
  }
};

struct BloomingFlower {
  bool debug = false;
  Sensor wetness;

  MilliTick doneTime = 0;
  MilliTick startTime = ~0;  //mark 'never started'
  void startTimer() {
    power(1);
    startTime = millis();  //record for relay start time for home sensor
    doneTime = startTime + puzzle.trackLength;
  }

  //state of bloom
  enum Bloomer {
    Unknown,
    Homing,
    Opened,
    Closed,
    Opening,
    Closing,
    Timing,
  };

  Bloomer blooming = Unknown;
  //keep hardware status handy for debug:
  struct state {
    bool Wet = false;
    unsigned bouncing = 0;
    bool Moving = false;
    long At = ~0;
    bool atHome = false;
  } is;

  BloomingFlower()
    : wetness(puzzle.sensorPin) {}

  void startOpening() {
    Serial.print("\nstartOpening");
    startTimer();
    blooming = Opening;
  }

  void startClosing() {  //this should be obsolete
    Serial.print("\nstartClosing");
    blooming = Closing;
    startTimer();
  }

  void rehome(const char *why) {
    Serial.print("\nrehome due to ");
    Serial.print(why);
    blooming = Homing;
    startTimer();
  }

  void stop(Bloomer be, const char *why) {
    blooming = be;
    power(0);
    startTime = ~0;
    Serial.print('\n');
    Serial.print(why);
    showState();
  }

  bool checkHome(MilliTick now) {
    if (startTime < now && now > startTime + 100) {  //todo: make that 100 another puzzle parameter
      bool newvalue = digitalRead(homeSensor) == 0;
      if (is.atHome != newvalue) {  //change detect to keep message spew readable
        is.atHome = newvalue;
        Serial.print("\nhome sensor changed to ");
        Serial.print(is.atHome);
        Serial.print(" at ");
        Serial.print(now);
      }
    }  //else just keep the last value
  }

  void onTick(MilliTick now) {
    checkHome(now);
    is.Moving = now < doneTime;  //timeout is a backup now, not a primary control

    if (wetness.onTick(now)) {  //then we have a new reading so update values
      bool newlyWet = puzzle.isWet(wetness.reading);
      if (is.Wet != newlyWet) {
        if (++is.bouncing >= puzzle.sensorFilter) {
          is.Wet = newlyWet;
          showState();
        }
      } else {
        is.bouncing = 0;
      }
      if (debug) {
        showState();
      }
    }
    //puzzle reset overrides most other logic
    if (blooming != Homing && digitalRead(forceOn) == 0) {
      rehome("puzzle reset button");
      blooming = Homing;
    }

    //state changes are here
    switch (blooming) {
      default:
        Serial.println("\nIllegal blooming state");
        blooming = Unknown;
        //join:
      case Unknown:
        rehome("power up");
        break;
      case Homing:
        if (!is.Moving) {
          stop(Closed, "Timeout Homing");
        } else if (is.atHome) {
          stop(Closed, "Homed");
        }
        break;

      case Closed:  //most of the time we are here, waiting for the customer to dunk orbees into the bucket.
        if (is.Wet) {
          startOpening();
        }
        break;

      case Opening:
        if (!is.Moving) {
          stop(Opened, "Opened");
        }
        break;

      case Opened:  //if sensor not on then start closing
        //removed auto home, it fuzzed things and also ignoring it means we may not need to debounce.
        break;

      case Closing:  //presume sensor glitched and we should do a full reset
        if (!is.Moving) {
          stop(Closed, "Closed");
        }
        break;
      case Timing:  //obsolete case
        if (digitalRead(forceOn)) {
          blooming = Closed;
          digitalWrite(InMotion, 0);
        }
        break;
    };
  }

  template<typename SomePrintableType> void showItem(const char *symbol, SomePrintableType datum) {
    Serial.print("\t");
    Serial.print(symbol);
    Serial.print(":");
    Serial.print(datum);
  }

  void showState() {
    //IDE lied about uno having printf:
    Serial.println();

    showItem("Wet", is.Wet);
    showItem("Bounce", is.bouncing);
    showItem("Homed", is.atHome);

    showItem("Moisture", wetness.reading);
    showItem("ms", wetness.read);

    Serial.println();
  }

  void setup() {
    Serial.println("\nFlower Setup");
    wetness.setup();
    setupPin(InMotion);
    pinMode(forceOn, INPUT_PULLUP);
    //set a known state for consistency
    power(0);
  }
};
BloomingFlower flower;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////
//arduino hooks:
//match baud rate to downloader so that we don't get garbage in serial monitor when running new program:
#ifdef ESP32
#define DebugBaud 921600
#else
#define DebugBaud 115200
#endif

void setup() {
  Serial.begin(DebugBaud);  //use same baud as downloader to eliminate crap in serial monitore window.
  delay(1000);              //to let UNO serial monitor reliably get the following text
  Serial.println("\nBloomin' Flower!");
  puzzle.setup();  //reads puzzle configuration object from eeprom. This is kept separate from flower.setup() so that we can see how flower will work with temporarily altered puzzle parameters. see '!' debug action.
  flower.setup();  //rest of setup.
}

//returns whether char was applied
template<typename SomeIntType> bool romanize(SomeIntType &number, char letter, bool isUpper) {

  SomeIntType magnitude = 0;  //init in case we get stupid later

  switch (letter) {
    case 'i':
      magnitude = 1;
      break;
    case 'v':
      magnitude = 5;
      break;
    case 'x':
      magnitude = 10;
      break;
    case 'l':
      magnitude = 50;
      break;
    case 'c':
      magnitude = 100;
      break;
    case 'd':
      magnitude = 500;
      break;
    case 'm':
      magnitude = 1000;
      break;
    default:
      return false;
  }
  if (isUpper) {
    number += magnitude;
  } else {
    number -= magnitude;  //user beware that negative numbers might be treated as really large.
  }

  return true;
}

unsigned *tweakee = &puzzle.WetnessRequired;  //legacy init

void loop() {

  //loop is called way too often, we put our logic in a once per millisecond function above
  MilliTick now = millis();
  if (milliTicker != now) {  //once a millisecond
    milliTicker = now;
    flower.onTick(now);
  }
  //debug actions
  auto key = Serial.read();
  if (key > 0) {  //negative is "no key was present", zero is a NUL character
    bool isUpper = key <= 'Z';
    char letter = tolower(key);
    //if a roman numeral letter then tweak some number
    if (tweakee && romanize(*tweakee, letter, isUpper)) {  //uses CDILMVX
      return;
    }
    //not a number tweak so do something else.
    switch (letter) {
      default:
        Serial.print("\nUnknown command char:");
        Serial.println(key);
        break;
      case 'q':
        flower.debug = isUpper;
        Serial.print("\nstatus spew is:");
        Serial.print(flower.debug ? "ON" : "Off");
        break;
      case 'p':
        power(1);
        Serial.print("\nMotor power enabled ");
        break;
      case 'o':
        power(0);
        Serial.print("\nMotor power disabled ");
        Serial.print(now);
        break;
      case '?':
        puzzle.info();
        break;
      case ' ':
        flower.showState();
        break;
      case '.':
        // flower.motor.move(0);  //or perhaps power off?
        break;
      case '/':
        flower.startClosing();
        break;
      case ',':
        flower.startOpening();
        break;
      case '!':
        flower.setup();
        flower.rehome("debug request");
        break;
      case '\\':  //reset to compiled in values, needed on first load of a signficantly new program
        Serial.print("\nSet parameters to hard coded values");
        puzzle.builtins();
        puzzle.info();
        break;
      case '`':
        Serial.print("\nUndoing temporary changes");
        puzzle.load();
        puzzle.info();
        break;
      case '|':
        Serial.print("\nSaving parameters");
        puzzle.save();
        puzzle.info();
        break;

      case 'r':
        Serial.print("\nTweaking sensor sampling rate");
        tweakee = &puzzle.sensorSamplingRate;
        break;
      case 'f':
        Serial.print("\nTweaking sensorFilter");
        tweakee = &puzzle.sensorFilter;
        break;
      case 'w':
        Serial.print("\nTweaking wetness threshold");
        tweakee = &puzzle.WetnessRequired;
        break;
      case 't':
        Serial.print("\nTweaking time");
        tweakee = &puzzle.trackLength;
        break;
      // case 'h':
      //   Serial.print("\nTweaking wetness hysteresis");
      //   tweakee = &puzzle.;
      //   break;
      case 'a':
        Serial.print("\nTweaking acceleration");
        tweakee = &puzzle.Acceleration;
        break;
      case 's':
        Serial.print("\nTweaking max speed");
        tweakee = &puzzle.MaxSpeed;
        break;

      case 13:
      case 10:  //ignore these, arduino2 serial monitor sends newlines when it sends what you type.
        break;
    }
  }
}
///////////////////////////
//end of code.
///////////////////////////
#if 0  //documentation

with home sensor but just controlling power:
-- on puzzle reset turn power on until home sensor is active
-- on wet power goes on and stays on 
-- perhaps if dry but am opening switch to homing


Doitourselves wiring:
P/S from servo board to sensor: red +5, brown GND,
 sensor AO to UNO A0, orange wire.
 barrel to M power: orange +, grey GND
 motor green/black to M1?
 motor red/blue to M2?

#endif  //and the file really has ended
