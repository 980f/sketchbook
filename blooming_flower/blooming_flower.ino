////////////////////////////////////////////////
// Blooming flower mechanism, first used for Quest Night 2026 Swamping puzzle
// 
// pins: 
// motor board 3,4,(5,6,)7,8,(9,10,)11,12
// sensor A0
// opened switch A1 = D15
// closed switch A2 = D16
//
//
// 
////////////////////////////////////////////////
//original board: #define UnoR3

//Andy's quick hack cause his leonardos are all at Scare:
//#define ProMicro

#include "AccelStepper.h"

//AccelStepper does not have a nativeimplementationfor the SPI like motor shield, but allows an override
class SPI_Motor : public AccelStepper {
  //only implement enough for bloomer
  enum {
 
#if defined(ProMicro)
//jumpered a few pins differently than stacking boards does.
    Latch = 2, //no 12
    Data = 8,
    Clock = 4,
    Disabler = 7,//may be bypassed to ground
   
    Adrive = 6, //no 11, 6 is M4 pwr.
    Bdrive = 3,
 #else
    Latch = D12,
    Data = D8,
    Clock = D4,
    Disabler = D7,
    Adrive = D11,
    Bdrive = D3,
#endif

  };
  //shift order: M4B,M3B,M4A,M2B,M1B,M1A,M2A,M3A 
  //latch low, shift in the order above, latch high (then low)
  //clock is low, put datum on pin, clock high.
private:

  void shiftout(int mask,int bitnum=0){
    digitalWrite(Clock,0);
    auto b=bitRead(mask,bitnum);
    Serial.print(b);
    digitalWrite(Data,b);
    digitalWrite(Clock,1);
  }

public:
  bool doitOurselves=false; //how to shift out bits. Delete once the system actually works.

  SPI_Motor():AccelStepper(AccelStepper::FULL4WIRE){}

  //override base class's direct pin stuff, compiler is balking at 'override' keyword, must be an older version of C++
  void setOutputPins(uint8_t mask){
    Serial.print("\nstep:");
    Serial.print(mask,BIN);// 5->6->10->9
    Serial.print('\t');
    //have to shift data out via psuedo spi interface
    digitalWrite(Latch,0);//idle low
    //incoming data: M2B,M2A,M1B,M1A
    //device: M4B,M3B,M4A,M2B,M1B,M1A,M2A,M3A 
    if(doitOurselves){
      shiftout(0); //M4B
      shiftout(0); //M3B
      shiftout(0); //M4A
      shiftout(mask,3);//M2B
      shiftout(mask,1);//M1B      
      shiftout(mask,0);//M1A       
      shiftout(mask,2);//M2A  
      shiftout(0); //M3A
    } else {
      //to use shiftOut library function we have to swap around the incoming bits
      int garbled=((mask&3)<<2);//0 and 1 in place
      if(bitRead(mask,3)){
        garbled |= 1<<4;
      }
      if(bitRead(mask,2)){
        garbled |= 1<<1;
      }
      Serial.print(garbled,BIN);//FYI: this print preceding shiftOut but following latch->0 creates a tangible delay from latch low to first data clock.
      shiftOut(Data,Clock,MSBFIRST,garbled);
      
    }

    digitalWrite(Latch,1);//mass update
  }

  void setup(){
    Serial.print("\nSPI_Motor taking its pins\n");
    //SPI like interface bits
    pinMode(Latch,OUTPUT);
    pinMode(Data,OUTPUT);
    pinMode(Clock,OUTPUT);
    //pwm motor controls need to be driven, even if we don't pwm them
    pinMode(Adrive,OUTPUT);
    pinMode(Bdrive,OUTPUT);

    pinMode(Disabler,OUTPUT);

    digitalWrite(Disabler, 0);//the board curiously pulls up this signal that must be low for the board to function properly.
    //set a known state for consistency
    power(0); //disable power to motor at startup
    setOutputPins(0); //secondary way to power down.
  }

  void power(bool beon){
    Serial.print("\tpower:");
    Serial.print(beon);
    //use the pwm capable controls as power control.
    digitalWrite(Adrive, beon);
    digitalWrite(Bdrive, beon);
  }
};

/////////////////////////////////////////////
//for motor board with HC595 replaced with jumpers
class HackedMotor : public AccelStepper {
  //only implement enough for bloomer
  enum {

#if defined(ProMicro)
//no longer valud
    M1A=7,    
    M1B=16,   //no 12 
    M2A=8,
    M2B=4,
    
    Adrive = 10, //no 11
    Bdrive = 3,
#elif defined(UnoR3)
    M1A=7,    
    M1B=12,
    M2A=8,
    M2B=4,
    
    Adrive = 11,
    Bdrive = 3,
#else
    M1A=7,
    M1B=6,
    M2A=5,
    M2B=4,
    
    Adrive = 8,
    Bdrive = 3,
#endif

  };

  public:

