#include "hms.h"
#include "char.h"

HMS::HMS(long tick) {
  mils = tick % 1000;
  sec = (tick / 1000) % 60;
  minute = (tick / (60 * 1000U)) % 60;
  hour =  (tick / (60 * 60 * 1000U)) % 12;
  if (hour == 0) {
    hour = 12;
  }
}

void HMS::setTimeFrom(const char *value) {
  int *field = &hour;
  while (char c = *value++) {
    Char see(c);
    if (!see.appliedDigit(*field)) {
      if (see == ':') {
        field = &minute; //note: seconds is not yet supported.
      } else {
        //        dbg("Bad hh:mm> ", value);
        //and try to survive it
      }
    }
  }
}
