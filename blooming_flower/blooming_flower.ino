// Blooming flower mechanism, first used for Quest Night 2026 Swamping puzzle

#include "AccelStepper.h"
class AF_Motor : public AccelStepper {
  //only implement enough for bloomer
  //D12 is latch
  //D8 data
  //D4 clock
  //D7 en*, always low please
  //PWM_2A,D11 for motor1A
  //PWM_2B,D3 for motor1B
  enum {
    Latch=12,
    Data=8,
    Clock=4,
    Disabler=7,
    Adrive=11,
    Bdrive=3,
  };
  //shift order: M4B,M3B,M4A,M2B,M1B,M1A,M2A,M3A 
  //latch low, shift in the order above, latch high (then low)
  //clock is low, put datum on pin, clock high.
private:

  void shiftout(int mask,int bitnum=0){
    digitalWrite(Clock,0);
    digitalWrite(Data,bitRead(mask,bitnum));
    digitalWrite(Clock,1);
  }
public:

  AF_Motor():AccelStepper(AccelStepper::FULL4WIRE){}

  void setOutputPins(uint8_t mask){
    //have to shift data out via psuedo spi interface
      digitalWrite(Latch,0);//idle low
      //incoming data: M2B,M2A,M1B,M1A
      //device: M4B,M3B,M4A,M2B,M1B,M1A,M2A,M3A 
      shiftout(0);      
      shiftout(0);       
      shiftout(0);  
      shiftout(mask,3);
      shiftout(mask,1);      
      shiftout(mask,0);       
      shiftout(mask,2);  
      shiftout(0);
      
      digitalWrite(Latch,1);//mass update
      digitalWrite(Latch,0);//idle low    
  }

  void setup(){
    pinMode(Latch,OUTPUT);
    pinMode(Data,OUTPUT);
    pinMode(Clock,OUTPUT);
    pinMode(Disabler,OUTPUT);
    pinMode(Adrive,OUTPUT);
    pinMode(Bdrive,OUTPUT);
//always on bits:
    digitalWrite(Disabler, 0);
    
    power(0); //disable power to motor at startup
    setOutputPins(0); //choose a phase.
  }

  void power(bool beon){
    digitalWrite(Adrive, beon);
    digitalWrite(Bdrive, beon);
  }
};

using Motor = AF_Motor;// we keep on changing our mind which class to use ;)

//andy has a library that makes millisecond timing compact, this is a tiny bit of it.
using MilliTick = unsigned long;
MilliTick milliTicker = 0;

#include <EEPROM.h>  //for tweaking parameters without downloading a new program

//things that should be saved and restored from eeprom:
struct Puzzle {
  unsigned trackLength;  //number of steps from fully closed to fully open
  int sensorPin = A0;
  MilliTick sensorSamplingRate;
  //wetness above required+hysteresis starts opening, below required-hysteresis to start closing if it was opening.
  int WetnessRequired;
  int WetnessHysteresis;

  bool isWet(int wetness){
    return wetness < (WetnessRequired - WetnessHysteresis);
  }

  bool isDry(int wetness){
    return wetness > (WetnessRequired + WetnessHysteresis);
  }

  MilliTick maxTimeToOpen ;
  MilliTick maxTimeToClose;

  void builtins(){
    trackLength = 500;  //number of steps from fully closed to fully open
    sensorPin = A0;
    sensorSamplingRate = 257;
    WetnessRequired = 600;
    WetnessHysteresis = 20;
  }

  void info() {
    Serial.println("\tPuzzle Parameters:");
    Serial.println("SampRate\tWetThreshold\t+/-\t");
    Serial.print(sensorSamplingRate);
    Serial.print("\t");
    Serial.print(WetnessRequired);
    Serial.print("\t");
    Serial.print(WetnessHysteresis);
    Serial.println();
  }

  void load(){
    EEPROM.get(0, *this);
  }

  void save(){
    EEPROM.put(0,*this);
  }

} puzzle;

//state of bloom
enum Bloomer {
  Unknown,
  Homing,
  Opened,
  Closed,
  Opening,
  Closing,
};

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
  Bloomer blooming = Unknown;
  //keep hardware status handy for debug:
  struct state {
    bool Wet=false;
    bool Dry=false;
    bool Moving=false;
    long At = ~0;
  } is;
  
  BloomingFlower()
    : motor(),
      wetness(puzzle.sensorPin) {}

  void startOpening() {
    Serial.println("startOpening");
    blooming=Opening;
    motor.moveTo(puzzle.trackLength);
  }

  void startClosing() {
    Serial.println("startClosing");
    blooming=Closing;
    motor.move(0);
  }

  void rehome() {
    Serial.println("rehome");
    blooming = Homing;
    motor.setCurrentPosition(puzzle.trackLength);// worst case: guess that we are as far open as can be
    motor.move(0);
  }


  void onTick(MilliTick now) {
    wetness.onTick(now);
    //get all hardware state whether it matters or not, for debug convenience:
    is.Wet = puzzle.isWet(wetness.reading);
    is.Dry = puzzle.isDry(wetness.reading);

    is.At = motor.currentPosition();
    is.Moving = motor.distanceToGo()!=0;

    //state changes are here
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
          blooming = Closed;
           Serial.println("Homed");
          //todo: reduce motor power.
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
          blooming = Opened;
           Serial.println("Opened");
          //todo: reduce motor power.
        }
        break;

      case Opened:  //if sensor not on then start closing
        if(is.Dry){
          startClosing();          
        }      
        break;

      case Closing:  //presume sensor glitched and we should do a full reset
        if(!is.Moving){
          blooming = Closed;
           Serial.println("Closed");
          //todo: reduce motor power.
        }
        //perhaps:
        // if(is.Wet){
        //   startOpening();          
        // }
        break;      
    };
  }

  void showState(){
    //IDE lied about uno having printf: Serial.printf("\tWet:%d\tDry:%d\tMoving:%d\tStep:%ld\tMoisture: %d\tms: %d\n\n",is.Wet,is.Dry,is.Moving,is.At,wetness.reading,wetness.read);
    Serial.print("\tWet:");    
    Serial.print(is.Wet);
    Serial.print("\tDry:");
    Serial.print(is.Dry);
    Serial.print("\tMoving:");
    Serial.print(is.Moving);
    Serial.print("\tStep:");
    Serial.print(is.At);
    Serial.print("\tMoisture: ");
    Serial.print(wetness.reading);
    Serial.print("\tms: ");
    Serial.print(wetness.read);
    Serial.println();
  }

  void setup() {
    wetness.setup();
    //todo:  motor.setAcceleration(something)
    motor.setSpeed(200);//todo: a puzzle param for this.
  }
};
BloomingFlower flower;

///////////////////////////////
//arduino hooks:
void setup() {
  puzzle.load(); //reads puzzle configuration object from eeprom.
  flower.setup();

  Serial.begin(115200);//use same baud as downloader to eliminate crap in serial monitore window.
  Serial.println("\nBloomin' Flower!");  
}


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
  switch (tolower(key)) {
    default:
      if(key>0){
        Serial.print("\nUnknown command char:");
        Serial.println(key);
      }
      break;
    case '?':
      puzzle.info();
      break;
    case ' ':
      flower.showState();
      break;
    case 'b':
      flower.startOpening();
      break;
    case 'm':
      flower.startClosing();
      break;
    case '!':
      flower.setup();
      flower.rehome();
      break;
    case '\\':
      puzzle.builtins();
      break;
    case '|':
      puzzle.save();
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
