#include "cheaptricks.h"
#include "dbgserial.h"
#include <Mouse.h>
/*
  explore pygamer board
*/

// using TimerTick = decltype(millis());
#include "millievent.h"

#include "pinFlasher.h"
Flasher flasher(LED_BUILTIN);

///////////////////////////////////////
#include "sui.h"
struct MySui : public SUI<> {
  using SUI::SUI; // incantation that is required to inherit base class constructors.

  bool handleKey(unsigned char cmd, bool wasUpper) {
    switch (tolower(cmd)) {
    case '\r':
    case '\n':
      break;
    case '!': {
      flasher.adjust(cli[1], cli[0]);
    } break;
    case 'c':
      flasher.showCount = wasUpper;
      break;
    case ' ': // show program state
      flasher.dump();
      break;
    default: // unrecognized command stuff gets to here
      cout("\nunknown command letter ", wasUpper ? toUpperCase(cmd) : cmd);
      break;
    }
    return true; // discard
  }
} sui(Serial, Serial);

/** how often to check if usb serial has connected */
MonoStable serialChecker(100); //adafruit often uses 25 here.
///////////////////////////////////////
void setup() {
  Serial.begin(115200);
  flasher.setup();
}

void loop() {
  if (MilliTicker) {
    flasher.loop(MilliTicker);
    if (serialChecker.isDone()) {
      if (changed(dbg.stifled, !Serial)) { //stifle debug if Serial is not connected.       
        dbg("Serial connected at ", MilliTicker); // which won't print if Serial just quit ;)
      }      
    }
  }

  if (!dbg.stifled) { // don't check the user interface if it is not connected.
    sui.loop();
  }
}
