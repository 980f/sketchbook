#include "RBD_Timer.h"

struct Flickery {
  bool ison; 
  unsigned range[2];
  RBD::Timer Led_On;
  RBD::Timer Led_Off;

  Flickery(unsigned ontime, unsigned offtime):Led_On(ontime), Led_Off(offtime){
    ison=false;
    range[0]=offtime;
    range[1]=ontime;
  }

  void setup() {
    Led_On.onExpired(); // clears the registry for initialization
    Led_Off.onExpired();
    Led_Off.restart(); // restarts the off cycle timer
  }
  
  void onTick() {
    if (Led_Off.onExpired()) { // runs this code each time the timer expires
      be(false);
      Serial.println("LED %d On" /*,Led_Pin*/);
      Led_On.setTimeout(random(range[0],range[1])); // starts the timer for the on duration
    }

    if (Led_On.onExpired()) { // runs this code each time the timer expires
      be(true);
      Serial.println("LED %d Off" /*,Led_Pin*/);
      Led_Off.setTimeout(random(range[0],range[1])); 
    }

  }

  virtual void be(bool on){
    ison=on;
  }
};

struct FlickeryPin: public Flickery {
  int Led_Pin;
  FlickeryPin(int apin, unsigned ontime, unsigned offtime):Flickery(ontime, offtime),Led_Pin(apin){
    //nada.
  }
 virtual void be(bool on){
    Flickery::be(on);
    digitalWrite(Led_Pin, on?HIGH:LOW); 
    if(on){
      Led_On.restart();
    } else {
      Led_Off.restart();
    }   
  }
};

FlickeryPin led[]={
  {13,201,400},
  {12,302,500},
  {11,100,703},
  {10,204,500},
};

static const int numleds=sizeof(led)/sizeof(FlickeryPin);

//import PCF7475 class here and pack the led array into it.

unsigned packed=0;

void packout(){
  unsigned newly=0;
  for(unsigned count=numleds;count-->0;){
    if(led[count].ison){
      newly |= 3<<(1+2*count);//this is how a particular board is wired, setting pairs of pins in case we need to boost drive.
    }
  }
  if(packed!=newly){
    packed=newly;
    //todo: send packed to I2C.
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Begin setup");
  for(unsigned count=numleds;count-->0;){
    led[count].setup();
  }
  
  packed=~0;//will force output
  packout();
  
  Serial.println("setup complete");
}

void loop() {
  //todo: only run the loop once a millisecond to lower power consumption, might be able to run on batteries if we do instead of needing another P/S.
  for(unsigned count=numleds;count-->0;){
    led[count].onTick();
  }

  //copy bits out to I2C
  packout();

}
