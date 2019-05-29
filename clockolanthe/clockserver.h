

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>


#include "eztypes.h"//countof
#include "chainprinter.h" //diagnostic output
#include "sprinter.h"
#include "millievent.h" //timers
#include "stopwatch.h"  //to see how fast we dare poll.

//// aux class for login credentials, one probably exists that we can find and replace this
struct Login {
  const char * const ssid;;
  const char * const password;
  unsigned timeout;
  Login(const char * const ssid , const char * const password, unsigned timeout = 5000 ): ssid(ssid), password(password), timeout(timeout) {}

};


#include "hms.h" //hours minutes and seconds

class ClockServer {
    ChainPrinter &dbg;
    Login *dnserver = nullptr;
    bool connected = false;
    const char * const ourname;
    WebServer server;

    //will roll() with each event.
    StopWatch since;
    MonoStable timedOut;
    MonoStable pollStatus;//inherited value from some examples.

    //page variables:
    bool updateDesired = false;
    //rate at which the client will call us back for a new clock image:
    unsigned refreshRate = 2;
    int msglen = 0;
    bool commanding;

    HMS bigben;
    HMS desired;

    // 1859,  "bigbender",dbg):
    ClockServer(unsigned port , const char * const name , ChainPrinter &dbg);

    void timestamp(const char *event) {
      dbg("\n", event, ':', double(since.roll()));
    }
  private:
    static const char headbody[];
    static const char form[];
    static const char clockdisplay[];
    static const char enddocument[];

    char workspace[4096];//2k is biggest so far. Choosing "eeprom" page size for the ESP-01.
    Sprinter p;

    void showclock(const HMS &ck);
    void elapsed() ;
    void diagRequest();
    void showRequest();
    void ackIgnore();
    void slash() ;
    void onConnection();
    void login();
  public:
    //call this from setup():
    void begin() ;

    //call this once per millisecond:
    void tick(void);

};
