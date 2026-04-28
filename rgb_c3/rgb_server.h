#pragma once

#if __has_include("rgb_server_opts.h")
#include "rgb_server_opts.h"
#endif
//todo: replace the following defines with NV memory 

#ifndef rgb_server_triplet
#error "to use rgb_server you must define rgb_server_triplet to the 3 pins of interest, separate by commas. This is typically done in file rgb_server_opts.h" 
#endif

#ifndef rgb_server_port
#error "to use rgb_server you must define rgb_server_port with the IP port to listen on. This is typically done in file rgb_server_opts.h"
#endif

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "sprinter.h" //String printer, that makes sure we don't run off the end of the string
#include "millievent.h" //MonoStable
#include "stopwatch.h" //to see how fast we dare poll.
#include "rgb_driver.h"

/** todo: save and restore to EEPROM (or the esp32 spi filesystem). */
struct Login {
  const char *const ssid;//todo: take advantage of 32 char max. Note that some implementations are defective and limit it to 31 (plus null most likely)
  const char *const password;//todo: take advantage of max 63 chars.
  unsigned timeout; // also serves as retry internval.
  Login(const char *const ssid, const char *const password, unsigned timeout = 5000) : ssid(ssid), password(password), timeout(timeout) {
  }
};


class RGB_Server {
  bool connected = false;
  WebServer server{rgb_server_port};
  public:

  RGB_C3 driver{rgb_server_triplet}; // todo: make an interface and pass this in
  MilliTick refreshRate=0;
  bool enableWeb=false; //untilow ehave time to difure out why we are getting NO_AP_FOUND"
  
  private:

  MonoStable timedOut;
  MonoStable pollStatus{500}; // 500: inherited value from some examples.

  char workspace[4096]; // 2k is biggest so far. Choosing "eeprom" page size for the ESP-01.
  Sprinter p{workspace, sizeof(workspace)};
  // invoke the following to start printing to the shared big block
  #define WORKSPACE p.rewind()

  StopWatch since; // will roll() with each event.
  void timestamp(const char *event);

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  void diagRequest(void);

  void showRequest(void);

  void ackIgnore(void);

  void slash(void) ;
  void onConnection(void) ;

  public:

  void login(void) ;

  void setup(void);

  void retry(void);

  void onTick(MilliTick now) ;
};
