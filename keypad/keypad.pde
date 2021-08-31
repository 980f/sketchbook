
int alePin = 19;
int wePin=5;
int keyPin=18;
int strobePin=4;
boolean kp;
int lastKey; //to debounce critical ones.
int autoReset=-15;

int lsSel=2;
int msSel=3;

enum Tracks {
  SilenceT,GreetingT,TryAgainT,OopsT};

void startTrack(int which){
  Serial.print("Track:");
  Serial.println(which,DEC);

  digitalWrite(lsSel,(which&1)?HIGH:LOW);
  digitalWrite(msSel,(which&2)?HIGH:LOW);
}

const int CountDownStartValue=6;//value from audio track... gets predecremented

enum Stages {
  Idle,         //waiting for a YES to "Present Biometrics".
  Welcome,      
  EnteringPin,
  CountingDown,
  Kablooie,     //delay for a while then go idle
};
int stage;


boolean beBlinking;
int blinkPhase;
const char * stageText[]={
  //23456789012345678901234"
  "PRESENT LOGIN BIOMETRICS",
  "ENTER YOUR ACCESS CODE",
  "RE-ENTER ACCESS CODE",
  "RETRIES EXCEEDED",
  "HAVE A NICE DEATH :)",
};

void setStage(int ns){
  Serial.print("\n setStage: from: [");
  Serial.print(stageText[stage]);
  Serial.print("] to [");
  Serial.println(stageText[ns]);
  
  if(stage!=ns){
    switch(ns){
    case Idle:
      stopCountdown(); 
      startTrack(SilenceT); //which is silence.
      break;
    case Welcome: 
      clearPasscode();
      startTrack(GreetingT);
      break;
    case EnteringPin: 
      clearPasscode();
      startTrack(TryAgainT);
      break;
    case CountingDown: 
      clearPasscode();
      startTrack(OopsT);
      break;
    case Kablooie: 
      break;
    }
  }
  stage=ns;
  showString(stageText[stage]);
}

//address/data bus pins:
#define bit4pin(pin)  (1<<((pin)-6))
#define forAdb for(int pin=14;pin -->6;)

//write out 8 bits
void adbData(int octet){
  forAdb {
    digitalWrite(pin,(octet & bit4pin(pin))?HIGH:LOW);
  }
}

//write an address
void select(int octet){
  digitalWrite(alePin,LOW);
  adbData(octet);
  digitalWrite(alePin,HIGH);
}

void deselect(void){
  select(36);
}

//write data to last select'ed address
void writeByte(int luno,int octet){
  select(luno);
  delayMicroseconds(100);
  digitalWrite(wePin,LOW);
  adbData(octet);
  delayMicroseconds(90);
  digitalWrite(wePin,HIGH);
  delayMicroseconds(1);
  deselect();
} 

//write segments to a 7-segment display element
void writeDigit(int digit,int segments){
  writeByte(24+(digit&7),segments);
}

char alphaDisplay[24];

void writeChar(int loc,char ascii){
  if(ascii>='a' && ascii <='z'){
    ascii+='A'-'a';
  }
  writeByte(loc, alphaDisplay[loc]= ascii|128);
}

//read keys from keyboard
int readKey(){
  int code=0;
  select(32);//enables keypad output drivers
  delayMicroseconds(100);
  for(int pin=18,bm=1<<3;pin -->14;bm>>=1){
    if(digitalRead(pin)==HIGH){
      code|=bm;
    }
  }
  deselect();//this clears 'key present' indicator, unless there is already another key. It also disables the keypad drivers.
  return code;
}

int ascii4code(int keycode){
  return "0123456789\nN-\r.Y"[keycode];
}

boolean keyPresent(){
  return digitalRead(keyPin)==LOW;
}

//segments for display:
unsigned char numDisplay[8];
unsigned char decimalSegments[]={
  0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6F,
  0x77,//10: doesn't seem to happen, hiding under the Enter key?
  0x7C,//11: No
  0x39,//12: -
  0x5e,//13: \r
  0x79,//14: '.'
  0x71 //15: Yes
};

//char oh=0x5C;
//char arr=0x50;
//char blank=0;
//char capE=0x79;
//
//char faceoff[]={
//  0x71,0x77,0x39,0x79,0x00,0x3f,0x71,0x71};
//char deadbeef[]={
//  0x5e,0x79,0x77,0x5e,0x7C, 0x79,0x79,0x71};
char goboom[]={
  0,0,0,0,0,0x80,0x80,0x80};

void clearNumber(boolean andShow=1){
  clearPasscode();
  for(int digit=8;digit-->0;){
    numDisplay[digit]=0;
  }
  if(andShow){
    refreshNumDisplay();
  }
}

void showString(const char *text){
  for(int ci=24;ci-->0;){
    writeChar(ci,(*text?*text++:' '));
  }
}

//void shiftAlpha(char rhs=' '){
//  for(int ci=24;ci-->1;){
//    alphaDisplay[ci]=alphaDisplay[ci-1];
//  }
//  alphaDisplay[0]=rhs;
//}

void refreshAlpha(void){
  showString(alphaDisplay);
}

void refreshNumDisplay(){
  for(int digit=8;digit-->0;){
    writeDigit(digit,numDisplay[digit]);
  }
}

