/*
   web server which passes along commands via serial to clock controller.
   Someday will merge and run directly on an ESP module of some kind, but those were pin tight for this application.
   Works on an ESP-01 with 512K (uses around ~320k).

*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

const char * const ssid = "honeypot";
const char * const password = "brigadoon-will-be-back-soon";

ESP8266WebServer server(8192);

struct HMS {
  int sec;
  int min;
  int hr;

  HMS() {
    long tick = millis();
    sec = (tick / 1000) % 60;
    min = (tick / (60 * 1000)) % 60;
    hr =  (tick / (60 * 60 * 1000)) % 12;
    if (!hr) {
      hr = 12;
    }
  }
};

#include <stdarg.h>
struct Sprinter {
  int guard = 0;
  int tail = 0;
  char *buffer;
  Sprinter(char * buffer, size_t length): guard(length), buffer(buffer) {}

  char * printf(const char *format, ...) {
    va_list args;
    va_start (args, format);
    tail += vsnprintf(buffer + tail, guard - tail, format, args);
    va_end (args);
    return buffer;//to inline print and use.
  }

};

//
const char pagetemplate[] =
  "<html><head><meta http-equiv='refresh' content='13'/><title>login</title></head><body>"
  "<h1>Big Bender</h1 "
  "<form> <span>Set:</span> <input name='h' value='%2d' type='number'> <span>:</span> <input name='m' value='%02d' type='number'> <button name='s' type='submit'> Set Clock </button><br>"
  "<button name='z' type=submit'> Zero. </button><br>"
  "<span>Nudge:</span>"
  "<button name='p' type=submit' value='-100'> -100 </button>"
  "<button name='p' type=submit' value='-10'>-10 </button>"
  "<button name='p' type=submit' value='10'> 10 </button>"
  "<button name='p' type=submit' value='100'> 100 </button>"
  "</form><br>"
  //analog clock display, doesn't track entry.
  "<svg xmlns='http://www.w3.org/2000/svg' id='clock' width='250' height='250'  viewBox='0 0 250 250' >  <title>dial it up</title>"
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

  "<circle id='knob' r='6' cx='125' cy='125' style='fill: #333;'/>"
  "</svg></body></html>";

//used %3d for the angle specs as the width of the control equals the width of the biggest field we might print and as such we can use the sizeof(...) to allocate ram to render the page into.
static char workspace[4096];//2k is biggest so far.

#define WORKSPACE Sprinter p(workspace, sizeof(workspace))
void slash() {
  HMS c;
  WORKSPACE;
  p.printf(pagetemplate , c.hr, c.min, c.hr * 30, c.min * 6, c.sec * 6); //6 degrees per second and minute, 360/60, 360/12 = 30 for hour

  server.send(200, "text/html", p.buffer);
}

void uptime() {
  HMS c;
  WORKSPACE;
  p.printf(pagetemplate , c.hr, c.min, c.hr * 30, c.min * 6, c.sec * 6); //6 degrees per second and minute, 360/60, 360/12 = 30 for hour
  server.send(200, "text/html", p.buffer);
}

void showRequest() {
  WORKSPACE;
  p.printf("%s %s ", server.uri().c_str(), (server.method() == HTTP_GET) ? "GET" : "POST");

  for (unsigned i = server.args(); i-- > 0;) {
    p.printf( "\n\t%s:%s ", server.argName(i).c_str() , server.arg(i).c_str() );
  }
  Serial.println(p.buffer);//for now track web activity
  //  server.send(404, "text/plain", "Sorry Dave, I can't do that");
}

void onConnection() {
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("bigbender")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", slash);
  server.onNotFound(showRequest);
  server.begin();
  Serial.println("Big Ben is at your service.");
  Serial.print("Max page size:");
  Serial.println(sizeof(pagetemplate));
}

void setup(void) {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

}

bool connected = false;
long checkConnection = 0;
void loop(void) {
  if (connected) {
    server.handleClient();
    MDNS.update();
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    } else {
      connected = true;
      onConnection();
    }
  }
}
