#pragma once

#ifndef ESPNOW_WIFI_CHANNEL
#warning "using 6 for wifi channel, #define ESPNOW_WIFI_CHANNEL before including this header if you don't like that"
#define ESPNOW_WIFI_CHANNEL 6
#endif

#include "ESP32_NOW.h"
#include "WiFi.h"

#include <esp_mac.h>  // For the MAC2STR and MACSTR macros

#define ESPNOW_Triplet ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr


class BroadcastNode : public ESP_NOW_Peer {
    static void new_node_thunk(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
      reinterpret_cast<BroadcastNode *>(arg)->unknown_node(info, data, len);
    }

  public:
    //debug control flags
    bool spew = false;
    bool errors = true;

    BroadcastNode (uint8_t channel, wifi_interface_t iface, const uint8_t *lmk) : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR,  channel, iface, lmk) {
      //don't do anything on construction, so that we can statically construct.
    }
    ~BroadcastNode () {}

    virtual bool validMessage(const uint8_t *data, size_t len) {
      return true;//default promiscuous
    }

    // Function to send a message to all devices within the network
    bool send_message(const uint8_t *data, size_t len) {
      if (send(data, len)) {
        return true;
      }
      if (errors) {
        Serial.println("Failed to broadcast message");
      }
      return false;
    }

  protected:
    // Function to print the received messages from the master
    void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {
      if (spew) {
        if (broadcast) {
          Serial.printf("Received a broadcast message:\n");
        } else {
          Serial.printf("Received a message from master " MACSTR ":\n", MAC2STR(addr()));
        }
        Serial.printf("  Message: %s\n", reinterpret_cast<const char * > (data));
      }
    }

  private:
    // Callback called when an unknown peer sends a message
    void unknown_node(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
      if (spew) {
        if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0) {
          Serial.printf("Node " MACSTR " sent a broadcast message\n", MAC2STR(info->src_addr));
        } else {
          Serial.printf("Received a unicast message from " MACSTR, MAC2STR(info->src_addr));
        }
      }
      //here is where we could qualify the peer and if its message indicates it is on our network than "add_peer" it and process the message.
      if (validMessage(data, len)) {
        //todo: construct peer from info->src_addr and esp_now add that.
        onReceive(data, len, true);//message from nodes that are not added are not sent to onReceive by ESP library.
      }
    }
  public:
    /** typically called from setup on your sole statically created BroadcastNode with (true,false) */
    bool begin(bool isLocal, bool isReceiveOnly) {
      int startupTime = - millis();
      WiFi.mode(WIFI_STA);
      WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
      while (!WiFi.STA.started()) {//todo: hook events and get rid of this nominally infinite loop.
        delay(100);//measured: 100,191,257, seems to warmup and converge around 180. Seems to depend upon how many nodes are live.
      }
      startupTime += millis();
      if (spew) {
        Serial.printf("Startup took around %d ms\n", startupTime);
      }

      if (errors) {
        Serial.printf("Using Channel: %d\n", ESPNOW_WIFI_CHANNEL);
        Serial.println("Own  MAC Address: " + WiFi.macAddress());
      }

      // Initialize the ESP-NOW protocol
      if (!ESP_NOW.begin()) {
        if (errors) {
          Serial.println("Failed to initialize ESP-NOW");
        }
        return false;
      }
      if (isLocal) {
        ESP_NOW.onNewPeer(new_node_thunk, this);
      }
      if (!isReceiveOnly) {
        add();//adds self to list, not sure if that is actually needed.
      }
      return true;
    }

};
