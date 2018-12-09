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
const InputPin<D8, LOW> Initparams;

#include "strz.h"

#include "telnetter.h"
Credentials cred;
Telnetter net;

#include "nvblock.h"

IPAddress mystaticip(192, 168, 12, 65); //65=='A'
const unsigned CredAddress = 0;

const Nvblock keepip=Nvblock::For(mystaticip,128);//todo: allocation scheme to ensure no overlap, without wrapping with a struct.

void setup() {
  Serial.begin(115200);

  EEPROM.begin(4096);//4096:esp8266 max psuedo eeprom
  if (Initparams) {
    cred.setID("honeypot");
    cred.setPWD("brigadoon-will-be-back-soon");
    
    cred.save(CredAddress);
    keepip.save();
  } else {
    cred.load(CredAddress);
    keepip.load();    
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
