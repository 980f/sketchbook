#pragma once
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
          auto buffer=incoming();
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
    static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
      //todo: check that  mac_addr makes sense.
      Serial.print("\r\nLast Packet Send Status:\tDelivery ");
      bool failed = status != ESP_NOW_SEND_SUCCESS;
      Serial.println(failed ? "Failed" : "Succeeded");
      if (failed) {
        ++stats.Failures;
      } else {
        ++stats.Successes;
      }
      if (sender) {
        sender->messageOnWire = false;
        //todo: if there is a requested one then send that now, at the moment the extended class will have to track that.
        if (sender->autoEcho && sender->lastMessage) {
          if (!failed) {
            sender->fakeReception(*sender->lastMessage);//yes, sendmessage to self, bypassing radio.
          }
        }
      }
    }

    MacAddress *remote = nullptr;
    void sendMessage( const Message &newMessage) {
      // Set values to send
      lastMessage = &newMessage;
      if (lastMessage) {
        ++stats.Attempts;
        auto buffer=lastMessage->outgoing();
        esp_err_t result = esp_now_send(*remote, &buffer.content, buffer.size);
        messageOnWire = result == OK;
        if (! messageOnWire) {//why does indenter fail on this line?
          ++stats.Failures;
        }
      }
    }

    /////////////////////////////////
    Message *message = nullptr; //incoming
    bool dataReceived = false;//todo: replace with pair of counters.

    // callback function that will be executed when data is received
    void onMessage(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
      if (message) {
        message->receive(incomingData, len);
        Serial.print("Bytes received: ");
        Serial.println(len);
        Serial.print(*message);
        dataReceived = true;
      } else {
        Serial.print("Bytes ignored: ");
        Serial.println(len);
      }

    }

  public:
    static NowDevice *receiver; //only one receiver is allowed at this protocol level.
    //  protected:
    static void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {//esp32 is not c++ friendly in its callbacks.
      if (receiver) {
        receiver->onMessage(esp_now_info, incomingData, len);
      }
    }

    /////////////////////////////////

    static unsigned setupCount;//=0;
  public:
    MacAddress ownAddress{0}; //will be all zeroes at startup

    virtual void setup(Message &receiveBuffer) {
      message = &receiveBuffer;
      //todo: use a better check for whether this has already been called, or even better have a lazy init state machine run from the loop.
      if (setupCount++) {
        return;
      }     
      // Init ESP-NOW
      if (esp_now_init() == ESP_OK) {
        receiver = this;
        esp_now_register_recv_cb(&OnDataRecv);
        sender = this;
        esp_now_register_send_cb(&OnDataSent);
      } else {
        Serial.println("Error initializing ESP-NOW");
        --setupCount;
      }
    }

    virtual void loop() {
      //empty loop rather than =0 in case extension doesn't need a loop
      //if we lazy init this is where that executes.
    }

    virtual void onTick(MilliTick now) {
      //empty loop rather than =0 in case extension doesn't need timer ticks
    }

    //talking to yourself, useful when the remote is an option and one device might be doing both boss and worker roles.
    void fakeReception(const Message &faker) {
      message = const_cast<Message *>( &faker);//# ok to const cast as we only modify it when actually receiving a message and only call fakeReception when nothing is being sent.
      dataReceived = true;
    }

};
