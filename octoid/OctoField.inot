/*
Description:
This code is to setup an arduino to record a button press sequence, and transmit it to a arduino that has the octobanger firmware already loaded onto it for field programming.
The hope is to make it as easy to program an arduino in the field as it is for a PicoBoo.

Operation:
-User will connect the FieldProgrammer to the octobanger
-Led on FieldProgrammer will blink till it is initialized
-User will press record button, which will start recording button sequence
-User will press buttons in the sequence they want the relays to trigger
-While pressing the buttons during the program sequence the relays on the Octobanger will trigger in response.
-Once complete the user will press the record button again to end the sequence, and the Field programmer will start sending the new sequence to the octobanger.


Contributors:
James Manley - james.manley@scareforacure.org


Pin Mapping
Arduino Nano
[0/1] Tx/Rx - Used to transmit seriall commands to OctoBanger Baud:115200
[2] Btn_1
[3] Btn_2
[4] Btn_3
[5] Btn_4
[6] Btn_5
[7] Btn_6
[8] Btn_7
[9] Btn_8
[11]Record Button

RJ45 connector - T586B 10/100 DC mode B
| OctoBanger
| [RX]  [TX]  [12VDC] [TX]  [GND]
  1 2   3     4 5     6     7 8
| [TX]  [RX]  [VIN]   [RX]  [GND]
| OctoField
// under normal cirucmstance the pairs on TX and RX are supposed to be opposite polarities for noise canceling, for our purposes we just wire the siganl to both for simplicity and signal reliance.


The array the data is stored in is using the SRAM of the arduino, you have to alloy 2x the space due to the array being coppied into the subfunction for transmission.
Nano SRAM : 2 KB - limit of 500 frames, worst case scenario of 25.5 seconds of recording
Mega SRAM : 8 KB - limit 2000 frames, worst case of 102 seconds

The sequence is stored EEPROM of the Octobanger, the EEPROM is about half the size of the sram, 
Nano EEPROM : 1KB
Mega EEPROM : 4KB


//11/8/21-James M
Was able to get it to record reliably I think the issue i was having before was that i was trying to use one nano to power the other, which doesn't provide enough power.
This time i opted to use a mega that was powered seperate form the octobanger and it seemed to have worked fine. Will need to restest with another nano just to make sure there wasn't something else causing the issues.

*/

//Libraries
#include <RBD_Timer.h>
#include <RBD_Button.h>


////////////////////////////////////////////////////////////////
/// V Define System Below V ///


///^ Define system above ^///
//////////////////////////////////////////////////////////
/// V leave code Below alone V ///

//inputs
RBD::Button Record(11);
RBD::Button Ch_1(2);
RBD::Button Ch_2(3);
RBD::Button Ch_3(4);
RBD::Button Ch_4(5);
RBD::Button Ch_5(6);
RBD::Button Ch_6(7);
RBD::Button Ch_7(8);
RBD::Button Ch_8(9);
RBD::Button Trigger(10);

//timers
RBD::Timer FrameTime(50); //sets the time for each from to be at 50 ms
RBD::Timer RecordBlink(200);// for blinking status led while recording

//outputs
int Indicator = 13;


//global variables
//bool = 1 bit / 0-1
bool recording = 0; // variable for tracking if recording is currently in progress 1 = yes
bool BlinkStatus = 0;

// byte = 1 byte / 0-255 values
byte Sequence[500][2]; // create empty 2d array with 2 columns, and more rows than could be needed to sequence [0]relay status int,[1]# of time steps int.
byte RelayStat = 0 ; // number representing on/off status of each relay, ch_1 = 1, ch_2 =2, ch_3 =4, ch_4=8 etc.
byte RelayStatLast = 0 ; // previous value for tracking changes
byte FrameCount = 0; // number of time segments frame is active



  
//word = 2 bytes / 0-65535
word Frame = 0 ; // frame number of sequence represents row in array

//int = 2 byte / (-32768)-32767





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////
///V- Sub Functions -V ///
//////////////////////////

void TransmitSeq(byte a[][2]){
//Programming hex commands:
//baud 115200
//@S = begin to receive new program
//XX 00 = 2x # of frames to be received (least byte greatest byte)
//___________ //V- this section repeats for each frame to be transmitted
//Xx = relay combo for each frame
//xx = # of 1/20 seconds frame is active
//------------------- //V- this section is required even if the state didn't change at the end
//00 = Relay combo for final frame // if programming on octobanger this could have a value, but for here its easier to just force to of
//00 = zero time duration for end of program
  word frametrack = 0;
  word linecount = 0;
  byte TwoByteArray[] ={'@','S'};

  Serial.write(TwoByteArray,2);
  //Serial1.write(TwoByteArray,2);
  //Serial.println("@S");
  for(int y = 499; y >=0 ; y--){// go through the array and find the last updated frame without 0 frame count, thats where our program stopped
    if(a[y][1] != 0){
      //this is the last part of our sequence
      frametrack = y;
      break; // weve already found the end we can stop looking now.
    }
  }
  
  linecount = ( frametrack + 2 )*2; // we need the total count of frames not the index, + the closing frame, multiplied by 2 to signify that we will be sending 2 lines for each frame
  
  byte low = linecount;
  byte high = linecount >> 8;
  TwoByteArray[0] = low;
  TwoByteArray[1] = high;

  Serial.write(TwoByteArray,2); //transmit the bottom 8 bits then the top 8 bits
  //Serial1.write(TwoByteArray,2); //transmit the bottom 8 bits then the top 8 bits
  //Serial.print("Number of frames hex: ");
  //Serial.print(low);
  //Serial.print(",");
  //Serial.println(high);
  
  for(int z = 0 ; z <= frametrack; z++){ // go through each frame and transmit both lines
    Serial.write(a[z][0]);  //relay combo
    Serial.write(a[z][1]);  //# of frames active
    //Serial1.write(a[z][0]);  //relay combo
    //Serial1.write(a[z][1]);  //# of frames active

    //Serial.print("Frame#:");
    //Serial.print(z+1 );
    //Serial.print(" : ");
    //Serial.print(a[z][0]);
    //Serial.print(",");
    //Serial.println(a[z][1]);
  }

  Serial.write(0); // go ahead and send the ending frame.
  Serial.write(0);

  //Serial1.write(0); // go ahead and send the ending frame.
  //Serial1.write(0);
  //Serial.println("00,00");
  //Serial.println("End Programming Sequence");


  
}

