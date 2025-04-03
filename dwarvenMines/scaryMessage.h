#pragma once

#include "broadcastNode.h" //for its Packet and Body definitions.
using Packet = BroadcastNode::Packet;
using Body = BroadcastNode::Body;

/** This class wraps application content with format identifier and markers for a binary parsing.
    NB: Stuff class cannot be virtual or bad things will happen since we do raw memory copy from incoming buffer to object, and unless binaries are perfectly synched that just doesn't work!
*/
template <class Stuff, unsigned IdLength = 4> struct ScaryMessage: public Printable {
  unsigned char prefix[IdLength];//todo: work on sharing the terminating null and the startMarker
  uint8_t startMarker = 0;//simplifies things if same type as endMarker, and as zero is terminating null for prefix
  //3 spare bytes here if we don't PACK
  uint8_t tag[3]="FX";
  Stuff m;
  /////////////////////////////
  uint8_t endMarker;//value ignored, not sent. Type for this and startMarker obviate fancy casting
  ////////////////////////////
  //local state.
  bool dataReceived = false;
  ///////////////////////////
  //init with {'x','x','x','x'}, only some compilers accept "xxxx" here and so we do the tedious thing.
  ScaryMessage(std::initializer_list<const unsigned char> idchunk) {
    unsigned index = 0;
    for (auto item : idchunk) {
      prefix[index++] = item;
    }
  }

  Packet outgoing() const {
    return Packet {(&endMarker - reinterpret_cast<const uint8_t *>(prefix)), *reinterpret_cast<const uint8_t *>(prefix)};
  }

  /** expect the whole object including prefix */
  bool isValidMessage(const Packet& msg) const {
    auto expect = outgoing();
    if (TRACE) {
      Serial.printf("Checking: %u %s\n", expect.size, &expect.content);
    }
    return msg.size >= expect.size && 0 == memcmp(&msg.content, &expect.content, sizeof(prefix));
  }

  Body incoming()  {
    return Body {(&endMarker - &startMarker), startMarker};
  }

  /** for efficiency this presumes you got a true from isValidMessage*/
  bool parse(const Packet &packet)  {
    Body buffer = incoming();
    memcpy(&buffer.content, &packet.content + sizeof(prefix), buffer.size);
    dataReceived = true;
    return true;//no further qualification at this time
  }

  bool accept(const Packet &packet) {
    if (isValidMessage(packet)) {
      return parse(packet);
    } else {
      return false;
    }
  }

  size_t printTo(Print &stream) const override {
    size_t length = 0;
    auto packet = outgoing();
    return stream.printf("%s \tlength:%u\n", &packet.content, packet.size) + m.printTo(stream);
  }

};
