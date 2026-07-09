#include "cheaptricks.h"
#include <Mouse.h>
#include "dbgserial.h"
/*
  explore pygamer board
*/

using TimerTick = decltype(millis());
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
      flasher.adjust(cli[1],cli[0]);
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

TimerTick serialLive = 0; // takes around a second for USB serial to start working.
TimerTick serialChecked = 0;
///////////////////////////////////////
void setup() {
  Serial.begin(115200);
  flasher.setup();
}

void loop() {
  TimerTick currentMillis = millis();
  flasher.loop(currentMillis);
  if (dbg.stifled) {
    if (changed(serialChecked, currentMillis)) {//only check once per millisecond
      if (changed(dbg.stifled,Serial)) {
        serialLive = serialChecked;
        dbg("Serial connected at ", serialLive);//which won't print if Serial just quit ;)
      }
    }
  }
  if (!dbg.stifled) { // don't check the user interface if it is not connected.
    sui.loop();
  }
}
