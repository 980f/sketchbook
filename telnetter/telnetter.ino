/*
  telnet server module tester.
  "Copyright (c) 2018 Andy Heilveil (github/980F)."

  this program accepts data from up to 3 (see MAX_SRV_CLIENTS) wifi clients, as well as the serial port
  and transmits each byte received to all the  other ports, wifi and serial.
  There is a place to add echo, but since pleasant use involves line buffering clients I set each client for local echo.

  There is a note as to where to insert a prefix indicating source of data, but I'd rather let that be in any protocol that uses
  this as spoofing is generally a benefit for my intended uses.

  todo: update board select ifdef's to warn on board selected not 8266 or esp32

*/


#ifndef ARDUINO_ARCH_ESP8266
#define UseESP32
#include <WiFi.h>
#include <IPAddress.h>
#else
#include <ESP8266WiFi.h>
#endif

#include "chainprinter.h"
ChainPrinter dbg(Serial);

#include "millievent.h"  //foreground timer utilities

#include "pinclass.h" // digital I/O as boolean variables

//a pin used to enable extra debug spew:
const InputPin<2, LOW> Verbose;
//a pin used to enable reload some params from eeprom
const InputPin<0, LOW> Initparams;

#include "strz.h"  //strztok 

#include "telnetter.h"  //980F/ TelnetActor and related classes
Credentials cred;


#include "nvblock.h" //980F's utilities for storing objects in non-volatile memory
//static ip set for ease of debug, instead of hunting something down via mDNS
IPAddress mystaticip {192, 168, 0, 65}; //65=='A'. Andy always sets his DHCP servers to start at 100, to make it easier to assign fixed addresses to dumb devices such as this one.

/** where credentials are to be stored in nv memory */
const unsigned CredAddress = 0;

/** allocate some nv memory for this application */
const Nvblock keepip = Nvblock::For(mystaticip, 128); //todo: allocation scheme to ensure no overlap, without wrapping with a struct.
//const Nvblock keepip2 <IPAddress>(mystaticip, 128); //todo: allocation scheme to ensure no overlap, without wrapping with a struct.

const char *Hostname = "980FMesher";

struct Chatter: public TelnetActor {
  bool chat = true;
  void onInput(uint8_t *bytes, unsigned length, unsigned ci);
  void onConnect(WiFiClient &aclient, unsigned ci);
  const char *hostname() ;
} chat;

Telnetter net(&chat);

void Chatter::onInput(uint8_t *bytes, unsigned length, unsigned ci) {
  Serial.write(bytes, length);
  if (chat) {
    net.broadcast(*bytes, length, ci);
  }
}

void Chatter::onConnect(WiFiClient &aclient, unsigned ci) {
  aclient.write(Hostname);
  aclient.write(" at your service, station ");
  aclient.write('0' + ci);
  aclient.write(".\n");
}

const char *Chatter::hostname() {
  return Hostname;//todo:eeprom
}

/** use this when you are blocking the foreground with 'delay', and tell us why. */
void goslow(const char *msg) {
  Serial.println(msg);
  delay(1000);
}

//////////////////////////////
void setup() {
  goslow("setup"); //## note- this runs at default baud rate! 
  Serial.begin(115200);

  goslow("loads");
  //EEPROM.begin(4096);//4096:esp8266 max psuedo eeprom
  cred.load(CredAddress);
  keepip.load();
  Telnetter::Verbose = Verbose;
  if (Verbose) {
    dbg("\nStored Credentials:", cred.ssid, cred.password);
    dbg("\nStored IP: ", mystaticip);
  }

  if (Initparams) {
    cred.setID("honeyspot").setPWD("brigadoonwillbebacksoon");
    mystaticip = IPAddress (192, 168, 0, 65); //65=='A'
    cred.save(CredAddress);
    dbg("\nSaved credentials at ", CredAddress);
    keepip.save();
  }
  goslow("net.begin");
  net.begin(cred);
}


void loop() {
  if (MilliTicker) { //must be polled for MonoStables to work
    //by only checking once per millisecond we lower total power consumption a smidgeon.

    Telnetter::Verbose = Verbose;//update before calling serve() as it uses this concept
    goslow("serve");
    if (net.serve()) {// the WiFi service needs to be polled.
      goslow("broadcast");
      net.broadcast(Serial);
    } else { //Check local port for something to broadcast
      if (size_t len = Serial.available()) {
        char sbuf[len + 1]; //# not standard C++! (but most compilers will let you do it)
        Serial.readBytes(sbuf, len);
        sbuf[len] = 0; //avert buffer overflow

        unsigned passat = strztok(sbuf, len, '@');
        if (passat != ~0U) {
          goslow("set creds");
          if (passat++) { //then we have a ssid in front, the ++ skips over the '@'
            cred.setID(sbuf);
          }
          //a trailing '@' means no password.
          cred.setPWD(&sbuf[passat]);
        }
      }
    }
  }
}
