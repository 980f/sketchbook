/*
   This a nearly minimal wrapper for octoid.h which is a reworking of OctoBanger_TTL.

   It demonstrates how to make an add-in for psuedo outputs, a control that is not a digital output.

   

*/

///////////////////////////////////
//build options, things that we don't care to make runtime options
//FrameTweaking 1 enables emulation of OB's timing adjustment, 0 ignores OB's config.  (saves 1 byte config, 50 bytes code. Might not be worth the lines of code.
#define FrameTweaking 1
//used to allocate space in the EEPROM, 1000 leaves only a few bytes for custom configuration for your addons.
#define SAMPLE_END 1000
//number of controlled outputs. You can make this smaller but to be more than 8 will take some work in octoid, it packs them all into a byte for EEPROM efficiency. 
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
