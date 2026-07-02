
#include "rgb_server.h"
#include "dbgserial.h"

//todo: replace the following defines with NV memory 

#ifndef rgb_server_triplet
#error "to use rgb_server you must define rgb_server_triplet to the 3 pins of interest, separate by commas" 
#endif

#ifndef rgb_server_port
#error "to use rgb_server you must define rgb_server_port with the IP port to listen on"
#endif
 
static const char *const ourname = "RGBonC3.980f";//todo: make this a constructor arg to RBG_Server

//need to get this into NV memory, but we don't have a text editing UI enabled (yet).
static Login known[] = {
  {"honeyspot", "brigadoonwillbebacksoon", 4500}, // timeout set to 4500 just to check up on syntax.
};

#include "eztypes.h" //countof
#include "limitedpointer.h" //circular array

LimitedPointer<Login> logins(known, countof(known));
Login *dnserver = nullptr; // the actual server in use, which is one of the 'known' ones.


//////////////////////////////////////////////////////////////////////////////
// the following was moved outside the using class during chasing down of a compilation bug ( parens vs braces on constructors).
// I'm leaving it out in case I change to using a file for the web page.
static const char headbody[] =
  "\n<html><head><meta http-equiv='refresh' content='%d'/><title>RGB on a C3</title></head><body>"
  "<h1>RGB Light String</h1> ";

static const char enddocument[] = "\n</body></html>";

#define NEWL "<br>\n"

// using single letter button names to allow use of switch(). The 4095 is for 12 bit analog outputs, some systems allow for setting the number of bits and as we try this on those we will make this an editable string.
static const char form[] =
  "<form > " NEWL 
  "<input name='r' id='r' type='number' step=1 min=0 max=4095 value=0>" NEWL 
  "<input name='g' id='g' type='number' step=1 min=0 max=4095 value=0>" NEWL 
  "<input name='b' id='b' type='number' step=1 min=0 max=4095 value=0>" NEWL 
  "<button type='submit'> Submit</button>" NEWL 
  "</form><br>";

//how to pass a member function to something that wants just a function pointer.
#define ThunkIng(member) \
      [this](){\
        member ();\
      }
//FYI: the above works as the compiler writes a function with function statics for the captured variables, emits code to set them where the lambda itself is declared then sets the value of the lambda expression to the address of the generated function.

  void RGB_Server::timestamp(const char *event) {
    dbg(CRLF, event, ':', double(since.roll()));
  }

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  void RGB_Server::diagRequest() {
    WORKSPACE;
    p.printf("\n%s %s ", server.uri().c_str(), (server.method() == HTTP_GET) ? "GET" : "POST");
    for (unsigned i = server.args(); i-- > 0;) {
      p.printf("\n\t%s:%s ", server.argName(i).c_str(), server.arg(i).c_str());
    }
    dbg(p.buffer); // for now track web activity
  }

  void RGB_Server::showRequest() {
    diagRequest();
    dbg("Most used: ", Sprinter::mostused); // ## a class static !!??!
    server.send(404, "text/plain", p.buffer);
  }

  void RGB_Server::ackIgnore() {
    server.send(410, "text/plain", "Sorry Dave, I can't do that");
  }

  void RGB_Server::slash() {
    timestamp("Begin main page");
    bool dumpem = server.args() == 0;
    bool needupdate=false;
    for (unsigned i = server.args(); i-- > 0;) {
      const char *value = server.arg(i).c_str(); // you MUST not persist this outside of the function call
      char key = server.argName(i)[0]; // single letter names

      switch (key) {
        case 'r': case 'b': case 'g':
          driver.apply(key, atoi(value));
          needupdate=true;
          break;
        case 'u':
          refreshRate = atoi(value);
          dbg("setting page refresh rate to ", refreshRate);
          break;
        default:
          dumpem = true;
          break;
      }
    }
    if(needupdate){
      driver.update();
    }
    if (dumpem) {
      diagRequest();
      dbg(p.buffer);
    }
    WORKSPACE;
    p.printf(headbody, refreshRate);
    p.printf(form, refreshRate);

    p.printf(enddocument);
    server.send(200, "text/html", p.buffer);
    timestamp("End main page");
  }

  void RGB_Server::onConnection(void) {
    dbg("\nConnected to ", dnserver->ssid);
    dbg("\nIP address: ", WiFi.localIP());

    if (MDNS.begin(ourname)) {
      dbg("\nmDNS working as ", ourname);
      // Add service to MDNS-SD
      MDNS.addService("http", "tcp", rgb_server_port);
      dbg("\nmDNS failed, tried to be ", ourname);
    }

    server.on("/", ThunkIng(slash));
    server.on("/favicon.ico", ThunkIng(ackIgnore)); // browser insists on asking us for this, ignore it.
    server.onNotFound(ThunkIng(showRequest));

    server.begin();
    dbg("\n RGB controller is at your service.\n");
  }

  void RGB_Server::login() {
    if (!logins.isValid()) {
      logins.rewind();
    }
    dnserver = &logins.next();
    dbg("\nTrying ", dnserver->ssid, ' ', dnserver->password);
    WiFi.begin(dnserver->ssid, dnserver->password);
    timedOut = dnserver->timeout;
  }

  void RGB_Server::setup(void) {
    WiFi.mode(WIFI_STA);
    login(); // optional early start, could drop this and wait for the recovery logic in loop to do the login.
    since.start();
  }

  void RGB_Server::retry(void){
    if(enableWeb){
      setup();
    } else {
      //todo: kill webserver.
    }
  }

  void RGB_Server::onTick(MilliTick now) {
    if (connected) {
      server.handleClient();
    } else {
      if (pollStatus.perCycle()) {
        if (WiFi.status() != WL_CONNECTED) {
          dbg(".");
          if (timedOut) {
            login(); // start over.
          }
        } else {
          connected = true;
          timestamp("Connected after ");
          onConnection();
        }
      }
    }
  }

