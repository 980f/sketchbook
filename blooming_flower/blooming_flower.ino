////////////////////////////////////////////////
// Blooming flower mechanism, first used for Quest Night 2026 Swamping puzzle
//
// pins:
// motor board 3,4,(5,6,)7,8,(9,10,)11,12
// sensor A0
//
// 
//
//
////////////////////////////////////////////////
//original board: #define UnoR3


const int InMotion = 10;  //D10;
const int forceOn = 8;    //D8
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
  unsigned WetnessHysteresis;
  //motor tuners:  The below should be floats but making them ints allows for shared tweaking code, and is resolution enough for this puzzle.
  unsigned Acceleration;  //steps per second per second
  unsigned MaxSpeed;      //steps per second

  unsigned trackLength;  //number of steps from fully closed to fully open
  unsigned sensorPin;
  unsigned sensorSamplingRate;

  bool isWet(int wetness) {
    return wetness < (WetnessRequired - WetnessHysteresis);
  }

  bool isDry(int wetness) {
    return wetness > (WetnessRequired + WetnessHysteresis);
  }

  void builtins() {
    trackLength = 6100;  //number of steps from fully closed to fully open, may be set low for debug!
    sensorPin = A0;
    sensorSamplingRate = 167;
    WetnessRequired = 600;
    WetnessHysteresis = 20;

    Acceleration = 7;
    MaxSpeed = 98;
  }

  void info() {
    Serial.print("\nPuzzle Parameters:");
    Serial.print("\nrate\tWet\t+/-\tLen\tAcc\tSpd");
    Serial.print("\n");
    Serial.print(sensorSamplingRate);
    Serial.print("\t");
    Serial.print(WetnessRequired);
    Serial.print("\t");
    Serial.print(WetnessHysteresis);
    Serial.print("\t");
    Serial.print(trackLength);
    Serial.print("\t");
    Serial.print(Acceleration);
    Serial.print("\t");
    Serial.print(MaxSpeed);
    Serial.println();
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
      readInterval(puzzle.sensorSamplingRate),
      reading(~0),  //init to something wildly impossible
      read(0) {
    //nothing needed here
  }

  void setup() {
    pinMode(ain, INPUT);  //in case we dynamically reassign pins.
    Serial.println("\nfirst reading of sensor");
    reading = analogRead(ain);
    read = millis();
  }

  void onTick(MilliTick now) {
    if (now > read + readInterval) {
      read = now;
      reading = analogRead(ain);
    }
  }
};

struct BloomingFlower {
  Sensor wetness;

  MilliTick doneTime = 0;
  void startTimer() {
    power(1);

    doneTime = millis() + puzzle.trackLength;
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
    bool Dry = false;
    bool Moving = false;
    long At = ~0;
    bool Open = false;    //limit switch
    bool Closed = false;  //limit switch
  } is;

  BloomingFlower()
    : wetness(puzzle.sensorPin) {}

  void startOpening() {
    Serial.print("\nstartOpening");
    startTimer();
    blooming = Opening;
  }

  void startClosing() {
    Serial.print("\nstartClosing");
    blooming = Closing;
    startTimer();
  }

  void rehome() {
    Serial.print("\nrehome");
    blooming = Homing;
  }

  void stop(Bloomer be, const char *why) {
    blooming = be;
    power(0);
    Serial.print('\n');
    Serial.print(why);
    showState();
  }

  void onTick(MilliTick now) {
    wetness.onTick(now);
    //get all hardware state whether it matters or not, for debug convenience:
    is.Wet = puzzle.isWet(wetness.reading);
    is.Dry = puzzle.isDry(wetness.reading);
    is.Moving = now < doneTime;

    if (digitalRead(forceOn) == 0) {
      blooming = Timing;
      digitalWrite(InMotion, 1);
    }

    //state changes are here
    //todo: priority of motion completing over changes in wetness. Presently once we start we finish before being willing to turn around.
    //todo: add limit switches making is.Moving a backup for a failed switch.
    switch (blooming) {
      default:
        Serial.println("\nIllegal blooming state");
        blooming = Unknown;
        //join:
      case Unknown:
        rehome();
        break;
      case Homing:
        if (!is.Moving) {
          stop(Closed, "Homed");
        }
        break;

      case Closed:  //most of the time we are here, waiting for the customer to dunk orbees into the bucket.
        if (is.Wet) {
          startOpening();
        }
        break;

      case Opening:
        //todo: check fail safe timer and stop stepper
        if (!is.Moving) {
          stop(Opened, "Opened");
        }
        break;

      case Opened:  //if sensor not on then start closing
        if (is.Dry) {
          startClosing();
        }
        break;

      case Closing:  //presume sensor glitched and we should do a full reset
        if (!is.Moving) {
          stop(Closed, "Closed");
        }
        //perhaps:
        // if(is.Wet){
        //   startOpening();
        // }
        break;
      case Timing:
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
    showItem("Dry", is.Dry);

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
bool romanize(unsigned &number, char letter) {
  bool isUpper = letter <= 'Z';
  int magnitude = 0;  //init in case we get stupid later

  switch (tolower(letter)) {
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
    number -= magnitude;  //user beware that negative numbers are instead treated as really large.
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
  //if a roman numeral letter then tweak some number
  if (tweakee && romanize(*tweakee, key)) {  //uses CDILMVX
    return;
  }
  //not a number tweak so do something else.
  switch (tolower(key)) {
    default:
      if (key > 0) {
        Serial.print("\nUnknown command char:");
        Serial.println(key);
      }
      break;
    case 'p':
      power(1);
      Serial.print("\nMotor power enabled");
      break;
    case 'o':
      power(0);
      Serial.print("\nMotor power disabled");
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
      flower.rehome();
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

    case 'w':
      Serial.print("\nTweaking wetness threshold");
      tweakee = &puzzle.WetnessRequired;
      break;
    case 't':
      Serial.print("\nTweaking time");
      tweakee = &puzzle.trackLength;
      break;
    case 'h':
      Serial.print("\nTweaking wetness hysteresis");
      tweakee = &puzzle.WetnessHysteresis;
      break;
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
///////////////////////////
//end of code.
///////////////////////////
// P/S from servo board to sensor: red +5, brown GND,
// sensor AO to UNO A0, orange wire.
// barrel to M power: orange +, grey GND
// motor green/black to M1?
// motor red/blue to M2?