  HackedMotor():AccelStepper(AccelStepper::FULL4WIRE){}
  void setOutputPins(uint8_t mask){
    Serial.print("\nstep:");
    Serial.print(mask);// 5->6->10->9
    Serial.print('\t');
    //break before make on each pair
    digitalWrite(M1A,0);
    digitalWrite(M1B,0);
    digitalWrite((bitRead(mask,0)?M1B:M1A) , 1);
    
    digitalWrite(M2A,0);
    digitalWrite(M2B,0);
    digitalWrite((bitRead(mask,2)?M2B:M2A) , 1);

  }

  void setup(){
    Serial.print("\nAF_Motor taking its pins");
    //pwm motor controls need to be driven, even if we don't pwm them
    pinMode(Adrive,OUTPUT);
    pinMode(Bdrive,OUTPUT);

    //set a known state for consistency
    power(0); //disable power to motor at startup
    //direct outputs to L293B
    digitalWrite(M1A,0);
    pinMode(M1A,OUTPUT);

    digitalWrite(M1B,0);
    pinMode(M1B,OUTPUT);
    
    digitalWrite(M2A,0);
    pinMode(M2A,OUTPUT);

    digitalWrite(M2B,0);
    pinMode(M2B,OUTPUT);
    
  }

  void power(bool beon){
    Serial.print("\tpower:");
    Serial.print(beon);
    //use the pwm capable controls as power control.
    digitalWrite(Adrive, beon);
    digitalWrite(Bdrive, beon);
  }

};
//picking at compile time for now.
using Motor = SPI_Motor;
//using Motor = HackedMotor;// we keep on changing our mind which class to use ;)

//andy has a library that makes millisecond timing compact, this is a tiny bit of it.
using MilliTick = unsigned long;
MilliTick milliTicker = 0;

#include <EEPROM.h>  //for tweaking parameters without downloading a new program
//esp32 needs eeprom size and init, worked without that on uno.
#define EEPROM_SIZE 1024
//things that should be saved and restored from eeprom:
struct Puzzle {
  //wetness above required+hysteresis starts opening, below required-hysteresis to start closing if it was opening.
  unsigned WetnessRequired;
  unsigned WetnessHysteresis;
  //motor tuners:  The below should be floats but making them ints allows for shared tweaking code, and is resolution enough for this puzzle.
  unsigned Acceleration; //steps per second per second
  unsigned MaxSpeed;     //steps per second
  
  unsigned trackLength;  //number of steps from fully closed to fully open
  unsigned sensorPin;
  unsigned sensorSamplingRate;
  
  bool isWet(int wetness){
    return wetness < (WetnessRequired - WetnessHysteresis);
  }

  bool isDry(int wetness){
    return wetness > (WetnessRequired + WetnessHysteresis);
  }

  void builtins(){
    trackLength = 100;  //number of steps from fully closed to fully open, may be set low for debug!
    sensorPin = A0;
    sensorSamplingRate = 167;
    WetnessRequired = 600;
    WetnessHysteresis = 20;

    Acceleration=7;
    MaxSpeed=98;
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

  void load(){
    EEPROM.get(0, *this);
  }

  void save(){
    EEPROM.put(0,*this);
    #ifdef ESP32
    EEPROM.commit();
    #endif
  }

  void setup(){
    EEPROM.begin(EEPROM_SIZE);
    load();
  }

} puzzle;


struct Sensor {
  int ain;
  MilliTick readInterval;  //how often to read the sensor, so that diagnostics don't swamp us.
  unsigned reading ;   //raw analog value
  MilliTick read ;      //time sensor was last read
  
  bool operator > (unsigned threshold){
    return reading > threshold;
  }

  bool operator <(unsigned threshold){
    return ! operator > (threshold);
  }

  Sensor(int ainPin)
    : ain(ainPin),
      readInterval(puzzle.sensorSamplingRate),
      reading(~0),  //init to something wildly impossible
      read(0){        
    //nothing needed here
  }

  void setup() {
    pinMode(ain,INPUT);//in case we dynamically reassign pins.
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
  Motor motor;
  Sensor wetness; 
  int openSwitch;
  int closeSwitch;
  //state of bloom
  enum Bloomer {
    Unknown,
    Homing,
    Opened,
    Closed,
    Opening,
    Closing,
  };

  Bloomer blooming = Unknown;
  //keep hardware status handy for debug:
  struct state {
    bool Wet=false;
    bool Dry=false;
    bool Moving=false;
    long At = ~0;
    bool Open = false;  //limit switch
    bool Closed = false;//limit switch
  } is;
  
  BloomingFlower()
    : motor(),
      wetness(puzzle.sensorPin) {}

  void startOpening() {
    Serial.print("\nstartOpening");
    motor.power(1);
    blooming=Opening;
    motor.moveTo(puzzle.trackLength);
  }

  void startClosing() {
    Serial.print("\nstartClosing");
    motor.power(1);
    blooming=Closing;
    motor.moveTo(0);
  }

  void rehome() {
    Serial.print("\nrehome");
    motor.power(1);
    blooming = Homing;
    motor.setCurrentPosition(puzzle.trackLength);// worst case: guess that we are as far open as can be
    motor.moveTo(0);
  }

  void stop(Bloomer be,const char *why){
    blooming = be;
    Serial.print('\n');
    Serial.print(why);
    motor.move(0);//truncate attempts to move
    motor.power(0);//the mechanism has enough friction that we don't need the motor to hold it in place.

    showState();
  }

  void onTick(MilliTick now) {
    wetness.onTick(now);
    //get all hardware state whether it matters or not, for debug convenience:
    is.Wet = puzzle.isWet(wetness.reading);
    is.Dry = puzzle.isDry(wetness.reading);
    is.Open = digitalRead(openSwitch);
    is.Closed = digitalRead(closeSwitch);

    is.At = motor.currentPosition();
    is.Moving = motor.distanceToGo()!=0;

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
        if(! is.Moving){
          stop(Closed,"Homed");
        }
        break;
      
      case Closed: //most of the time we are here, waiting for the customer to dunk orbees into the bucket.
        if(is.Wet){
          startOpening();          
        }
        break;
   
      case Opening:
        //todo: check fail safe timer and stop stepper
        if(!is.Moving){
          stop(Opened,"Opened");
        }
        break;

      case Opened:  //if sensor not on then start closing
        if(is.Dry){
          startClosing();          
        }      
        break;

      case Closing:  //presume sensor glitched and we should do a full reset
        if(!is.Moving){
          stop(Closed,"Closed");
        }
        //perhaps:
        // if(is.Wet){
        //   startOpening();          
        // }
        break;      
    };
  }

