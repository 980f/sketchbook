/*
   web server for controlling a simple RGB led or strip thereof.

  the url, which only works on the same wifi network as the device, is formed from
  the name and port number given to the server module via construction(the port number) and the MDNS.begin() command for the name as:
  name.local:port e.g. RGBonC3.local:1859

*/


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
  Login(const char *const ssid, const char *const password, unsigned timeout = 5000) :
      ssid(ssid), password(password), timeout(timeout) {
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

// greee, esp put this in global macro namespace, when it should be __ since it is a nominal intrinsic, not a convenience. It also should be a real function that is inlined, but they like C over C++ and do we have to deal with the slop ourselves.
#undef cli
/////////////////////////////////////////////////////////
void setup(void) {
  Serial.begin(115200);//loses hardware identity when wrapped by a chainprinter.
  rgbPage.setup();
}

#include "sui.h"
struct myUI: public SUI<unsigned, true, 2> {

  myUI(Stream &keyboard, Print &printer):SUI(keyboard, printer){}

  bool handleKey(unsigned char cmd, bool wasUpper){
    switch (cmd) {
      case 'r':
      case 'b':
      case 'g':
        rgbPage.driver.apply(cmd, cli[0]);
        break;
      case '\n':
        rgbPage.driver.refresh();
        break;
    }
    //treat every input as a command, unknown ones still consume any passed parameters.
    return true;
  }  
} cli(Serial,dbg.raw);

void loop(void) {
  if (MilliTicker) {
    rgbPage.onTick(MilliTicker.recent());
  }

  cli.loop();
}
