/*
   web server which passes along commands via serial to clock controller.
   Someday will merge and run directly on an ESP module of some kind, but those were pin tight for this application.
   Works on an ESP-01 with 512K (uses around ~320k).

*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include "easyconsole.h"
EasyConsole<decltype(Serial)> dbg(Serial);


const char * const ssid = "honeypot";
const char * const password = "brigadoon-will-be-back-soon";

const char * const ourname = "bigbender";
ESP8266WebServer server(1859);

struct HMS {
  int hour;
  int minute;
  int sec;
  int mils;

  HMS(long tick) {
    mils = tick % 1000;
    sec = (tick / 1000) % 60;
    minute = (tick / (60 * 1000)) % 60;
    hour =  (tick / (60 * 60 * 1000)) % 12;
    if (hour == 0) {
      hour = 12;
    }
  }
};

HMS bigben(0);
HMS desired(0);
bool updateDesired = false;

#include "char.h"

void setTimeFrom(const char *value) {
  int *field = &desired.hour;
  while (char c = *value++) {
    Char see(c);
    if (!see.appliedDigit(*field)) {
      if (see == ':') {
        field = &desired.minute;
      } else {
        dbg("Bad hh:mm> ", value);
        //and try to survive it
      }
    }
  }

}

#include <stdarg.h>
struct Sprinter {
  const int guard;
  char * const buffer;

  int tail;
  Sprinter &rewind() {
    tail = 0;
    return *this;
  }

  Sprinter(char * buffer, size_t length): guard(length), buffer(buffer) {
    rewind();
  }

  char * printf(const char *format, ...) {
    va_list args;
    va_start (args, format);
    tail += vsnprintf(buffer + tail, guard - tail, format, args);
    va_end (args);
    return buffer;//to inline print as an argument
  }

  operator char *() {
    return buffer;
  }

  //diagnostic:
  static int mostused;
  ~Sprinter() {
    if (mostused < tail) {
      mostused = tail;
    }
  }
};
int Sprinter::mostused = 0;

//
const char headbody[] =
  "\n<html><head><meta http-equiv='refresh' content='%d'/><title>BigBen</title></head><body>"
  "<h1>Big Bender</h1 ";


//using single letter arg names to allow use of switch()
const char form[] =
  "<form > <input name='h' type='time'>"
  "\n<button name='s' type='submit'> Set Clock </button><br>"
  "\n<button name='z' type='submit'> Zero. </button><br>"
  "\n<span>Steps:</span>"
  "<button name='p' type=submit' value='-100'> -100 </button>"
  "<button name='p' type=submit' value='-10'>-10 </button>"
  "<button name='p' type=submit' value='-1'>-1 </button>"
  "<button name='p' type=submit' value='1'> 1 </button>"
  "<button name='p' type=submit' value='10'> 10 </button>"
  "<button name='p' type=submit' value='100'> 100 </button>"
  "\n</form><br>";

const char clockdisplay[] =
  "\n<svg xmlns='http://www.w3.org/2000/svg' id='clock' width='250' height='250'  viewBox='0 0 250 250' >  <title>dial it up</title>"
  "<circle id='face' cx='125' cy='125' r='100' style='fill: white; stroke: black'/>"
  "<g id='ticks' transform='translate(125,125)'>"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(30)'  />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(60)'  />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(90)'  />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(120)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(150)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(180)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(210)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(240)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(270)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(300)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(330)' />"
  "<path d='M95,0 L100,-5 L100,5 Z' transform='rotate(360)' />"
  "</g>"

  "<g id='hands' style='stroke: black;  stroke-width: 5px; stroke-linecap: round;'>"
  "<path id='hour'   d='M125,125 L125,75' transform='rotate(%3d, 125, 125)'/>"
  "<path id='minute' d='M125,125 L125,45' transform='rotate(%3d, 125, 125)'/>"
  "<path id='second' d='M125,125 L125,30' transform='rotate(%3d, 125, 125)' style='stroke: red; stroke-width: 2px' />"
  "</g>"

  "<text x='5' y='25' fill='blue'>%02d:%02d:%02d</text>"
  "\n<circle id='knob' r='6' cx='125' cy='125' style='fill: #333;'/>"
  "</svg>";

const char enddocument[] = "\n</body></html>";

static char workspace[4096];//2k is biggest so far. Choosing "eeprom" page size for the ESP-01.
Sprinter p(workspace, sizeof(workspace));

#define WORKSPACE p.rewind()


void showtime(const HMS &ck) {
  p.printf(clockdisplay, ck.hour * 30, ck.minute * 6, ck.sec * 6, ck.hour , ck.minute, ck.sec ); //6 degrees per second and minute, 360/60, 360/12 = 30 for hour
}

void elapsed() {
  HMS c(millis());
  WORKSPACE;
  p.printf(headbody, 13); //refresh rate
  showtime(c);
  p.printf(enddocument);
  server.send(200, "text/html", p.buffer);
}


void diagRequest(){
	WORKSPACE;
  p.printf("%s %s ", server.uri().c_str(), (server.method() == HTTP_GET) ? "GET" : "POST");
  for (unsigned i = server.args(); i-- > 0;) {
    p.printf( "\n\t%s:%s ", server.argName(i).c_str() , server.arg(i).c_str() );
  }
  dbg(p.buffer);//for now track web activity
}

void showRequest() {
  diagRequest();
  dbg("Most used: ", Sprinter::mostused);
  server.send(404, "text/plain", p.buffer);
}

void ackIgnore() {
  server.send(410, "text/plain", "Sorry Dave, I can't do that");
}

void slash() {
 bool dumpem=server.args()==0;
  for (unsigned i = server.args(); i-- > 0;) {
    const char *value = server.arg(i).c_str(); //you MUST not persist this outside of the function call
    char key = server.argName(i)[0];//single letter names

    switch (key) {
      case 'h': //set time of remote from hh:mm (24H)
        setTimeFrom(value);
        dbg("\nDesired time recorded locally");
        break;
      case 's'://publish time to clock
        updateDesired = true; //not checking if it actually changed, so that we may refresh the remote.
        dbg('!', desired.hour, ':' , desired.minute);
        break;
      case 'z'://tell clock that it is at midnight/noon
        dbg("!0");
        break;
      case 'p'://pulse the stepper
        dbg('!', value);
        break;
        default:
        dumpem=true;
        break;
    }
  }
	if(dumpem){
		diagRequest();
		dbg(p.buffer);	
	}
  HMS c(millis());//will be clock's last report
  WORKSPACE;
  p.printf(headbody, 13); //refresh rate
  p.printf(form);//no args yet, time widget ignores given value.
	 showtime(c);
  p.printf(enddocument);
  server.send(200, "text/html", p.buffer);
}


void onConnection() {
  dbg("\nConnected to ", ssid);
  dbg("\nIP address: ", WiFi.localIP());

  if (MDNS.begin(ourname)) {
    dbg("\nmDNS working as ", ourname);
  } else {
    dbg("\nmDNS failed for ", ourname);
  }

  server.on("/", slash);
  server.on("/uptime", elapsed);

  server.on("/favicon.ico", ackIgnore);//browser insists on asking us for this, ignore it.
  server.onNotFound(showRequest);

  server.begin();
  dbg("\nBig Ben is at your service.\n");
}

void setup(void) {
  dbg.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
}

bool connected = false;
long checkConnection = 0;

int msglen = 0;
bool commanding;

void loop(void) {
  if (connected) {
    server.handleClient();
    MDNS.update();
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      delay(500);//todo: milltick, will want to poll serial port while waiting for wifi, to be ready to run on contact.
      dbg(".");
    } else {
      connected = true;
      onConnection();
    }
  }
  if (byte ch = dbg.getKey()) {
    if (ch == '!') {
      if (msglen == 0) {

      }
    }
  }
}
