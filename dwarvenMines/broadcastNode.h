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

#include "block.h"

class BroadcastNode : public ESP_NOW_Peer {
    static void new_node_thunk(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {//declared by espressif
      reinterpret_cast<BroadcastNode *>(arg)->unknown_node(info, len, data);
    }

    /** this class was added due to documents which appear to be false, that claimed it was a requirement to extend the base class in order to have something useful.
      Since *we* are only registering peers to shortcut the "unknown" call back we could probably just create a base class entity and call its add() method then discard it.
      Note: the add always fails, perhaps some of the unlisted but "you must implement some functions" do matter. 
      Oh well. */
    struct AddaPeer: public ESP_NOW_Peer {
      esp_err_t failed;
      AddaPeer(const esp_now_recv_info_t *info): ESP_NOW_Peer(info->src_addr, BroadcastNode_Triplet) {
        failed = add();
      }
    };

  public:
    using Packet = Block<const uint8_t>;
    using Body = Block<uint8_t>;
    //debug control flags
    static bool spew;// = false;
    static bool errors;// = true;

    BroadcastNode (uint8_t channel, wifi_interface_t iface, const uint8_t *lmk = nullptr) : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR,  channel, iface, lmk) {
      //don't do anything on construction, so that we can statically construct.
    }
    //todo: remember that we added ourselves and remove ourselves, but we shouldn't be dynamically doing that so NYI.    ~BroadcastNode () {}

    // Function to send a message to all devices within the network
    bool send_message(const Packet &msg) {
      if (msg.isVacuous()) {
        return true; //vacuous messages are instantly sent successfully
      }
      esp_log_level_set("*", ESP_LOG_WARN);
      if (send(msg.content, msg.size)) {
        return true;
      }
      if (errors) {
        Serial.println("Failed to broadcast message");
      }
      return false;
    }


    static void dumpHex(unsigned len, const uint8_t *data, Print &stream) {
      stream.printf("Hex Dump: %u bytes Address:%p\n", len, data);
      while (len-- > 0) {
        stream.printf(" %02X", *data++);
        if (0 == len % 8) {
          stream.println();
        }
      }
      stream.println();
    }

    static void dumpHex(const Packet &buff, Print &stream) {
      dumpHex(buff.size, buff.content, stream);
    }

    static void dumpHex(const Body &buff, Print &stream) {
      dumpHex(buff.size, buff.content, stream);
    }

    //  protected:
    // Function to print the received messages from the master
    void onReceive(const uint8_t *data, size_t len, bool broadcast) override { //#prototype declared by espressif, can't change that.
      if (spew) {
        Serial.printf("onReceive called on (%p)\n", this);
        Serial.printf("  Message: %s\n", reinterpret_cast<const char * > (data));
        dumpHex(Packet{len, data}, Serial);//#yes, making a Packet just to tear it apart seems like extra work, but it provides an example of use and a compile time test of source integrity.
      }
    }

    bool addJustReceived = false;

  private:
    // called (via thunk) when an unknown peer sends a message
    void unknown_node(const esp_now_recv_info_t *info, unsigned len, const uint8_t *data) {
      if (spew) {
        if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0) {//todo: apply our MAC class
          Serial.printf("Broadcast received from: " MACSTR "\n", MAC2STR(info->src_addr));
        } else {
          Serial.printf("Received a unicast message from " MACSTR, MAC2STR(info->src_addr));
        }
      }
      //here is where we could qualify the peer and if its message indicates it is on our network than "add_peer" it and process the message.
      addJustReceived = false;
      onReceive(data, len, true);//message from nodes that are not added are not sent to onReceive by ESP library.
//      if (addJustReceived) { //stifled incoming message in the rare case that it succeeded.
//        AddaPeer noob(info);
//        if (noob.failed) {
//          Serial.printf("Add peer failed with %s \n", esp_err_to_name( noob.failed));
//        }
//      }
    }
  public:
    /** typically called from setup on your sole statically created BroadcastNode with (true)
      isLocal seems to always be true, we are trying to ignore who sends a message, all context must be in the message itself.*/
    bool begin(bool isLocal) {
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
      if (!ESP_NOW.begin()) {
        if (errors) {
          Serial.println("Failed to initialize ESP-NOW");
        }
        return false;
      }
      if (isLocal) {//then this node handles "new peer" notifications
        ESP_NOW.onNewPeer(new_node_thunk, this);
      }

      add();//adds self to list, needed to get callbacks, although receive seems to work regardless.
      return true;
    }

};

bool BroadcastNode::spew = false;
bool BroadcastNode::errors = true;
