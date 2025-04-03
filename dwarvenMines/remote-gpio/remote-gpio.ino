#define BroadcastNode_WIFI_CHANNEL 6


bool TRACE = false; //must preceded remoteGPIO for it to use it
#include "remoteGpio.h"

#include "simpleTicker.h"

RemoteGPIO my;

void explode() {
  Serial.println("Rebooting in a few seconds... hoping that magically fixes things");
  for (unsigned countdown = 4; countdown-- > 0;) {
    Serial.printf("\t%d", countdown);
    delay(1000);
  }
  ESP.restart();
}


void setup() {
  Serial.begin(460800);
  Serial.printf("Program: %s  Running on WiFi channel %u\n\n", __FILE__ , BroadcastNode_WIFI_CHANNEL);
  my.spew = true;
  my.period = Ticker::Never;

  if (!my.setup(55)) {
    Serial.println("Failed to initialize ESP-NOW");
    explode();
  }
  my.forceMode(INPUT_PULLUP);//GP25 seems to become a low output at random, likely during wifi setup.
  Serial.println("Setup complete.");
}


void loop() {
  if (Ticker::check()) { // read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    my.onTick();
  }
  my.loop();

  if (Serial.available()) {
    auto key = Serial.read();
    Serial.printf("Command %c [%d]\n", char(key), int(key) );
    switch (key) {
      case 26:
        explode();
        break;
      case 13:
        Serial.printf("posting %u\n", my.report.sequenceNumber);
        my.post();
        break;
      case ' ':
        ForPin(index) {
          Serial.printf("\tD%u=%x", my.gpio[index].pin.number, bool(my.gpio[index]));
        }
        Serial.println();
        ForPin(index) {
          Serial.printf("\t[%u]", index);
          my.gpio[index].printTo(Serial);
        }
        Serial.println();
        break;
      case '.':
        my.shouldSend = true;
        break;
      case '=':
        my.dumpHex(my.toSend.outgoing(), Serial);
        break;
      case '!':
        TRACE = true;
        break;
      case '?':
        my.forceMode(INPUT_PULLUP);
        break;
    }
  }
}
