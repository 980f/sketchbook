#pragma once

#ifndef BroadcastNode_WIFI_CHANNEL
#warning "using 6 for wifi channel, #define BroadcastNode_WIFI_CHANNEL before including this header if you don't like that"
#warning "and since you are new to this library you will likely want to pass BroadcastNode_Triplet as the constructor arg"
#define BroadcastNode_WIFI_CHANNEL 6
#endif

#include <ESP32_NOW.h>
#include <WiFi.h>

#include <esp_mac.h>  // For the MAC2STR and MACSTR macros

#define BroadcastNode_Triplet BroadcastNode_WIFI_CHANNEL, WIFI_IF_STA, nullptr


class BroadcastNode : public ESP_NOW_Peer {
    static void new_node_thunk(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {//declared by espressif
      reinterpret_cast<BroadcastNode *>(arg)->unknown_node(info, len, data);
    }

  public:
    //debug control flags
    static bool spew;// = false;
    static bool errors;// = true;

    BroadcastNode (uint8_t channel, wifi_interface_t iface, const uint8_t *lmk = nullptr) : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR,  channel, iface, lmk) {
      //don't do anything on construction, so that we can statically construct.
    }
    //todo: remember that we added ourselves and remove ourselves, but we shouldn't be dynamically doing that so NYI.    ~BroadcastNode () {}

    virtual bool validMessage(size_t len, const uint8_t *data) {
      return true;//default promiscuous
    }

    // Function to send a message to all devices within the network
    bool send_message(size_t len, const uint8_t *data) {
      if (len == 0 || data == nullptr) {
        return true; //vacuous messages are instantly sent successfully
      }
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
    void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {//declared by espressif
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
    // called (via thunk) when an unknown peer sends a message
    void unknown_node(const esp_now_recv_info_t *info, unsigned len, const uint8_t *data) {
      if (spew) {
        if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0) {//todo: apply our MAC class
          Serial.printf("Node " MACSTR " sent a broadcast message\n", MAC2STR(info->src_addr));
        } else {
          Serial.printf("Received a unicast message from " MACSTR, MAC2STR(info->src_addr));
        }
      }
      //here is where we could qualify the peer and if its message indicates it is on our network than "add_peer" it and process the message.
      if (validMessage(len, data)) {
        //todo: construct peer from info->src_addr and esp_now add that.
        onReceive(data, len, true);//message from nodes that are not added are not sent to onReceive by ESP library.
      }
    }
  public:
    /** typically called from setup on your sole statically created BroadcastNode with (true,false) */
    bool begin(bool isLocal, bool isReceiveOnly) {
      int startupTime = - millis();
      WiFi.mode(WIFI_STA);
      WiFi.setChannel(BroadcastNode_WIFI_CHANNEL);
      while (!WiFi.STA.started()) {//todo: hook events and get rid of this nominally infinite loop.
        delay(100);//measured: 100,191,257, seems to warmup and converge around 180. Seems to depend upon how many nodes are live.
      }
      startupTime += millis();
      if (spew) {
        Serial.printf("Startup took around %d ms\n", startupTime);
      }

      if (errors) {
        Serial.printf("Using Channel: %d\n", BroadcastNode_WIFI_CHANNEL);
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

bool BroadcastNode::spew = false;
bool BroadcastNode::errors = true;
