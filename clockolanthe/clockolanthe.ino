
#include "options.h"
//todo: leonardo and separate ESP01, both sources with lots of #ifdef

////////////////////////////////////
#include "pinclass.h"
#include "cheaptricks.h"
#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);


//soft millisecond timers are adequate for minutes and hours.
#include "millievent.h"
Using_MilliTicker

#if ServeWifi
#include "clockserver.h"
ClockServer server(1859, "bigbender", dbg);
#endif

#if UsingEDSir
#include "edsir.h"
EDSir IRRX;
MonoStable sampleIR(147);//fast enough for a crappy keypad
#endif


#include "stepper.h"
#include "motordrivers.h"
#include "clockhand.h"


//project specific values:
const unsigned baseSPR = 2048;//28BYJ-48
const unsigned slewspeed = 5;//5: smooth moving, no load.

//#if UsingUDN2540
//UDN2540<18, 16, 17, 15, 19> minutemotor;
//UDN2540<13, 11, 12, 10, 9> hourmotor;
//#elif UsingDRV8833
//#  ifdef UsingD1
//#    define SharedDrive 1
//#error "faked out shared drive"
////DRV8833<D5, D6, D7, D8, D3> minutemotor;
////DRV8833<D5, D6, D7, D8, D4> hourmotor;
//DRV8833<D5, D5, D5, D5, D5> minutemotor;
//DRV8833<D5, D5, D5, D5, D5> hourmotor;
//#  else
//DRV8833<27,26,25,33,32> minutemotor;
//DRV8833<16,17,5,18,19> hourmotor;
//#  endif
//#elif UsingULN2003
//ULN2003<18, 16, 17, 15> minutemotor;
//ULN2003<13, 11, 12, 10> hourmotor;
//#else
//#error "must define one of the driver classes such as UsingUDN2540, UsingDRV8833 ..."
void minutemotor(byte step){}
void hourmotor(byte step){}
//#endif

///////////////////////////////////////////////////////////////////////////
 
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

#include "clockdriver.h"

//interface to local logic
ClockDriver::ClockDriver():
  target(0)
{
  //#done.
}

void ClockDriver::runReal(bool jerky) {
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

void ClockDriver::setMinute(unsigned arg, bool raw) {
  if (raw) {
    dbg("\nMinute step to:", arg);
    minuteHand.setTarget(arg);
  } else {
    dbg("\nMinute time to:", arg);
    minuteHand.setTime(target.minute = arg);
  }
}

void ClockDriver::setHour(unsigned arg, bool raw) {
  if (raw) {
    dbg("\nHour stepping to:", arg);
    hourHand.setTarget(arg);
  } else {
    dbg("\nHour time to:", arg);
    hourHand.setTime(target.hour = arg);
  }
}

void reportHand(const ClockHand&hand, const char *which) {
  dbg("\n", which, "T=", hand.target, " en:", hand.enabled, " FR=", hand.freerun, " Step=", hand.mechanism);
}


void ClockDriver::dump() {
  reportHand(hourHand, "Hour");
  reportHand(minuteHand, "Minute");
}

ClockDriver bigben;

//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP cmd;

ClockHand *hand = &minuteHand; //for tweaking one at a time

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
      bigben.dump();
      break;

    case 'h'://go to position
      bigben.setHour(cmd.arg, true);
      break;

    case 'm'://go to position
      bigben.setMinute(cmd.arg, true);
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
  while(!Serial);

	while(1){
		Serial.print('A');
		delay(550);
	}
  
  dbg("setup");
#if UsingEDSir
  dbg("setup edsir");
  IRRX.begin();//our sole i2c user
#endif
  dbg("setup upsspeed");
  upspeedBoth(slewspeed);
  dbg("setup basespr");
  minuteHand.stepperrev = baseSPR;
  hourHand.stepperrev = baseSPR;
  //here is where we would configure step duration.
#if ServeWifi
  dbg("setup server");
  server.begin();
#endif
}

unsigned looped = 0;

void loop() {
  if (!looped) {
    dbg("loop first");
  }
  if (MilliTicked) {
#if ServeWifi
    server.onTick();
#endif
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
  //did not fix wdt. yield();//
  ++looped;
}
