/*
   web server which passes along commands via serial to clock controller.
   Someday will merge and run directly on an ESP module of some kind, but those were pin tight for this application.
   Works on an ESP-01 with 512K (uses around ~320k).

  the url, which only works on the same wifi network as the device, is formed from
  the name and port number given to the server module via construction(the port number) and the MDNS.begin() command for the name as:
  name.local:port e.g. bigbender.local:1859

*/


#include "eztypes.h" //countof
#include "limitedpointer.h" //circular array
#include "millievent.h" //especially login timing

////////////////////////////////////////////////////////////////
#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);

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
;
RGB_Server rgbPage;

// greee, esp put this in global macro namespace, when it should be __ since it is a nominal intrinsic, not a convenience. It also should be a real function that is inlined, but they like C over C++ and do we have to deal with the slop ourselves.
#undef cli
/////////////////////////////////////////////////////////
void setup(void) {
  dbg.begin(115200);
  rgbPage.setup();
}

#include "clirp.h"
CLIRP<unsigned, true, 2> cli;

void loop(void) {
  if (MilliTicker) {
    rgbPage.onTick(MilliTicker.recent());
  }

  auto ch = dbg.getKey();
  if (cli(ch)) { // if might be command
    switch (ch) {
      case 'r':
      case 'b':
      case 'g':
        rgbPage.driver.desired.apply(ch, cli[0]);
        break;
      case '\n':
        rgbPage.driver.apply(rgbPage.driver.desired);
        break;
    }
    cli.reset(); // prepare for next command, do not let args leak.
  }
}
