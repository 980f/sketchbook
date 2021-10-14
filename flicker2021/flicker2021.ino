#include "chainprinter.h"
#if 1 //8684/606 with debug 7616/483 without, so around 1k of debug statments 
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.
//eventually these will be made editable by Serial
bool dbgi2c = 0;
bool dbgpin = 0;
#else
#define dbg(...)
#endif

#include "millievent.h"
const MilliTick offtime=50;
struct Flickery {
  bool ison;
  //these are used as MilliTicks, but since we want flicker we can use a shorter int to save on ram.
  unsigned mintime, maxtime;
  MonoStable duration;

  Flickery(unsigned mintime, unsigned maxtime): ison(true), mintime(mintime), maxtime(maxtime) {
    //#nada
  }

  virtual void setup() {
    be(true);//autostart
    duration = 750;//value chosen for debug
  }

  void onTick() {
    if (duration.hasFinished()) {
      be(!ison);//just toggle, less code and random is random.
      duration = ison ? random(mintime, maxtime) : offtime;
    }
  }

  virtual void be(bool on) {
    ison = on;
  }

  //to ease some testing we kill the timer then toggle bits manually
  void freeze() {
    duration.stop();
  }
};

/**
  flicker a pin
*/
struct FlickeryPin: public Flickery {
  int Led_Pin;
  FlickeryPin(int apin, unsigned mintime, unsigned maxtime): Flickery(mintime,  maxtime), Led_Pin(apin) {
    //nada.
  }

  void setup() {
    //set pin as output
    pinMode(Led_Pin, OUTPUT);
    Flickery::setup();
  }

  void be(bool on) {
    Flickery::be(on);
    digitalWrite(Led_Pin, on ? HIGH : LOW);
    if (dbgpin) dbg("Pin ", Led_Pin, '=', on ? "HIGH " : "LOW ", " for ", MilliTick(duration));
  }
};

const FlickeryPin led[] = {
  //station 4 will use one of these, scanner hallway two, station 9 all four but copied over I2C.
  { 10, 100, 759},
  { 9, 150, 1250},
  { 8, 175, 1431},
  { 7, 25, 330},

};

static const int numleds = sizeof(led) / sizeof(FlickeryPin);

void freezeAll() {
  for (unsigned count = numleds; count-- > 0;) {
    led[count].freeze();
  }

}

#include "pcf8574.h"

PCF8574 station9(0, 0, 100); //for 9th status panel, operand is jumper settings, bus 0, 100 kHz is its max bus rate

void packout() {
  unsigned newly = 0;
  for (unsigned count = numleds; count-- > 0;) {
    if (led[count].ison) {
      newly |= 3 << (2 * count); //this is how a particular board is wired, setting pairs of pins in case we need to boost drive.
      //dbg("Packing:",HEXLY(newly));
    }
  }
  newly &= 0xFF; //only use lower 4 leds. Else we get perpetual miscompares.
  if (station9.seemsOk()) {//deferred test for debug
    auto cached = station9.cachedBits();
    if (cached != newly) {
      station9 = newly; //this outputs to I2C
      if (dbgi2c) dbg("I2C:", HEXLY(newly), " from:", HEXLY(cached));
    }
  }
}
/////////////////////////////////////////////////

#include "scani2c.h"  //not a proper h/cpp file combo


void setup() {
  Serial.begin(115200);//used by dbg()
  dbg("setup flickerers");
  for (unsigned count = numleds; count-- > 0;) {
    led[count].setup();
  }
  dbg("setup I2C");
  station9.setInput(0);//no inputs
  station9.begin();

  if (station9.isPresent()) {
    packout();
    dbg("I2C worked");
  } else {
    scanI2C(dbg, 0x27, 0x20);
    dbg("I2C failed");
  }
  randomSeed(202142);
  dbg("setup complete");
}

unsigned testitem = 0; //index into led[]

void loop() {
  if (MilliTicked) { //only run the loop once a millisecond to lower power consumption, might be able to run on batteries if we do instead of needing another P/S.
    for (unsigned count = numleds; count-- > 0;) {
      led[count].onTick();
    }

    //copy bits out to I2C
    packout();
  }
  if (Serial) {
    int key = Serial.read();
    if (key > 0) {
      switch (tolower(key)) {
        case '-':
          led[testitem].be(false);
          break;
        case '=':
          led[testitem].be(true);
          break;
        case '!': //stop activity so that we can test individual events
          freezeAll();
          break;
        case '@': //soft reset
          setup();
          break;
        case 'p':
          dbgpin = isupper(key);
          break;
        case 'i':
          dbgi2c = isupper(key);
          break;
        default:
          if (key >= '0' && key < '0' + numleds) {
            testitem = key - '0';
            dbg("selected:", testitem);
          } else {
            dbg("Key:", key);
          }
          break;
      }
    }

  }
}
