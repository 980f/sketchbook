
#include <Arduino.h>  //help non-arduino IDE
#include "DFRobotDFPlayerMini.h"
#include "millievent.h"
DFRobotDFPlayerMini mplayer;

////////////////////////////////////
#include "pinclass.h"

#ifdef LED_BUILTIN
OutputPin<LED_BUILTIN> led;
#else
bool led;
#endif

#include "cheaptricks.h"

#include "easyconsole.h"
EasyConsole<decltype(SerialUSB)> dbg(SerialUSB, true /*autofeed*/);


//command line interpreter, up to two RPN unsigned int arguments.
#include "clirp.h"
CLIRP<uint16_t> cmd;//params are 16 bits.

void doKey(char key) {
  Char k(key);
  bool which = k.toUpper();
  switch (k) {
    case ' '://show status.
//      if (mplayer.available()) { //then at some time a message from it has been received.
//        auto mstatus = mplayer.readCommand();
//        auto marg = mplayer.read();
//        dbg("MS:", HEXLY(mstatus), "\t", HEXLY(marg), "\t", marg);
//      }
      break;
    case 'T':
//      while (!mplayer.Ready); //block, player is busy but won't tell us so.
      
      mplayer.play(cmd.arg);
      break;
    case 'V':
      mplayer.volume(cmd.arg);
      break;
    case 'D':
    	mplayer.ACK(0);//trying to match example packet to determine what checksum should be.
      mplayer.outputDevice(Medium::FLASH);
      break;
  }
}

void accept(char key) {
  if (cmd.doKey(key)) {
    doKey(key);
  }
}

//////////////////////////////////////////////////////////
void setup() {
  Serial1.begin(9600);//hardcoded baud rate of device.
  mplayer.begin(Serial1, true, true); //isAck and reset
//  playerReady = 0; //want this to be part of the begin which presently has a multi-second block in it.
//  playerReady = mplayer.outputDevice(Medium::SDcard);
}

void loop() {
  if (MilliTicked) {//at 9600 baud nothing happens fast ;)
    while (char key = dbg.getKey()) {
      accept(key);
    }
  }
}
