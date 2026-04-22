#pragma once


#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "sprinter.h" //String printer, that makes sure we don't run off the end of the string
#include "millievent.h" //MonoStable
#include "stopwatch.h" //to see how fast we dare poll.
#include "rgb_driver.h"

//todo: replace the following defines with NV memory 

#ifndef rgb_server_triplet
#error "to use rgb_server you must define rgb_server_triplet to the 3 pins of interest, separate by commas" 
#endif

#ifndef rgb_server_port
#error "to use rgb_server you must define rgb_server_port with the IP port to listen on"
#endif
 
static const char *const ourname = "RGBonC3";//todo: make this a constructor arg to RBG_Server

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


class RGB_Server {
  static RGB_Server *highlander; ///there can be only one. At least until the callback has some means to figure out the base url of the request and map that to out container.

  bool connected = false;
 
  WebServer server{rgb_server_port};
  public:
  RGB_C3 driver{rgb_server_triplet}; // todo: make an interface and pass this in
  MilliTick refreshRate=0;

  private:

  MonoStable timedOut;
  MonoStable pollStatus{500}; // 500: inherited value from some examples.

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // shared block

  char workspace[4096]; // 2k is biggest so far. Choosing "eeprom" page size for the ESP-01.
  Sprinter p{workspace, sizeof(workspace)};
  // invoke the following to start printing to the shared big block
  #define WORKSPACE p.rewind()

  StopWatch since; // will roll() with each event.
  void timestamp(const char *event) {
    dbg(CRLF, event, ':', double(since.roll()));
  }

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  void diagRequest() {
    WORKSPACE;
    p.printf("\n%s %s ", server.uri().c_str(), (server.method() == HTTP_GET) ? "GET" : "POST");
    for (unsigned i = server.args(); i-- > 0;) {
      p.printf("\n\t%s:%s ", server.argName(i).c_str(), server.arg(i).c_str());
    }
    dbg(p.buffer); // for now track web activity
  }

  void showRequest() {
    diagRequest();
    dbg("Most used: ", Sprinter::mostused); // ## a class static !!??!
    server.send(404, "text/plain", p.buffer);
  }

  void ackIgnore() {
    server.send(410, "text/plain", "Sorry Dave, I can't do that");
  }

  void slash() {
    timestamp("Begin main page");
    bool dumpem = server.args() == 0;
    for (unsigned i = server.args(); i-- > 0;) {
      const char *value = server.arg(i).c_str(); // you MUST not persist this outside of the function call
      char key = server.argName(i)[0]; // single letter names

      switch (key) {
          //      case 'h': //set time of remote from hh:mm (24H)
          //        desired.setTimeFrom(value);
          //        dbg("\nDesired time recorded locally");
          //        break;
        case 'u':
          refreshRate = atoi(value);
          dbg("setting page refresh rate to ", refreshRate);
          break;
        default:
          dumpem = true;
          break;
      }
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

  void onConnection(void) {
    dbg("\nConnected to ", dnserver->ssid);
    dbg("\nIP address: ", WiFi.localIP());

    if (MDNS.begin(ourname)) {
      dbg("\nmDNS working as ", ourname);
      // Add service to MDNS-SD
      MDNS.addService("http", "tcp", 80); // todo: check port and protocol.
    } else {
      dbg("\nmDNS failed, tried to be ", ourname);
    }

    server.on("/", ThunkIng(slash));
    server.on("/favicon.ico", ThunkIng(ackIgnore)); // browser insists on asking us for this, ignore it.
    server.onNotFound(ThunkIng(showRequest));

    server.begin();
    dbg("\n RGB controller is at your service.\n");
  }

  public:

  void login() {
    if (!logins.isValid()) {
      logins.rewind();
    }
    dnserver = &logins.next();
    dbg("\nTrying ", dnserver->ssid, ' ', dnserver->password);
    WiFi.begin(dnserver->ssid, dnserver->password);
    timedOut = dnserver->timeout;
  }

  public:

  void setup(void) {
    WiFi.mode(WIFI_STA);
    login(); // optional early start, could drop this and wait for the recovery logic in loop to do the login.
    since.start();
    highlander = this;
  }

  void onTick(MilliTick now) {
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
};

//todo: move to a cpp file, along with most of the method guts:
RGB_Server *RGB_Server::highlander=nullptr;