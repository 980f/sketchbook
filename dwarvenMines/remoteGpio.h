#pragma once

/**
  Core of remote digital input device.
  First rendition 8 digital inputs

  Not much will be configurable initially

  lolin32 lite: 18,19,23,25,26,27,32,33,22

*/
#include "simpleDebouncedPin.h"
#include "simpleTicker.h" //send periodically even if no change, but also on change
#include "broadcastNode.h"
#include "block.h"

const unsigned numRemoteGPIO = 8;
struct RemoteGPIO: BroadcastNode {
  //  unsigned pinNumber[numRemoteGPIO];
  DebouncedInput gpio[numRemoteGPIO] {{18}, {19}, {23}, {25}, {26}, {27}, {32}, {33}};
  Ticker periodically;
  MilliTick period = 100;

#define ForPin(index) for(unsigned index=numRemoteGPIO;index-->0;)

  struct Message {
    const unsigned char prefix[4] = {'G', 'P', 'I', 'O'};
    uint8_t startMarker = 0;//simplifies things if same type as endMarker, and as zero is terminating null for prefix
    uint8_t padding[3]; //see if we can name the padding!
    //note: without packed attribute we have 3 spare bytes here, so marker could be [4] and 1,2,3 used for debug info
    /// body
    unsigned sequenceNumber = 0;//for debug or stutter detection
    bool value[numRemoteGPIO];
    /// end body
    /////////////////////////////
    uint8_t endMarker;//value ignored, not sent
    bool spew = false;
   
    ///////////////////////////
    size_t printTo(Print &stream) {
      size_t length = 0;
      auto packet = outgoing();
      length += stream.printf("%s \tlength:%u\tSequence#:%u\n", &packet.content, packet.size, sequenceNumber);
      ForPin(index) {
        length += stream.printf("\t[%u]=%x", index, value[index]);
      }
      stream.println();
      return length;
    }

    bool &operator[](unsigned index) {
      static bool trash;
      if (index < numRemoteGPIO) {
        return value[index];
      } else {
        return trash;
      }
    }

    /** format for delivery, content is copied but not immediately so using stack is risky.
        we check the prefix but skip copying it since we const'd it.
    */

    Block<const uint8_t> outgoing() const {
      return Block<const uint8_t> {(&endMarker - reinterpret_cast<const uint8_t *>(prefix)), *reinterpret_cast<const uint8_t *>(prefix)};
    }

    /** expect the whole object including prefix */
    bool isValidMessage(unsigned len, const uint8_t* data) const {
      auto expect = outgoing();
      bool yep = len >= expect.size && 0 == memcmp(data, &expect.content, sizeof(prefix));
      if (spew) {
        Serial.printf("GPIO?:%.*s\texpecting:%u, got %u\t%s\n", len, data, expect.size, len, yep ? "valid" : "clueless");
      }
      return yep;
    }

    Block<uint8_t> incoming()  {
      return Block<uint8_t> {(&endMarker - &startMarker), startMarker};
    }

    /** for efficiency this presumes you got a true from isValidMessage*/
    void parse(unsigned len, const uint8_t* data)  {
      auto buffer = incoming();
      memcpy(&buffer.content, data + sizeof(prefix), buffer.size);
    }

    bool accept(unsigned len, const uint8_t* data) {
      if (isValidMessage(len, data)) {
        parse(len, data);
        return true;
      }
      return false;
    }

  };

  Message toSend;

  bool shouldSend = false;

  void onTick() {
    unsigned numChanges = 0;
    ForPin(index) {
      if (gpio[index].onTick()) {
        ++numChanges;
      }
      toSend.value[index] = bool(gpio[index]);
    }
    if (numChanges > 0) {
      shouldSend = true;
    }
    if (periodically.done()) {
      shouldSend = true;
    }
  }

  RemoteGPIO(): BroadcastNode(BroadcastNode_Triplet) {}

  bool setup(MilliTick bouncer = 50) {
    ForPin(index) {
      gpio[index].filter(bouncer);
      gpio[index].setup(true);//true here makes the pin report that it has just changed to whatever its present value is.
    }
    periodically.next(0);//send immediately with initial values.
    return BroadcastNode::begin(true);
  }

  void post() {
    ++toSend.sequenceNumber;//for debug
    toSend.printTo(Serial);
    auto outer = toSend.outgoing();
    send_message(outer.size, &outer.content);

    periodically.next(period);
  }

  void loop() {
    if (flagged(shouldSend)) {
      post();
    }
  }

};
