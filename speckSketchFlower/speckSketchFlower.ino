////////////////////////////////////////////////
// Blooming flower mechanism, first used for Quest Night 2026 Swamping puzzle
//
// full control pins:
// motor board 3,4,(5,6,)7,8,(9,10,)11,12
// moisture sensor A0
// homeSensor ??
//
//
////////////////////////////////////////////////
////////////////////////////////////////////////
//timer version: InMotion is output to enable flower power
const int InMotion = 10;  //D10;
//forceOn is for testing, it bypasses most other logic
// and now it is 'puzzle reset', but I ain't changing the name until it actually works
const int forceOn = 8;  //D8
//low when flower is closed, else somewhat high (2.5V in first system, maybe wrong zener was used?)
//CAVEAT: reports high when power is off! Cannot check home without the device definitely being on (relay has to have activated, not just been told to be active).
const int homeSensor = 6;  //D6
////////////////////////////////////////////////
//made a separate function for debugging ESP32duino boot loops
void setupPin(int dx) {
  Serial.print("\ttaking pin ");
  Serial.print(dx);
  Serial.print('\t');
  pinMode(dx, OUTPUT);
}

void power(bool beon) {
  //Serial.print("\tpower:");
  //Serial.println(beon);
  digitalWrite(InMotion, beon);
}

void loop() {
  static unsigned long btnPressTime = 0;
  static int isSolved = 0;
  static unsigned long solveStart = 0;
  static int hasReset = 1;

  if((millis() % 2000) == 0) {
    Serial.print(" btnPressTime:"); Serial.print(btnPressTime);
    Serial.print(" isSolved:"); Serial.print(isSolved);
    Serial.print(" solveStart:"); Serial.print(solveStart);
    Serial.print(" hasReset:"); Serial.print(hasReset);
    Serial.print(" forceOn:"); Serial.print(digitalRead(forceOn));
    Serial.print(" wetVal:"); Serial.print(analogRead(A0));
    Serial.println();
  }
  
  if(btnPressTime == 0) {
    if(digitalRead(forceOn) == 0){
      btnPressTime = millis(); 
      isSolved = 0;
    }
  } else {
    if(digitalRead(forceOn)) {
      btnPressTime = 0;
      power(0);
      hasReset = 1;
    } else if((millis() - btnPressTime) > 100) {
      power(1);
    }
  }
  if(hasReset) {
    if(isSolved == 0 && analogRead(A0) < 800) {
      power(1);
      isSolved = 1;
      solveStart = millis();
    }
    if(isSolved == 1 && (millis() - solveStart) > 9000) {
      power(0);
      solveStart = 0;
      isSolved = 0;
      hasReset = 0;
    }
  }
  //if(digitalRead(forceOn) == 0) {
  //  power(1);
  //} else {
  //  power(0);
  //}
}

void setup() {
  pinMode(forceOn, INPUT_PULLUP);
  pinMode(InMotion, OUTPUT);
  
  power(0);
}
