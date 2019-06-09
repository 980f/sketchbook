#pragma once //(C) 2019 Andy Heilveil, github/980f

/** data for human time given milliseconds*/
class HMS {
  public:
    int hour;
    int minute;
    int sec;
    int mils;

    //arg type here is the return type of arduino standard millis() function:
    HMS(long tick);
    /** parse human readable time into separate fields.*/
    void setTimeFrom(const char *value) ;
};
