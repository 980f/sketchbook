/*
   Copyright (c) 2015, Majenko Technologies
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification,
   are permitted provided that the following conditions are met:

 * * Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

 * * Redistributions in binary form must reproduce the above copyright notice, this
     list of conditions and the following disclaimer in the documentation and/or
     other materials provided with the distribution.

 * * Neither the name of Majenko Technologies nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
   ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
    hr =  (tick / (60 * 60 * 1000));
  }
};


//used %3d for the spec as it happens to be the width of the biggest field we might print and as such we cna use the sizeof(clockface) to allocate ram to render the svg into
const char clockface[] = "<svg xmlns='http://www.w3.org/2000/svg' id='clock' width='250' height='250'  viewBox='0 0 250 250' >  <title>SVG Analog Clock</title>"
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
                         "</svg>";

static char workspace[sizeof(clockface) + 1];

#define ClockKey "/clockimage.svg"

void handleRoot() {
  char temp[400];
  HMS c;
  snprintf(workspace, sizeof(workspace) - 1, clockface, c.hr*30, c.min*6, c.sec*6);//6 degrees per second and minute, 360/60, 360/12 = 30 for hour
  snprintf(temp, sizeof(temp) - 1, "<html><head><meta http-equiv='refresh' content='13'/><title>login</title></head><body><h1>Big Bender</h1><p>Uptime: %02d:%02d:%02d</p> <img src='" ClockKey "' /></body></html>", c.hr, c.min, c.sec);
  server.send(200, "text/html", temp);
}

void handleNotFound() {
  String message = "Item Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
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

  server.on("/", handleRoot);
  server.on(ClockKey, [](){ server.send(200, "image/svg+xml", workspace);});
  server.on("/inline", []() {
    server.send(200, "text/plain", "simple text response");
  });
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void setup(void) {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

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