  template <typename SomePrintableType> void showItem(const char *symbol,SomePrintableType datum){
    Serial.print("\t");
    Serial.print(symbol);
    Serial.print(":");    
    Serial.print(datum);
    
  }
  void showState(){
    //IDE lied about uno having printf:
    Serial.println();

    showItem("Wet",is.Wet);
    showItem("Dry",is.Dry);
    showItem("Moving",is.Moving);
    showItem("Step",is.At);

    showItem("Olim",is.Open);
    showItem("CLim",is.Closed);

    showItem("Moisture",wetness.reading);
    showItem("ms",wetness.read);
    
    Serial.println();
  }

  void setupSwitch(int &switchee,int Dpin){
    pinMode(Dpin,INPUT_PULLUP);
    switchee=digitalRead(Dpin);
  }

  void setup() {
    Serial.println("\nFlower Setup");
    wetness.setup();
    motor.setup();
    motor.setAcceleration(puzzle.Acceleration);
    motor.setMaxSpeed(puzzle.MaxSpeed);
    motor.setSpeed(puzzle.MaxSpeed);
    setupSwitch(openSwitch,15);//15 should get us A1 as digital
    setupSwitch(closeSwitch,16);//16 for A2
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
  Serial.begin(DebugBaud);//use same baud as downloader to eliminate crap in serial monitore window.
  delay(1000);//to let UNO serial monitor reliably get the following text
  Serial.println("\nBloomin' Flower!");  
  puzzle.setup(); //reads puzzle configuration object from eeprom. This is kept separate from flower.setup() so that we can see how flower will work with temporarily altered puzzle parameters. see '!' debug action.
  flower.setup(); //rest of setup.
}

//returns whether char was applied
bool romanize(unsigned &number,char letter){
  bool isUpper=letter <= 'Z';
  int magnitude=0;//init in case we get stupid later

  switch(tolower(letter)){
    case 'i':
      magnitude=1; break;
    case 'v':
      magnitude=5; break;
    case 'x':
      magnitude=10;break;
    case 'l':
      magnitude=50; break;
    case 'c':
      magnitude=100; break;
    case 'd':
      magnitude=500; break;
    case 'm':
      magnitude=1000; break;
    default:
      return false;
  }
  if(isUpper){
    number += magnitude;
  } else {
    number -= magnitude;//user beware that negative numbers are instead treated as really large.
  }

  return true;  
}

unsigned *tweakee = &puzzle.WetnessRequired;//legacy init

void loop() {
  //this stepper library checks micros in the following call and steps motor etc.
  flower.motor.run(); //todo: see if we can do this on the millitick OR in a timer ISR.
  
  //loop is called way too often, we put our logic in a once per millisecond function above
  MilliTick now = millis();
  if (milliTicker != now) {  //once a millisecond
    milliTicker = now;
    flower.onTick(now);
  }
  //debug actions
  auto key=Serial.read();
  //if a roman numeral letter then tweak some number
  if(tweakee && romanize(*tweakee, key)){ //uses CDILMVX
    return;
  }
  //not a number tweak so do something else.
  switch (tolower(key)) {
    default:
      if(key>0){
        Serial.print("\nUnknown command char:");
        Serial.println(key);
      }
      break;
    case 'p':
      flower.motor.power(1);
      Serial.print("\nMotor power enabled");
      break;
    case 'o':
      flower.motor.power(0);
      Serial.print("\nMotor power disabled");
      break;
    case '?':
      puzzle.info();
      break;
    case ' ':
      flower.showState();
      break;
    case '.':
      flower.motor.move(0);//or perhaps power off?
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
    case '\\'://reset to compiled in values, needed on first load of a signficantly new program
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
      Serial.print("\nTweaking track length");
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

    case 13: case 10: //ignore these, arduino2 serial monitor sends newlines when it sends what you type.
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
