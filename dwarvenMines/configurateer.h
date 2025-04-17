#pragma once
#include "EEPROM.h"
/** a base class for a binary configuration object.
  First implementation will be a one-and-only instance */
template <class UserConfig> struct Configurable : public UserConfig {
  unsigned checker = 0; //checksum field

  static unsigned checksum(const UserConfig &cfg) {
    //todo: constexprIf for UserConfig having its own checksum routine
    //      else do checksum that is sensitive to all one's bytes and zeroes
    return 0x980F;//placeholder for development, will do an actual sum someday
  }

  void operator =(const UserConfig &other) {
    *reinterpret_cast<UserConfig *>(this) = other;
  }

  bool get() {
    EEPROM.begin(sizeof(*this));

    UserConfig temp;//will have default values
    EEPROM.get(0, temp);
    if (checker == checksum(temp)) {
      *this = temp; //requires UserConfig have a viable operator =
      return true;
    }
    return false;
  }

  void put() {
    checker = checksum(*this);
    EEPROM.put(0, *this);
    EEPROM.commit(); // needed for flash backed paged eeproms to actually save the info.
  }

  void factoryReset() {
    UserConfig temp;//will have default values
    *this = temp; //requires UserConfig have a viable operator =
  }

};
