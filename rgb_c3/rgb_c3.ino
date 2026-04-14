/*
   web server which passes along commands via serial to clock controller.
   Someday will merge and run directly on an ESP module of some kind, but those were pin tight for this application.
   Works on an ESP-01 with 512K (uses around ~320k).

	the url, which only works on the same wifi network as the device, is formed from 
	the name and port number given to the server module via construction(the port number) and the MDNS.begin() command for the name as:
	name.local:port e.g. bigbender.local:1859

*/



#include "millievent.h" //especially login timing
#include "eztypes.h"//countof
#include "limitedpointer.h"

////////////////////////////////////////////////////////////////
#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);

/////////////////////////////////////////////
struct Login {
  const char * const ssid;;
  const char * const password;
  unsigned timeout; //also serves as retry internval.
  Login(const char * const ssid , const char * const password, unsigned timeout = 5000 ): ssid(ssid), password(password), timeout(timeout) {}
};

static Login known[] = {
  {"honeypot", "brigadoonwillbebacksoon", 4500}, //timeout set to 4500 just to check up on syntax. 
};

LimitedPointer<Login> logins(known, countof(known));
Login *dnserver = nullptr; //the actual server in use, which is one of the 'known' ones.
/////////////////////////////////////////////////////////////

struct Color {
  unsigned r;
  unsigned g;
  unsigned b;

  void set(char color,unsigned rawvalue){
    switch(color){
      case 'r': r=rawvalue; break;
      case 'g': g=rawvalue; break;
      case 'b': b=rawvalue; break;
    }
  }
};

/** access to the hardware */
#include "ESP32Servo.h"
struct RGB_C3 {
  //todo: pick 3 pins
  void apply( Color &desired){
    //todo: send 'desired' values to hardware.
  }
  
};


#include "RGB_Server.h"
RGB_Server rgbPage;

/////////////////////////////////////////////////////////
void setup(void) {
  dbg.begin(115200);
  rgbPage.setup();
}


#include "clirp.h"
CLIRP<unsigned,true,2> cli;
 
void loop(void) {
  if (MilliTicker) {
    rgbPage.onTick(MilliTicker.recent());   
  }

  auto ch = dbg.getKey();
  if(cli(ch)){//if might be command
    switch(ch){
      case 'r': case 'b': case 'g':

      break;
    }

  }

}

