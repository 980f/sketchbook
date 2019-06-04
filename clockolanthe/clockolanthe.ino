
#include "options.h"
//todo: leonardo and separate ESP01, both sources with lots of #ifdef

////////////////////////////////////
#include "pinclass.h"
#include "cheaptricks.h"
#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);


#include "clockserver.h"
ClockServer server(1859, "bigbender", dbg);

#if UsingEDSir
#include "edsir.h"
EDSir IRRX;
MonoStable sampleIR(147);//fast enough for a crappy keypad
#endif

//soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"
Using_MilliTicker


#include "stepper.h"
#include "clockhand.h"
//project specific values:
const unsigned baseSPR = 2048;//28BYJ-48
const unsigned slewspeed = 5;//5: smooth moving, no load.

/** 4 wire 2 phase unipolar drive */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn> class FourBanger {
  protected:
    OutputPin<xp> mxp;
    OutputPin<xn> mxn;
    OutputPin<yp> myp;
    OutputPin<yn> myn;

  public:
    static bool greylsb(byte step) {
      byte phase = step & 3;
      return (phase == 1) || (phase == 2);
    }

    static bool greymsb(byte step) {
      return (step & 3) >> 1;
    }

    void operator()(byte step) {
      bool x = greylsb(step);
      bool y = greymsb(step);
      mxp = x;
      mxn = !x;
      myp = y;
      myn = !y;
    }

};

/** 4 phase unipolar with power down via nulling all pins. Next step will energize them again. It is a good idea to energize the last settings before changing them, but not absolutely required */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn> class ULN2003: public FourBanger<xp, xn, yp, yn> {
    using Super = FourBanger<xp, xn, yp, yn>;
  public:
    void operator()(byte step) {
      if (step == 255) { //magic value for 'all off'
        powerDown();
      }
      Super::operator()(step);
    }

    void powerDown() {
      //this-> or Super:: needed because C++ isn't yet willing to use the obvious base class.
      this->mxp = 0;
      this->mxn = 0;
      this->myp = 0;
      this->myn = 0;
    }
};

/** 4 unipolar drive, with common enable. You can PWM the power pin to get lower power, just wiggle it much faster than the load can react. */
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn, PinNumberType pwr,  unsigned polarity> class FourBangerWithPower: public FourBanger<xp, xn, yp, yn> {
    using Super = FourBanger<xp, xn, yp, yn>;
    OutputPin<pwr, polarity> enabler;
  public:
    void operator()(byte step) {
      if (step == 255) { //magic value for 'all off'
        powerDown();
      }
      enabler = 0;
      Super::operator()(step);
    }

    void powerDown() {
      enabler = 1;
    }
};


#if UsingUDN2540
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn, PinNumberType pwr> class UDN2540: public FourBangerWithPower<xp, xn, yp, yn, , pwr, HIGH> {};
UDN2540<18, 16, 17, 15, 19> minutemotor;
UDN2540<13, 11, 12, 10, 9> hourmotor;
#elif UsingDRV8833
template <PinNumberType xp, PinNumberType xn, PinNumberType yp, PinNumberType yn, PinNumberType pwr> class DRV8833: public FourBangerWithPower<xp, xn, yp, yn, , pwr, LOW> {};
DRV8833<5, 6, 7, 8, 3> minutemotor;
DRV8833<5, 6, 7, 8, 4> hourmotor;
#elif UsingULN2003
ULN2003<18, 16, 17, 15> minutemotor;
ULN2003<13, 11, 12, 10> hourmotor;
#else
#error "must define one of the driver classes such as UsingUDN2540, UsingDRV8833 ..."
#endif

#ifdef UsingD1
#define SharedDrive 1
#endif



ClockHand minuteHand(ClockHand::Minutes, [](byte step) {
  minutemotor(step);
});

ClockHand hourHand(ClockHand::Hours, [](byte step) {
  hourmotor(step);
});


//arduino has issues with a plain function name matching that of a class in the same file!
void upspeedBoth(unsigned msperstep) {
  minuteHand.upspeed(msperstep);
  hourHand.upspeed(msperstep);
}

void freezeBoth() {
  minuteHand.freeze();
  hourHand.freeze();
}

//now is reference position. Might be 5:00 a.m. for act II of Iolanthe
void highnoon() {
  minuteHand.setReference(0);
  hourHand.setReference(0);
}

MilliTick jerky0 = BadTick;
bool beJerky = false;


//interface to local logic
class ClockDriver {
  public:
    HMS target;//intermediary for RPN commands
    ClockDriver():
      target(0)
    {
      //#done.
    }

    void runReal(bool jerky) {
      if (jerky) {
        upspeedBoth(slewspeed);//move briskly between defined points.
        highnoon();
        jerky0 = MilliTicked.recent();
        beJerky = true;
      } else {
        minuteHand.realtime();
        hourHand.realtime();
      }
    }

