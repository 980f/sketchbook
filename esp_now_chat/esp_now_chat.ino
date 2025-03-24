/*
    ESP-NOW Broadcast Receiver
    starting with code from Lucas Saavedra Vaz - 2024
    Mutating into an object rather than a main by github/980F - 2025

    This sketch incubates the BroadcastNode class, which is part of a network of peers using only broadcast addresses

*/

#define ESPNOW_WIFI_CHANNEL 6

#include "broadcastNode.h"

BroadcastNode me(ESPNOW_Triplet );


void setup() {
  Serial.begin(115200);
  me.spew = true;
  if (!me.begin(true, false)) {
    Serial.println("Failed to initialize ESP-NOW");
    Serial.println("Reeboting in 5 seconds... hoping that magically fixes things");
    delay(5000);
    ESP.restart();
  }

  Serial.println("Setup complete.");
}

char data[32];
char *writer = data;

void loop() {
  auto buffered = Serial.available();
  decltype(Serial.read()) key = 0;
  if (buffered > 0) { //do by chunks
    do {
      if (writer < & data[sizeof(data) - 2]) {
        key = Serial.read();
        *writer++ = key;
      }
      if (key == '\r') {
        *writer = 0;
        Serial.printf("Emitting message: %s\n", data);
        if (!me.send_message(sizeof(data), reinterpret_cast<const uint8_t*>(data))) {
          Serial.println("Failed to broadcast message");
        }
        writer = data;
      }
    } while (--buffered);
  }
}
