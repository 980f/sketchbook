/*
    ESP-NOW Broadcast Receiver
    starting with code from Lucas Saavedra Vaz - 2024
    Mutating into an object rather than a main by github/980F - 2025

    This sketch incubates the BroadcastNode class, which is part of a network of peers using only broadcast addresses
    Yes, ESP_NOW has a serial streamer facility, this program exists to test the BroadcastNode class.

    todo: use pins to select baud rate, default to download rate
*/

#define BroadcastNode_WIFI_CHANNEL 6

#include "broadcastNode.h"

BroadcastNode me(BroadcastNode_Triplet );

void setup() {
  Serial.begin(921600);//todo: read pins and set baud according to them.
  Serial.printf("Program: %s\n\n", __FILE__);
  me.spew = true;
  if (!me.begin(true)) {
    Serial.println("Failed to initialize ESP-NOW");
    Serial.println("Rebooting in 5 seconds... hoping that magically fixes things");
    for (unsigned countdown = 5; countdown-- > 0;) {
      Serial.printf("\t%d", countdown);
      delay(1000);
    }
    ESP.restart();
  }
  Serial.println("Setup complete.");
  delay(2000);//see if messages get out before spontaneous reboot.
}

uint8_t data[252];
uint8_t *writer = data;

void loop() {
  auto buffered = Serial.available();
  decltype(Serial.read()) key = 0;
  if (buffered > 0) { //do by chunks
    do {
      if (writer < & data[sizeof(data) - 2]) {//2= room for char AND prophylactic null
        key = Serial.read();//don't read if we have no place for it.
        if (key == '\r') {//keyboard end of line
          *writer++ = '\n';//unix end of line, your serial monitor should be configured to convert \n into \r\n locally.
          *writer = 0;
          Serial.printf("Emitting message: %s\n", data);
          if (!me.send_message(BroadcastNode::Packet{writer - data, *data})) {
            Serial.println("Failed to broadcast message");
          }
          writer = data;//reset buffer pointer and keep on accepting strings.
        } else {
           *writer++ = key;
        }
      }
    } while (--buffered);
  }
}
