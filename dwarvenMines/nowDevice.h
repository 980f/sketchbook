#pragma once

///////////////////////////////////////////////
//communications manager:
//todo: replace template with helper class or a base class for Messages.
template <class Message> class NowDevice {
  protected:

    /////////////////////////////////
    Message lastMessage;
    bool messageOnWire = false; //true from send attempt if successful until 'OnDataSent' is called.

  struct SendStatistics {
    unsigned Attempts = 0;
    unsigned Failures = 0;
    unsigned Successes = 0;
  };
  static SendStatistics stats;
  public:
    static NowDevice *sender; //only one sender is allowed at this protocol level.
  protected:
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
        //if there is a requested one then send that now.
      }
    }

    void sendMessage(const Message &newMessage) {
      // Set values to send
      lastMessage = newMessage;
      ++stats.Attempts;
      // Send message via ESP-NOW
      esp_err_t result = esp_now_send(workerAddress, reinterpret_cast < uint8_t *>(&lastMessage), sizeof(Message));
      messageOnWire = result == OK;
      if (!messageOnWire) {
        ++stats.Failures;
      }
    }

    /////////////////////////////////
    Message message;//incoming
    bool dataReceived = false;//todo: replace with pair of counters.

    // callback function that will be executed when data is received
    void onMessage(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
      memcpy(&message, incomingData, sizeof(Message)); //todo: check len against sizeof(Message), they really must be equal or we have a serious failure to communicate.
      Serial.print("Bytes received: ");
      Serial.println(len);
      message.printOn(Serial);
      dataReceived = true;
    }

  public:
    static NowDevice<Message> *receiver; //only one receiver is allowed at this protocol level.
  protected:
    static void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {//esp32 is not c++ friendly in its callbacks.
      if (receiver) {
        receiver->onMessage(esp_now_info, incomingData, len);
      }
    }

    /////////////////////////////////

    static unsigned setupCount;//=0;
  public:
    MacAddress ownAddress{0}; //will be all zeroes at startup
    virtual void setup() {
      //todo: use a better check for whether this has already been called, or even better have a lazy init state machine run from the loop.
      if (setupCount++) {
        return;
      }
      // Set device as a Wi-Fi Station
      WiFi.mode(WIFI_STA);
      WiFi.macAddress(ownAddress);
      Serial.print("I am: ");
      ownAddress.PrintOn(Serial);
      //todo: other examples spin here waiting for WiFi to be ready.
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
      message = faker;
      dataReceived = true;
    }

};
