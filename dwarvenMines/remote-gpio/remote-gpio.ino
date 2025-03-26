#define BroadcastNode_WIFI_CHANNEL 6
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
  Serial.begin(921600);
  Serial.printf("Program: %s  Running on WiFi channel %u\n\n", __FILE__ , BroadcastNode_WIFI_CHANNEL);
  my.spew = true;
  my.period = Ticker::Never;

  if (!my.setup(55)) {
    Serial.println("Failed to initialize ESP-NOW");
    explode();
  }
  Serial.println("Setup complete.");
}


void loop() {
  if (Ticker::check()) { // read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    my.onTick();
    my.loop();//inside ticker as the only time things can change is on the timer
  }

  if (Serial.available()) {
    auto key = Serial.read();
    Serial.printf("Command %c [%d]\n", char(key), int(key) );
    switch (key) {
      case 26:
        explode();
        break;
      case 13:
        Serial.printf("posting %u\n", my.toSend.sequenceNumber);
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
      {
        auto buf=my.toSend.outgoing();
        my.dumpHex(buf,Serial);
      }
          break;
    }
  }
}
