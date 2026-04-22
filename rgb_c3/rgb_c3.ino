/*
   web server for controlling a simple RGB led or strip thereof.

  the url, which only works on the same wifi network as the device, is formed from
  the name and port number given to the server module via construction(the port number) and the MDNS.begin() command for the name as:
  name.local:port e.g. RGBonC3.local:1859

  as of this writing the pins and port are set by defines, later on they will be set from NV memory, ditto for network name and passwords.

*/

#define rgb_server_triplet 0,1,3
#define rgb_server_port 1859


#include "eztypes.h" //countof
#include "limitedpointer.h" //circular array
#include "millievent.h" //especially login timing

////////////////////////////////////////////////////////////////
#include "chainprinter.h"
ChainPrinter dbg(Serial);

/////////////////////////////////////////////
struct Login {
  const char *const ssid;
  const char *const password;
  unsigned timeout; // also serves as retry internval.
  Login(const char *const ssid, const char *const password, unsigned timeout = 5000) : ssid(ssid), password(password), timeout(timeout) {
  }
};

static Login known[] = {
  {"honeypot", "brigadoonwillbebacksoon", 4500}, // timeout set to 4500 just to check up on syntax.
};

LimitedPointer<Login> logins(known, countof(known));
Login *dnserver = nullptr; // the actual server in use, which is one of the 'known' ones.
/////////////////////////////////////////////////////////////

#include "rgb_server.h"
RGB_Server rgbPage;

/////////////////////////////////////////////////////////
void setup(void) {
  Serial.begin(115200);
  rgbPage.setup();
}

#include "sui.h"
struct myUI: public SUI<unsigned, true, 2> {
  using SUI::SUI;

  bool handleKey(unsigned char cmd, bool wasUpper){
    switch (cmd) {
      case 'r':
      case 'b':
      case 'g': {
        unsigned desired=cli[0];
        if(desired>4095){
          desired=4095;
        }
        cout("\n setting ",cmd," to ",desired);
        rgbPage.driver.apply(cmd, desired);
      }
        break;
      case '\n':
        cout("\t updating.");
        rgbPage.driver.refresh();
        break;
    }
    //treat every input as a command, unknown ones still consume any passed parameters.
    return true;
  }  
} sui(Serial,dbg.raw);

void loop(void) {
  if (MilliTicker) { 
    rgbPage.onTick(MilliTicker.recent());
  }

  sui.loop();
}
