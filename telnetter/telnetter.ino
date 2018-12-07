/*
  telnet server module tester.
  "Copyright (c) 2018 Andy Heilveil (github/980F)."

*/

/**
  on Wifi_Kit_8 the PRG pin is D0

*/
#include <ESP8266WiFi.h>

Stream &debug(Serial);

#include "millievent.h"

#include "pinclass.h"

const InputPin<D7> Verbose;

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
    if (amConnected) {
      if (server.hasClient()) {//true when a client has requested connection
        if (!findSlot()) {//no free/disconnected spot so reject
          server.available().stop();//?forceful reject to client?
          debug.println("Incoming connection rejected, no room for it.");
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
    } else {
      if (amTrying) {
//        if (Verbose) {
//          debug.print('\t');
//          debug.print(MilliTicked.recent());
//          debug.print('\t');
//          debug.println(testRate.due());
//        }
        if (testRate.perCycle()) {   
          amConnected = testConnection();
        } 
      } else if (beTrying) {
        tryConnect();
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
      debug.print("\nConnecting to AP "); debug.print(cred->ssid);
      if (Verbose) {
        debug.print(" using password "); debug.println(cred->password);
      }
      amTrying = true;
    } else {
      amTrying = false;
    }
    return amTrying;
  }

  bool testConnection() {
    wst = WiFi.status();
    debug.print("\tAPCon status "); debug.println(wst);
    if (wst == WL_CONNECTED) {
      server.begin();
      server.setNoDelay(true);

      debug.print("\nUse 'telnet "); debug.print(WiFi.localIP()); debug.println(" [23]' to connect.");
      amTrying = false; //trying to close putative timing gap
      return true;
    } else {
      return false;
    }
  }

  /** see if we have a place to keep client connection info. If so retain it.*/
  bool findSlot() {
    for (unsigned i = MAX_SRV_CLIENTS; i-- > 0;) {
      auto &aclient = serverClients[i];
      if (aclient && !aclient.connected()) {//connection died
        aclient.stop();//clean it up.
      }
      //not an else! aclient.stop() may make aclient be false.
      if (!aclient) {//if available
        aclient = server.available();
        debug.print("\nNew client: "); debug.println(i);
        return true;
      }
    }
    return false;
  }

} net;


void setup() {
  Serial.begin(115200);
  net.begin(cred);
  Serial.print(cred.ssid);
  if (Verbose) {
    Serial.print('@');
    Serial.print(cred.password);
  }

}

void loop() {
  if (MilliTicked) { //must be polled for MonoStables to work
    //by only checking once per millisecond we lower total power consumption a smidgeon.
    net.serve(Serial);

    net.broadcast(Serial);
  }

}
