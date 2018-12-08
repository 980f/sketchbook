/*
  telnet server module tester.
  "Copyright (c) 2018 Andy Heilveil (github/980F)."

  this program accepts data from up to 3 (see MAX_SRV_CLIENTS) wifi clients, as well as the serial port
  and transmits each byte received to all the  other ports, wifi and serial.
  There is a place to add echo, but since pleasant use involves line buffering clients I set each client for local echo.

  There is a note as to where to insert a prefix indicating source of data, but I'd rather let that be in any protocol that uses
  this as spoofing is generally a benefit for my intended uses.

*/

#include <ESP8266WiFi.h>

Stream &debug(Serial);

#include "millievent.h"
#include "EEPROM.h"

#include "pinclass.h"

//a pin used to enable extra debug spew:
const InputPin<D7, LOW> Verbose;

//max number of simultaneous clients allowed, note: their incoming stuff is mixed together.
#define MAX_SRV_CLIENTS 3

//copy protecting and enforcing a null terminator.
void strzcpy(char *target, unsigned allocated, const char *source) {
  unsigned si = 0;
  if (allocated--) { //check non zero, decrement instead of having -1 through the rest of this fn.
    while (*source && si < allocated) {
      target[si++] = *source++;
    }
    target[si] = 0;
  }
}

unsigned strztok(char *sbuf, unsigned len, char cutter = ':') {
  unsigned pi = 0;
  while (pi < len) {
    char c = *sbuf++;
    if (c == cutter) {
      sbuf[pi] = 0;
      return pi;
    }
    if (c == 0) {
      break;
    }
    ++pi;
  }
  return ~0U;
}

//write fixed size block of eeprom, nulling all locations starting with the given null.
unsigned strzsave(char *thing, unsigned allocated, unsigned offset) {
  bool clean = false;
  for (unsigned si = 0; si < allocated; ++ si) {
    char c = clean ? 0 : thing[si];
    clean = (c == 0);
    EEPROM.write(offset++, c);   //no need to finesse with update() on an esp8266.
  }
  return offset;
}

//read from fixed size block of eeprom, ignoring data after given null.
unsigned strzload(char *thing, unsigned allocated, unsigned offset) {
  bool clean = false;
  for (unsigned si = 0; si < allocated; ++ si, ++offset) {
    char c = clean ? 0 : EEPROM.read(offset);
    clean = (c == 0);
    thing[si] = c;
  }
  return offset;
}

struct Credentials {//todo: use allocation constants from wifi.h
  char ssid[32];// = "honeypot";
  char password[64];// = "brigadoon-will-be-back-soon";

  unsigned save(unsigned offset = 0) { //todo: symbols or static allocator.
    return strzsave(password, sizeof(password), strzsave(ssid, sizeof(ssid), offset));
  }

  unsigned load(unsigned offset = 0) {
    return strzload(password, sizeof(password), strzload(ssid, sizeof(ssid), offset));
  }

  //safe set of ssid, clips and ensures a trailing nul
  void setID(const char *s) {
    strzcpy(ssid, sizeof(ssid), s);
  }
  //safe set of password, clips and ensures a trailing nul
  void setPWD(const char *s) {
    strzcpy(password, sizeof(password), s);
  }

} cred;

const char *Hostname = "980FMesher";

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

  Telnetter(uint16_t teleport = 23): //23 is standard port, need to make this a configurable param so that we can serve different processes on one device.
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
      return true;
    } else {
      if (amTrying) {
        if (testRate.perCycle()) {
          amConnected = testConnection();
        }
      } else if (beTrying) {
        tryConnect();
      }
      return false;
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
      for (unsigned mi = 0; mi < WL_MAC_ADDR_LENGTH; mi++) {
        debug.print(':');
        debug.print((mac[mi] >> 4), 16);
        debug.print((mac[mi] & 0xF), 16);
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
      if (teleport != 23) {
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


const unsigned CredAddress=0;

void setup() {
  Serial.begin(115200);
  cred.load(CredAddress);
  if(cred.ssid[0]==255){
    cred.setID("honeypot");
    cred.setPWD("brigadoon-will-be-back-soon");
    cred.save(CredAddress);
  }
  net.begin(cred);
}

void loop() {
  if (MilliTicked) { //must be polled for MonoStables to work
    //by only checking once per millisecond we lower total power consumption a smidgeon.
    if (net.serve(Serial)) {
      net.broadcast(Serial);
    } else {
      if (size_t len = Serial.available()) {
        char sbuf[len + 1]; //not standard C++! (but most compilers will let you do it)
        Serial.readBytes(sbuf, len);
        sbuf[len] = 0; //avert buffer overflow
        unsigned colonated = strztok(sbuf, len, ':');
        if (colonated != ~0U) {
          if (colonated++) { //then we have a ssid
            cred.setID(sbuf);
          }
          if (sbuf[colonated]) { //have more bytes?
            cred.setPWD(&sbuf[colonated]);
          }
        }
      }
    }

  }

}
