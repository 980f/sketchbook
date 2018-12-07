/*
  telnet server module tester.
  "Copyright (c) 2018 Andy Heilveil (github/980F)."

*/

#include <ESP8266WiFi.h>

Stream &debug(Serial);

#include "millievent.h"

#include "pinclass.h"

//a pin used to enable extra debug spew;
const InputPin<D7> Verbose;

//max number of simultaneous clients allowed, note: their incoming stuff is mixed together.
#define MAX_SRV_CLIENTS 3

struct Credentials {//todo: use allocation constants from wifi.h
  const char* ssid = "honeypot";
  const char* password = "brigadoon-will-be-back-soon";
} cred;

const char *Hostname="980FMesher";

using MAC = uint8_t[WL_MAC_ADDR_LENGTH];

//will make into its own module real soon now.
struct Telnetter {
  bool amTrying = false;
  bool amConnected = false;
  bool beTrying = false;
  /** whether to send data from one wifi to all others as well as serial */
  bool chat = true;
  uint16_t teleport;//retained for diagnostics
  MonoStable testRate;

  const Credentials *cred = nullptr;

  wl_status_t wst = WL_NO_SHIELD;

  WiFiServer server;
  WiFiClient serverClients[MAX_SRV_CLIENTS];

  Telnetter(uint16_t teleport=23):   //23 is standard port, need to make this a configurable param so that we can serve different processes on one device.
    teleport(teleport),
    testRate(500), //500=2 Hz
    server(teleport) { 
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
      for (unsigned ci = MAX_SRV_CLIENTS; ci-- > 0;) {
        auto &aclient = serverClients[ci];
        if (aclient && aclient.connected() && aclient.available()) {
          //todo: emit prefix to allow demuxing of multiple clients
          //get data from the telnet client and push it to the UART
          size_t len = aclient.available();
          uint8_t sbuf[len];//not standard C++! (but most compilers will let you do it)
          aclient.readBytes(sbuf, len);
          stream.write(sbuf, len);
          if (chat) {
            broadcast(*sbuf, len, ci);
          }
        }
      }
    } else {
      if (amTrying) {
        if (testRate.perCycle()) {
          amConnected = testConnection();
        }
      } else if (beTrying) {
        tryConnect();
      }
    }
  }

  /** sends given data to all clients */
  void broadcast(const uint8_t &sbuf, const unsigned len, const unsigned sender = ~0U) {
    //push UART data to all connected telnet clients
    for (unsigned ci = MAX_SRV_CLIENTS; ci-- > 0;) {
      if (ci == sender) {
        continue;//don't echo, although you can set this to something outside the legal range and then it will.
      }
      auto &aclient = serverClients[ci];
      if (aclient && aclient.connected()) {
        aclient.write(&sbuf, len);
      }
    }
  }

  /** sends data from given stream to all clients */
  void broadcast(Stream & stream) {
    if (size_t len = stream.available()) {
      uint8_t sbuf[len];//not standard C++! (but most compilers will let you do it)
      stream.readBytes(sbuf, len);
      broadcast(*sbuf, len);
    }
  }

  bool tryConnect() {
    if (cred) {
      WiFi.mode(WIFI_STA);
      WiFi.hostname(Hostname);
      WiFi.begin(cred->ssid, cred->password);
      debug.print("\nDevice Mac ");
      MAC mac;
      WiFi.macAddress(mac);
      for(unsigned mi=0;mi<WL_MAC_ADDR_LENGTH;mi++){
        debug.print(':');
        debug.print((mac[mi]>>4),16);
        debug.print((mac[mi]&0xF),16);        
      }
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

      debug.print("\nUse 'telnet "); debug.print(WiFi.localIP()); 
      if(teleport!=23){
        debug.print(teleport);
      }
      debug.println("' to connect.");
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
        
        aclient.write(Hostname);
        aclient.write(" at your service.\n");
        return true;
      }
    }
    return false;
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
