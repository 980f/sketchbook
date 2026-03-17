// AFMotor R4 Compatible Library
//blocking actions, bad juju! #include "AFMotor_R4.h"
// using Motor = AF_Stepper;

// //stub until we have non-blocking motor class.
// class Motor {
//   public:
//   Motor(unsigned,int);
//  void setSpeed(unsigned Hertz);
//  void step(unsigned, int fORWARD, int steptype);
// };


#include "AccelStepper.h"
using Motor = AccelStepper;

//andy has a library that makes millisecond timing compact, this is a tiny bit of it.
using MilliTick = unsigned long;
MilliTick milliTicker = 0;

//configuration of hardware

//things that should be saved and restored from eeprom:
struct Puzzle {
  unsigned trackLength = 500;  //number of steps from fully closed to fully open
  int sensorPin = A0;
  MilliTick sensorSamplingRate = 257;
  //wetness above required+hysteresis starts opening, below required-hysteresis to start closing if it was opening.
  int WetnessRequired = 500;
  int WetnessHysteresis = 20;
  MilliTick maxTimeToOpen = 9920;
  MilliTick maxTimeToClose = 6900;

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
    : motor(AccelStepper::FULL4WIRE),  // AccelStepper(uint8_t interface = AccelStepper::FULL4WIRE, uint8_t pin1 = 2, uint8_t pin2 = 3, uint8_t pin3 = 4, uint8_t pin4 = 5, bool enable = true);
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
    //get all hardware state whether it matters or not, for debug convenience.
    is.Wet = wetness < puzzle.WetnessRequired - puzzle.WetnessHysteresis;
    is.Dry = wetness > puzzle.WetnessRequired + puzzle.WetnessHysteresis;
    //motor guesses where it is at, don't know how reliably it homes.
    is.At = motor.currentPosition();
    is.Moving = motor.distanceToGo()!=0;

    //state changes are here
    switch (blooming) {
      default:
        Serial.println("Illegal blooming state");
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
        break;      
    };
  }

  void showState(){
    Serial.printf("\tWet:%d\tDry:%d\tMoving:%d\tStep:%ld\tMoisture: %d\tms: %d\n\n",is.Wet,is.Dry,is.Moving,is.At,wetness.reading,wetness.read);         
  }

  void setup() {
    wetness.setup();
    //todo:  motor.setAcceleration(something)
    motor.setSpeed(200);//todo: a puzzle param for this.
  }
};
BloomingFlower flower;

void setup() {
  Serial.begin(9600);
  Serial.println("Bloomin' Flower!");
  //todo: read puzzle configuration object from eeprom.
  flower.setup();
}

//loop is called way too often, we put our logic in a once per millisecond function above
void loop() {
  flower.motor.run(); //todo: see if we can do this on the millitick OR in a timer ISR.

  MilliTick now = millis();
  if (milliTicker != now) {  //once a millisecond
    milliTicker = now;
    flower.onTick(now);
  }
  switch (tolower(Serial.read())) {
    default:
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
  }
}

// Serial.println("Double coil steps");
// motor.step(1400, BACKWARD, DOUBLE);
// //open = 1;
// motor.step(1400, BACKWARD, DOUBLE);
// }
//  Serial.println("Interleaved coil steps");
// motor.step(100, FORWARD, INTERLEAVE);
//  motor.step(100, BACKWARD, INTERLEAVE);

// Serial.println("Microsteps");
//  motor.step(50, FORWARD, MICROSTEP);
//  motor.step(50, BACKWARD, MICROSTEP);
