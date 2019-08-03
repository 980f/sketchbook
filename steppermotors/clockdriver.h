#pragma once

/** api to the .ino code for webserver control */

#include "hms.h" //HoursMinutesSeconds, not the Pinafore.
class ClockDriver {
  public:
    HMS target;//intermediary for RPN commands
    ClockDriver();

    void runReal();
    void setMinute(unsigned arg,bool raw=false);
    void setHour(unsigned arg,bool raw=false);
//emit debug information to its own logger
    void dump();
};
