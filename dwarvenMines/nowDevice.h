#pragma once
const bool crippled = false;

#include <esp_now.h>

#include <cstdint>
#include <string.h>
#include "macAddress.h"
#include "simpleTicker.h"
#include "block.h"

const unsigned FX_channel = 6; //todo: discuss with crew, do we want to share with Hollis and Luma or avoid them, and what else is beaming around the facility?

///////////////////////////////////////////////
//communications manager:
class NowDevice {
  public:
    static unsigned debugLevel;//higher gets more spew
    /** a base class that you must derive your messages from.
    */
    class Message: public Printable  {
        friend class NowDevice;
      protected:
        virtual Block<uint8_t> incoming() = 0;
        virtual Block<const uint8_t> outgoing() const = 0;

        /** copy in content, can't trust that the data will stay allocated, it could be on stack.
            NB: using unsigned as len converts a negative length to an enormous one, and that makes min() work.
           default is binary copy, can implement a text parser if that floats your boat.
        */
        virtual void receive(const uint8_t *incomingData, unsigned len) {
          auto buffer = incoming();
          memcpy(&buffer.content, incomingData, min(len, buffer.size));
        }

        size_t printTo(Print &debugger) const override {
          //you don't have to output diagnostic info if you don't want to.
          return 0;
        }
    };
    //  protected:

    /////////////////////////////////
    const Message* lastMessage = nullptr;
    bool messageOnWire = false; //true from send attempt if successful until 'OnDataSent' is called.

    struct SendStatistics {
      unsigned Attempts = 0;
      unsigned Failures = 0;
      unsigned Successes = 0;
    };
    static SendStatistics stats;
  public:
    static NowDevice *sender; //only one sender is allowed at this protocol level.
    //  protected:
    bool autoEcho = true;// until the worker replies to the boss we pretend we got an echo back from them
    //ACK/NACK thunk
    static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
      bool failed = status != ESP_NOW_SEND_SUCCESS;
      //maydo: check that mac_addr makes sense.
      if (debugLevel > 100) {
        Serial.print("\r\nLast Packet Send Status:\tDelivery ");
        Serial.println(failed ? "Failed" : "Succeeded"); //only two values are documented, no textifier for more detail.
      }
      if (failed) {
        //        if (debugLevel > 50 && (stats.Attempts - stats.Failures) < 20) {
        //          Serial.println("Failure code: (%d) %s\n", status, esp_err_to_name(status));
        //        }
        ++stats.Failures;
      } else {
        ++stats.Successes;
      }
      if (sender) {
        sender->messageOnWire = false;
        sender->onSend(failed);
      } else {
        Serial.println("no sender configured");
      }
    }

    MacAddress *remote = nullptr;

    void sendMessage( const Message &newMessage) {
      // Set values to send
      lastMessage = &newMessage;//todo: (formal) need to copy object
      if (!crippled && lastMessage) {
        ++stats.Attempts;
        auto buffer = lastMessage->outgoing();
        esp_err_t result = esp_now_send(*remote, &buffer.content, buffer.size);
        if (stats.Failures < 10) {
          Serial.printf("esp_now_send returned %d %s on send of %u bytes\n", result, esp_err_to_name(result), buffer.size);
        }
        messageOnWire = result == OK;
        if (! messageOnWire) {
          ++stats.Failures;
        }
      }
    }

    virtual void onSend(bool failed) {
      //no implementation required.
    }

    /////////////////////////////////
    Message *message = nullptr; //incoming
    bool dataReceived = false;//todo: replace with pair of counters.

    // callback function that will be executed when data is received
    void onMessage(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
      if (message) {
        message->receive(incomingData, len);
        if (debugLevel > 50) {
          Serial.print("Bytes received: ");
          Serial.println(len);
          Serial.print(*message);
        }
        dataReceived = true;
      } else {
        if (debugLevel > 10) {
          Serial.print("Bytes ignored: ");
          Serial.println(len);
        }
      }

    }

  public:
    static NowDevice *receiver; //only one receiver is allowed at this protocol level.
    // receiver thunk
    static void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {//esp32 is not c++ friendly in its callbacks.
      if (receiver) {
        receiver->onMessage(esp_now_info, incomingData, len);
      } else {
        if (debugLevel > 30) {
          Serial.print("Bytes ignored: ");
          Serial.println(len);
        }
      }
    }

    static esp_err_t reportError(esp_err_t error, const char*context) {
      if (error != ESP_OK) {
        Serial.printf("%s due to (%d) %s\n", context, error , esp_err_to_name(error));
      }
      return error;
    }

    /////////////////////////////////
    static unsigned setupCount;//=0;
  public:
    MacAddress ownAddress{0}; //will be all zeroes at startup

    virtual void setup(Message &receiveBuffer) {
      message = &receiveBuffer;
      //todo: use a better check for whether this has already been called, or even better have a lazy init state machine run from the loop.
      if (setupCount++) {
        Serial.printf("Attempted to setup ESP_NOW %u times.", setupCount);
        return;
      }
      // Init ESP-NOW
      WiFi.mode(WIFI_MODE_STA);//essential!
      if (esp_now_init() == ESP_OK) {
        receiver = this;
        esp_now_register_recv_cb(&OnDataRecv);
        sender = this;
        esp_now_register_send_cb(&OnDataSent);
        Serial.println("esp_now_init OK");
      } else {
        Serial.println("Error initializing ESP-NOW");
        --setupCount;
      }
      delay(357);
      Serial.print(" My Mac: ");
      Serial.println(WiFi.macAddress());
    }

    esp_err_t addPeer(const MacAddress &knownPeer, bool spamme) {
      // BTW:ownAddress is all zeroes until after NowDevice::setup.
      esp_now_peer_info_t peerInfo;
      memset(&peerInfo, 0, sizeof(peerInfo));//there appear to be hidden fields needing zero init.
      // Register peer
      knownPeer >> peerInfo.peer_addr;
      if (spamme) {
        Serial.println("Mac given to espnow peer");
        for (unsigned i = 0; i < 6; ++i) {
          Serial.printf(":%02X", peerInfo.peer_addr[i]);
        }
        Serial.println();
      }
      peerInfo.channel = 0; // defering to some inscrutable default selection. Should probably canonize a "show channel" and a different one for luma and hollis.
      peerInfo.encrypt = false;
      return esp_now_add_peer(&peerInfo);
    }

    virtual void loop() {
      //empty loop rather than =0 in case extension doesn't need a loop
      //if we lazy init this is where that executes.
    }

    virtual void onTick(MilliTick now) {
      //empty loop rather than =0 in case extension doesn't need timer ticks
    }

};

unsigned NowDevice::debugLevel = 0; //higher gets more spew