double passcode;
int beenDotted;
double pow10;
void clearPasscode(void){
  passcode=0.0;
  beenDotted=0;
  pow10=1.0;
}


void shiftNum(int code){
  if(code==14){//decimal point
    if(!beenDotted){
      numDisplay[0]|=0x80;
    }
  } 
  else {
    for(int digit=8;digit-->1;){
      numDisplay[digit]=numDisplay[digit-1];
    }
    numDisplay[0]=decimalSegments[code];
  }
}

void numBackspace(int segments){
  for(int digit=0;digit<7;digit++){
    numDisplay[digit]=numDisplay[digit+1];
  }
  numDisplay[7]=segments;  
}

boolean addToPasscode(int code){
  if(code==14){
    ++beenDotted;
  } 
  else if(code>=0 && code<=9){
    if(beenDotted){
      pow10/=10.0;
      passcode+=code*pow10;
    } 
    else {
      passcode*=10;
      passcode+=code;
    }
    return true;
  } 
  else {
    return false;
  }
}


void numString(char *segmented){
  for(int digit=8;digit-->0 &&*segmented;){
    numDisplay[digit]=*segmented++;
  }
}

int going=0;
unsigned long gtime;
unsigned grate=1000;//millis.

void startCountdown(){
  going=CountDownStartValue;
  gtime=millis()+grate;
  digitalWrite(strobePin,HIGH);
}

void stopCountdown(){
  digitalWrite(strobePin,HIGH);
  going=0;
}

int acursor;

void onYes(void){
  Serial.println("\nonYes: ");
  switch(stage){
  case Idle: 
    setStage(Welcome);
  
    break;
  case Welcome: 
    setStage(EnteringPin);

    break;
  case EnteringPin: 
    onEnter();
    break;
  case CountingDown: 
    break;
  case Kablooie: 
    break;
  }
}

void onNo(){
  Serial.print("\nonNo:");
  if(beenDotted>3){
    Serial.print(" dotted idle:");
    setStage(Idle);
  }
  clearNumber();
  switch(stage){
  case Idle: 
    break;
  case Welcome: 
    break;
  case EnteringPin: 
    break;
  case CountingDown: 
    break;
  case Kablooie: 
    break;
  }
}

void onEnter(void){
  Serial.print("\nonEnter:");
  switch(stage){
  case Idle: 
    setStage(Idle);
    break;
  case Welcome: 
    clearNumber();
    setStage(EnteringPin);
    break;
  case EnteringPin: 
    clearNumber();
    startCountdown();
    setStage(CountingDown);
    break;
  case CountingDown: 
    setStage(Idle);
    break;
  case Kablooie: 
    setStage(Idle);
    break;
  }
}

void process(int code){
  Serial.print("\nprocess code:");
  Serial.print(code,DEC);
  Serial.print(" last:");
  Serial.print(lastKey,DEC);
  switch(code){
  default: //0-9,.
  case 14://decimal point key
    shiftNum(code);
    break;        
  case 10://may be the other key under the ENTER button.
  case 13://ENTER
    if(lastKey!=code){
      onEnter();
    }
    break;
  case 12://- backspaces
    numBackspace(0);
    break;
  case 11://no
    if(lastKey!=code){
      onNo(); //clearNumber(1);
    }
    break;
  case 15://yes
    if(lastKey!=code){          
      onYes();
    }
    break;
  }

  lastKey=code;
  refreshNumDisplay();
}


void onTick(void){
  switch(stage){
  case CountingDown:
    clearNumber(false);
    if(going>=10){
      shiftNum(going/10);
    }
    shiftNum(going%10);      
    refreshNumDisplay();
    if(going==0){
      digitalWrite(strobePin,HIGH);
      setStage(Kablooie);
    } 
    break;
  case Kablooie:
    if(going<autoReset){
      setStage(Idle);
    }

    break;  
  } 
}


void setup() {
  Serial.begin(19200);

  forAdb{
    pinMode(pin, OUTPUT);      
  }

  pinMode(alePin,OUTPUT);
  digitalWrite(alePin,HIGH);    

  pinMode(wePin,OUTPUT);
  digitalWrite(wePin,HIGH);    

  pinMode(keyPin,INPUT);
  for(int pin=18;pin -->14;){
    pinMode(pin,INPUT);
  }

  pinMode(strobePin,OUTPUT);
  digitalWrite(strobePin,LOW);  

  pinMode(lsSel,OUTPUT);
  pinMode(msSel,OUTPUT);
  startTrack(SilenceT);

  clearNumber();
  setStage(Idle);
  acursor=0;
  kp=keyPresent();
  gtime=millis();

  lastKey=-1;//none
}

void loop() {
  int echo = Serial.read();
  if(echo>=0){
    Serial.write(echo);
    if(echo>='0'&&echo<='3'){
      startTrack(echo-'0');
      return;
    }

    if(echo=='r'){
      process(13);
    }
    if(echo='y'){ 
      process(15);
    }
    if(echo='n'){
      process(11);
    }
  }

  if(keyPresent()!=kp){
    kp=!kp;
    if(kp){ 
      int code=readKey();
      echo=ascii4code(code);
      Serial.write(echo);
      process(code);
    }
  } 

  unsigned long now=millis();
  if( now>gtime || now<grate){
    gtime+=grate;
    --going;
    onTick();
  }
}

//end of file.