    void setMinute(unsigned arg) {
      dbg("\nMinute time to:", arg);
      minuteHand.setTime(target.minute = arg);
    }

    void setHour(unsigned arg) {
      dbg("\nHour time to:", arg);
      hourHand.setTime(target.hour = arg);
    }



};

ClockDriver bigben;

//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP cmd;

ClockHand *hand = &minuteHand; //for tweaking one at a time
void reportHand(const ClockHand&hand, const char *which) {
  dbg("\n", which, "T=", hand.target, " en:", hand.enabled, " FR=", hand.freerun, " Step=", hand.mechanism);
}

//I2C diagnostic
#include "scani2c.h"


void doKey(char key) {
  switch (key) {
    case 'T': case 't': //run real time, smoothly
      bigben.runReal(false);
      break;

    case 'G': case 'g': //jumpy real time
      bigben.runReal(true);
      break;

    case ' '://report status
      reportHand(hourHand, "Hour");
      reportHand(minuteHand, "Minute");
      break;

    case 'h'://go to position
      dbg("\nHour stepping to:", cmd.arg);
      hourHand.setTarget(cmd.arg);
      break;

    case 'm'://go to position
      dbg("\nMinute hand to:", cmd.arg);
      minuteHand.setTarget(cmd.arg);
      break;

    case ':':
      bigben.setHour(cmd.arg);
      break;

    case ';':
      bigben.setMinute(cmd.arg);
      break;

    case 'Z'://declare present position is noon
      dbg("\n marking noon");
      highnoon();
      break;

    case 'v'://set stepping rate to use
      dbg("\nSetting step:", cmd.arg);
      upspeedBoth(cmd.arg);
      break;

    case 'x'://stop stepping of both
      dbg("\nStopping.");
      freezeBoth();
      beJerky = 0;
      break;

    case 'H':
      dbg("\nfocussing on Hour hand");
      hand = &hourHand;
      break;

    case 'M':
      dbg("\nfocussing on minute hand");
      hand = &minuteHand;
      break;

    case 'R'://free run in reverse
      dbg("\nRun Reverse.");
      hand->freerun = -1;
      break;

    case 'F'://run forward
      dbg("\nRun Forward.");
      hand->freerun = +1;
      break;

    case 'X'://stop just the one
      hand->freeze();
      break;

    case '?':
      scanI2C(dbg);
#if UsingEDSir
      dbg("\nIR device is ", IRRX.isPresent() ? "" : "not", " present");
#endif
      break;

    default:
      dbg("\nIgnored:", char(key), " (", key, ") ", cmd.arg, ',', cmd.pushed);
      return; //don't put in trace buffer
  }
}

void accept(char key) {
  if (cmd.doKey(key)) {
    doKey(key);
  }
}

void doui() {
  while (auto key = dbg.getKey()) { //only checking every milli to save power
    accept(key);
  }
}
#if UsingEDSir
/* EDS IR receiver:
     p     c     n
     <     >    ' '
     -     +     =
     0     %     &
     1     2     3
     4     5     6
     7     8     9
*/

char irkey() {
  char irc = IRRX.key(); //I2C read, 0.1 ms\

  switch (irc) {//will map these to char used by keybaord etc.
    case 'p'://run reverse
      return 'R';
    case 'c'://
      return 'x'; //stop tuning
      break;
    case 'n'://run forward
      return 'F';
      break;
    case '<':
      return 'H';
      break;
    case '>':
      return 'M';
      break;
    case ' '://run realtime
      return 't';
      break;
    case '-':
      return 'G';
      break;
    case '+':
      return 'g';
      break;
    case '=':
      return 'Z';
      break;
    case '%'://set hour
      return ':';
      break;
    case '&'://set minute
      return ';';
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return irc;
    case 0: //nothing there
      return 0;
    default:
      dbg("\nUnexpected IR code:", irc);
      return 0;
  }
}
#endif
/////////////////////////////////////

void setup() {
  Serial.begin(115200);
#if UsingEDSir
  IRRX.begin();//our sole i2c user
#endif
  upspeedBoth(slewspeed);
  minuteHand.stepperrev = baseSPR;
  hourHand.stepperrev = baseSPR;
  //here is where we would configure step duration.
  server.begin();
}

void loop() {
  if (MilliTicked) {
    server.onTick();
    if (beJerky /*jerky.isRunning()*/) { //set time and let target logic move the hand
      MilliTick elapsed = MilliTicked.since(jerky0); //this presumes we set the clock origin to 0 when we record jerky0.
      minuteHand.setFromMillis(elapsed);
      hourHand.setFromMillis(elapsed);
    }
    //if both need to run, minute gets priority
    if (!minuteHand.onTick()) {
      hourHand.onTick();
    }
    doui();
#if UsingEDSir
    if (sampleIR.perCycle()) {
      accept(irkey());
    }
#endif
  }

}