void SequenceStream(byte b){
//Streaming hex Commands:
//baud 115200
//@Mx = instantly tell octobanger to activate corresponding relays
//      //x= hex value of relay states  
//
//byte ThreeByteArray[] = {64,77,b};
  //Serial.println(b);
  byte ThreeByteArray[] = {'@','M',b};

  Serial.write(ThreeByteArray,3); // confirmed in labview that this would work.
  
  //Serial1.write(ThreeByteArray,3); // confirmed in labview that this would work.
  //Serial.print("@M");
  //Serial.println(b);
 
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////
///V-Main Functions-V///
//////////////////////// 

void setup(){
  Serial.begin(115200); // commuication with computer
  
  //Serial.begin(115200); // commuication with computer
  //Serial1.begin(115200); //override baud rate for uno, should also work for nano
  FrameTime.stop();
  pinMode(Indicator, OUTPUT);

  Record.setDebounceTimeout(40);
  Trigger.setDebounceTimeout(40);

  //while(!Serial.available()){
  //  delay(100);
  //}
  
}

void loop(){

  if(!recording && Record.onPressed()){
    recording = HIGH;
    FrameTime.restart();
    Frame = 0; // each time you start recoridng the frame number goes back to 0.
    for(int x=0; x<500; x++){ // make sure to clear the full array before starting programming.
      Sequence[x][0]=0;
      Sequence[x][1]=0; 
    }
    delay(20);
  }

  if(recording && Record.onPressed()){
    recording = LOW;
    FrameTime.stop();
    TransmitSeq(Sequence); // starts transmission of the collected array.
    delay(500);
  }

  if(Trigger.onPressed()){
    byte TwoByteArray[] ={'@','T'};
    Serial.write(TwoByteArray,2);
    //Serial1.write(TwoByteArray,2);
    //Serial.print("@T");
    delay(500);
  }
  
///^ INPUT BASED EVENTS GO ABOVE ^///
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///V TIMING BASED EVENTS GO BELOW V///

  if(recording && RecordBlink.onRestart()){
    BlinkStatus = !BlinkStatus;
    digitalWrite(Indicator, BlinkStatus);
  }
  if(!recording){
    BlinkStatus = 0;
    digitalWrite(Indicator, BlinkStatus);
  }

  if(FrameTime.onRestart()){ // should repeat every 50 ms, re-reading all the buttons and recording them.
    RelayStat = 0;
    if(Ch_1.isPressed()){
      RelayStat += 1;
    }
    if(Ch_2.isPressed()){
      RelayStat += 2;
    }
    if(Ch_3.isPressed()){
      RelayStat += 4;
    }
    if(Ch_4.isPressed()){
      RelayStat += 8;
    }
    if(Ch_5.isPressed()){
      RelayStat += 16;
    }
    if(Ch_6.isPressed()){
      RelayStat += 32;
    }
    if(Ch_7.isPressed()){
      RelayStat += 64;
    }
    if(Ch_8.isPressed()){
      RelayStat += 128;
    }
    //Serial.println(RelayStat);
    if(RelayStatLast == RelayStat){ // if the combination of relays is the same as last round, increase the counted frames in the array
      ++FrameCount;
      if(FrameCount <=255){
        Sequence[Frame][1] = FrameCount; //
      }
      else{ // if the frame count goes over 255 then it needs to be moved into a new frame
        ++Frame;
        FrameCount=0;
        Sequence[Frame][0] = RelayStat; // store the relay combo in column 1 of the array
        Sequence[Frame][1] = FrameCount; // when creating new frame the frames count is 0.
      }
    }
    else{ // if the relay stat is not the same as the last round, move to a new frame in the array
      ++Frame;
      FrameCount=0;
      Sequence[Frame][0] = RelayStat; // store the relay combo in column 1 of the array
      Sequence[Frame][1] = FrameCount; // when creating new frame the frames count is 0.
    }
    RelayStatLast = RelayStat;
    SequenceStream(RelayStat); // send the current relay combo to the arduino to have the relay's mimic the buttons in real time.
  }
  
  
}
