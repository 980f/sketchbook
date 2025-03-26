/*
    ESP-NOW Broadcast Receiver
    starting with code from Lucas Saavedra Vaz - 2024
    Mutating into an object rather than a main by github/980F - 2025

    This sketch incubates the BroadcastNode class, which is part of a network of peers using only broadcast addresses

*/

#define BroadcastNode_WIFI_CHANNEL 6

#include "broadcastNode.h"

BroadcastNode me(BroadcastNode_Triplet );


void setup() {
  Serial.begin(921600);
  Serial.printf("Program: %s\n\n",__FILE__);
  me.spew = true;
  if (!me.begin(true)) {
    Serial.println("Failed to initialize ESP-NOW");
    Serial.println("Rebooting in 5 seconds... hoping that magically fixes things");
    for(unsigned countdown=5;countdown-->0;){
      Serial.printf("\t%d",countdown);
      delay(1000);
    }
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
        if (!me.send_message(writer-data, reinterpret_cast<const uint8_t*>(data))) {
          Serial.println("Failed to broadcast message");
        }
        writer = data;
      }
    } while (--buffered);
  }
}
