
#include <Arduino.h>  //help non-arduino IDE

#define DF_Include_Helpers 1
#include "DFRobotDFPlayerMini.h"

#include "millievent.h"

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


//I2C diagnostic
#include "scani2c.h"

DFRobotDFPlayerMini mplayer;

void onReply(uint8_t opcode, uint16_t param) {
  dbg("Response: ", opcode, ':', param);
}

void doKey(char key) {
  Char k(key);
  bool which = k.toUpper();
  switch (k) {
    case 'i':
      mplayer.reset();
      break;
    case '!':
      if (mplayer.ready) {
        mplayer.sendCommand(cmd.arg, cmd.pushed);
      }  	  else {
        dbg("Player not ready");
      }
      break;
    case ' '://show status.
      mplayer.showstate(dbg);
      break;
    case 'T':
      if (mplayer.ready) { //block, player is busy but won't tell us so.
        mplayer.play(cmd.arg);
      } else {
        dbg("Player not ready");
      }
      break;
    case 'V':
      mplayer.volume(cmd.arg);
      break;

    case 'D':
      mplayer.ACK(0);//trying to match example packet to determine what checksum should be.
      mplayer.outputDevice(Medium::FLASH);
      break;

    case '?':
      scanI2C(dbg);
#if UsingEDSir
      dbg("IR device is ", IRRX.isPresent() ? "" : "not", " present");
#endif
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
  mplayer.begin(Serial1, true, false); //defer reset to init command
  //  playerReady = 0; //want this to be part of the begin which presently has a multi-second block in it.
  //  playerReady = mplayer.outputDevice(Medium::SDcard);
}

void loop() {
  if (MilliTicked) {//at 9600 baud nothing happens fast ;)
    while (char key = dbg.getKey()) {
      accept(key);
    }
    mplayer.onMilli(onReply);
  }
}
