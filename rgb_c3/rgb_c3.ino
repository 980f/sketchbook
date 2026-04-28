/*
   web server for controlling a simple RGB led or strip thereof.

  the url, which only works on the same wifi network as the device, is formed from
  the name and port number given to the server module via construction(the port number) and the MDNS.begin() command for the name as:
  name.local:port e.g. RGBonC3.local:1859

  as of this writing the pins and port are set by defines, later on they will be set from NV memory, ditto for network name and passwords.

*/


#include "millievent.h" //especially login timing

/////////////////////////////////////////////////////////////

#include "rgb_server.h"
RGB_Server rgbServer;
//todo: the next line should be in a cpp file, with some extracts of rgb_driver.h file.
LEDC::ClockSource LEDC::clockSource;


#include "dbgserial.h"

/////////////////////////////////////////////////////////
void setup(void) {
  Serial.begin(115200);
  rgbServer.enableWeb = false;
  rgbServer.setup();
}

#include "sui.h"
struct myUI: public SUI<unsigned, true, 2> {
  using SUI::SUI;

  bool handleKey(unsigned char cmd, bool wasUpper) override {
    switch (cmd) {
      case 'r':
      case 'b':
      case 'g': {
        unsigned desired=cli[0];
        if(desired>4095){
          desired=4095;
        }
        cout("\n setting ",cmd," to ",desired);
        rgbServer.driver.apply(cmd, desired);
      }
        break;
      case 'x':
        if(changed(rgbServer.enableWeb,cli[0])){
          rgbServer.retry();
        }
        break;
      case '!':
        dbg.stifled=cli[0];
        break;
      case '\n':
        cout("\t updating.");
        rgbServer.driver.update();
        break;
    }
    //treat every input as a command, unknown ones still consume any passed parameters.
    return true;
  }  
} sui(Serial,dbg.raw);

void loop(void) {
  if (MilliTicker) { 
    rgbServer.onTick(MilliTicker.recent());
    //todo: check if Serial is available for debug.
  }

  sui.loop();
}
