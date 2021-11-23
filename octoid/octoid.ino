/*
   inspird by OctoBanger_TTL, trying to be compatible with their gui but might need a file translator.
   Octobanger had blocking delays in many places, and halfbaked attempts to deal with the consequences.
   If the human is worried about programming while the device is operational they can issue a command to have the trigger ignored.
   By doing that 1k of ram is freed up, enough for this functionality to become a part of a much larger program.

  todo: guard against user spec'ing rx/tx pins
  todo: debate whether suppressing the trigger locally should also suppress the trigger output, as it presently does
  note: trigger held active longer than sequence results in a loop.  Consider adding config for edge vs level trigger.
  todo: package into a single class so that can coexist with other code, such as the flicker code.
  test: receive config into ram, only on success burn eeprom.
  test: instead of timeout on host abandoning program download allow a sequence of 5 '@' in a row to break device out of download.
  todo: abort/disable input signal needed for recovering from false trigger just before a group arrives.
  todo: temporary mask via pin config, ie a group of 'bit bucket'

  Timer tweaking:
  The legacy technique of using a tweak to the millis/frame doesn't deal with temperature and power supply drift which, only with initial error.

  If certain steps need to be a precise time from other steps then use a good oscillator.
  In fact, twiddling the timer reload value of the hardware timer fixes the frequency issue for the whole application.
  todo: finish up the partial implementation of FrameSynch class.

*/

///////////////////////////////////
//build options, things that we don't care to make runtime options
//FrameTweaking 1 enables emulation of OB's timing adjustment, 0 ignores OB's config.  (saves 1 byte config, 50 bytes code. Might not be worth the lines of code.
#define FrameTweaking 1
#define SAMPLE_END 1000
//you can make this smaller but to be more than 8 will take some work in octoid, it packs them all into a byte for EEPROM efficiency. 
#define TTL_COUNT 8

#include "octoid.h"
Octoid::Blaster B;


//////////////////////////////////////////////////////////////
bool fakebits[TTL_COUNT];
void octoid(unsigned pinish, bool action) {
  //print statements here will usually bog the system down horribly. Perhaps single chars can be printed, just not faster than 1000 per second (which would be very annoying and probably not possible without setting the frame time short)
  return;
}

struct MyConfig {
  byte b='B';
  word w='W';
  float f=3.1828;
} myfig;


/** your EEPROM configuration */
void octoidMore(EEAddress start, byte sized,  bool writeit) {
  if(sizeof(myfig)>sized){
    //print an error message to serial
    return;
  }
  if(writeit){
     EEPROM.put(start,myfig);
  } else {
    EEPROM.get(start,myfig);   
  }  
}

///////////////////////////////////////////////////////
void setup() {
  B.setup();
//your code goes here
}

void loop() {
  auto ticked = MilliTicked.ticked();
  B.loop(ticked);
//your code goes here
}
//end of octoid, firmware that understands octobanger configuration prototocol and includes picoboo style button programming with one or two boards.
