#include "chainprinter.h"
ChainPrinter dbg(Serial, true); //true adds linefeeds to each invocation.

#include "millievent.h"

struct Flickery {
  bool ison;
  unsigned range[2];
  MonoStable duration;

  Flickery(unsigned ontime, unsigned offtime) {
    ison = false;
    range[0] = offtime;
    range[1] = ontime;
  }

  void setup() {
    be(true);//autostart
    duration = 750;//value chosen for debug
  }

  void onTick() {
    if (duration.hasFinished()) {
      be(!ison);//just toggle, less code and random is random.
      duration = (random(range[0], range[1]));
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
  FlickeryPin(int apin, unsigned ontime, unsigned offtime): Flickery(ontime, offtime), Led_Pin(apin) {
    //nada.
  }
  virtual void be(bool on) {
    Flickery::be(on);
    digitalWrite(Led_Pin, on ? HIGH : LOW);
    dbg("Pin ", Led_Pin, '=', on ? "HIGH " : "LOW ");
  }
};

FlickeryPin led[] = {
  {13, 1201, 1400}, //for debug
  //station 4 will use one of these, scanner hallway two, station 9 all four but copied over I2C.
  { 10, 1201, 1400},
  { 9, 1302, 1500},
  { 8, 1100, 1703},
  { 7, 1204, 1500},

};

static const int numleds = sizeof(led) / sizeof(FlickeryPin);

void freezeAll() {
  for (unsigned count = numleds; count-- > 0;) {
    led[count].freeze();
  }

}

//import PCF7475 class here and pack the led array into it.
#include "pcf8574.h"

PCF8574 station9(0);//9th status panel, operand is jumper settings

void packout() {
  unsigned newly = 0;
  for (unsigned count = numleds; count-- > 0;) {
    if (led[count].ison) {
      newly |= 3 << (1 + 2 * count); //this is how a particular board is wired, setting pairs of pins in case we need to boost drive.
    }
  }
  if (station9.seemsOk()) {//deferred test for debug
    if (station9.cachedBits() != newly) {
      station9 = newly;
    }
  }
}

void setup() {
  Serial.begin(115200);//used by dbg()
  dbg("setup flickerers");
  for (unsigned count = numleds; count-- > 0;) {
    led[count].setup();
  }
  dbg("setup I2C");
  station9.begin();

  if (station9.isPresent()) {
    packout();
  } else {
    dbg("I2C failed");
  }
  randomSeed(202142);
  dbg("setup complete");
}

//unsigned spewer = 0;

unsigned testitem = 0; //index into led[]

void loop() {
  if (MilliTicked) { //only run the loop once a millisecond to lower power consumption, might be able to run on batteries if we do instead of needing another P/S.
    for (unsigned count = numleds; count-- > 0;) {
      led[count].onTick();
    }

    //copy bits out to I2C
    packout();
    //was approx 1/second, not 10 times. the print might be too expensive
    //    if (0 == ++spewer % 100) {
    //      dbg("Tick:", MilliTicked.recent());
    //    }
  }
  if (Serial) {
    int key = Serial.read();
    if (key > 0) {
      switch (key) {
        case '-': led[testitem].be(false);
          break;
        case '=': led[testitem].be(true);
          break;
        case '!':
          freezeAll();
          break;
        case '@':
          setup();
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
