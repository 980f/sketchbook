/*
  WiFiTelnetToSerial - Example Transparent UART to Telnet Server for esp8266

  Originally "Copyright (c) 2015 Hristo Gochkov. All rights reserved." but massively reworked by 980F.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <ESP8266WiFi.h>

Stream &debug(Serial);

#include "millievent.h"

//max number of simultaneous clients allowed, note: their incoming stuff is mixed together.
#define MAX_SRV_CLIENTS 3

struct Credentials {//todo: use allocation constants from wifi.h
  const char* ssid = "honeypot";
  const char* password = "brigadoon-will-be-back-soon";
} cred;

//will make into its own module real soon now.
struct Telnetter {
  bool amTrying = false;
  bool amConnected = false;
  bool beTrying = false;
  MonoStable testRate;//500=2 Hz

  const Credentials *cred = nullptr;

  wl_status_t wst = WL_NO_SHIELD;

  WiFiServer server;//port number
  WiFiClient serverClients[MAX_SRV_CLIENTS];

  Telnetter():
    testRate(500),
    server(23) {
    //#done
  }

  /** you may call this in setup, or at some time later if you have some other means of getting credentials */
  bool begin(const Credentials &acred) {
    cred = &acred;
    beTrying = cred != nullptr;
    return tryConnect();//early first attempt, not strictly needed.
  }

  /** call this frequently from loop()
      Feeds merged wifi data from all clients to the given stream.
  */
  bool serve(Stream &stream) {//todo: use functional that matches Stream::write(uint8*,unsigned)
    if (amServing()) {
      if (server.hasClient()) {//true when a client has requested connection
        if (!findSlot()) {//no free/disconnected spot so reject
          server.available().stop();//?forceful reject to client?
          debug.println("Connection rejected ");
        }
      }
      //check clients for data
      for (unsigned i = MAX_SRV_CLIENTS; i-- > 0;) {
        auto &aclient = serverClients[i];
        if (aclient && aclient.connected()) {
          if (aclient.available()) {
            //todo: emit prefix to allow demuxing of multiple clients
            //get data from the telnet client and push it to the UART
            size_t len = aclient.available();
            uint8_t sbuf[len];//not standard C++! (but most compilers will let you do it)
            aclient.readBytes(sbuf, len);
            stream.write(sbuf, len);
          }
        }
      }
    }
  }

  /** sends given data to all clients */
  void broadcast(uint8_t *sbuf, unsigned len) {
    //push UART data to all connected telnet clients
    for (unsigned i = MAX_SRV_CLIENTS; i-- > 0;) {
      auto &aclient = serverClients[i];
      if (aclient && aclient.connected()) {
        aclient.write(sbuf, len);
        //          delay(1);//why delay here?
      }
    }
  }

  /** sends data from given stream to all clients */
  void broadcast(Stream &stream) {
    if (stream.available()) {
      size_t len = stream.available();
      uint8_t sbuf[len];//not standard C++! (but most compilers will let you do it)
      stream.readBytes(sbuf, len);
      broadcast(sbuf, len);
    }
  }

  bool tryConnect() {
    if (cred) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(cred->ssid, cred->password);
      debug.print("\nConnecting to AP "); debug.println(cred->ssid);
      amTrying = true;
    } else {
      amTrying = false;
    }
    return amTrying;
  }

  bool testConnection() {
    wst = WiFi.status();
    debug.print("APCon status ");debug.println(wst);
    if (wst == WL_CONNECTED) {
      debug.println("Connected!");
      server.begin();
      server.setNoDelay(true);

      debug.print("Ready! Use 'telnet ");
      debug.print(WiFi.localIP());
      debug.println(" 23' to connect");
      return true;
    } else {
      //      debug.print("APCon status ");debug.println(wst);
      return false;
    }
  }

  bool findSlot() {
    for (unsigned i = MAX_SRV_CLIENTS; i-- > 0;) {
      auto &aclient = serverClients[i];
      if (!aclient || !aclient.connected()) {//if available
        if (aclient) {
          aclient.stop();//?some kind of cleanup?
        }
        aclient = server.available();
        debug.print("\nNew client: "); debug.println(i);
        return true;
      }
    }
    return false;
  }

  bool amServing() {
    if (amConnected) {
      return true;
    } else {
      if (amTrying) {
        if (testRate.isDone()) {
          testRate.start();//delay next
          amConnected = testConnection();
        } else {
          debug.print('-');
          if((MilliTicked.recent()%50)==0){
            debug.println('.');
          }
        }
      } else if (beTrying) {
        tryConnect();
        amTrying = true;
      }
      return false;
    }
  }

} net;


void setup() {
  Serial.begin(115200);
  net.begin(cred);
}

void loop() {
  if (MilliTicked) { //must be polled for MonoStables to work
    //by only checking once per millisecond we lower total power consumption a smidgeon.
    net.serve(Serial);
    net.broadcast(Serial);
  }

}
