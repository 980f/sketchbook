/*
   This a nearly minimal wrapper for octoid.h which is a reworking of OctoBanger_TTL.

   It demonstrates how to make an add-in for psuedo outputs, a control that is not a digital output.

   

*/

///////////////////////////////////
//build options, things that we don't care to make runtime options. These all have defaults, you can omit the #defines.
//FrameTweaking 1 enables emulation of OB's timing adjustment, 0 ignores OB's config.  (saves 1 byte config, 50 bytes code. Might not be worth the lines of code.
#define FrameTweaking 1
//used to allocate space in the EEPROM, 1000 leaves only a few bytes for custom configuration for your addons.
#define SAMPLE_END 1000
//number of controlled outputs. You can make this smaller but to be more than 8 will take some work in octoid, it packs them all into a byte for EEPROM efficiency. 
#define TTL_COUNT 8
// end of optional compile time options
///////////////////////


// compile time options must precede this header file
#include "octoid.h"

//////////////////////////////////////////////////////////////
// this is a user hook for implementing 'virtual outputs', things controlled by the octo sequencer
void octoid(unsigned pinish, bool action) {
  //print statements here will usually bog the system down horribly. Perhaps single chars can be printed, just not faster than 1000 per second (which would be very annoying and probably not possible without setting the frame time short)
  //pinish will be in the range 0 to 31 for configured pin number 48 through 127.
  return;
}

// example of managing shared EEPROM use
struct MyConfig {
  byte b='B';
  word w='W';
  float f=3.1828;
} myfig;


/** manages your EEPROM configuration */
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

////////////////////////////////////
// all you really need starts here with the header someplace above.

Octoid::Blaster B;

void setup() {
  B.setup();
//your code goes here
  if(true){ //overriding configuration until we know that it works, or if we detect it is invalid.
    B.cli.O.output[0]={5}; //force config low active D5
    B.cli.O.output[1]={12,0,1}; //force config, high active D12, often the builtin_led
    B.cli.O.save();
  }
  CliSerial.print("\nLive from New York!\n");
  
}

void loop() {
  bool ticked = MilliTicker.ticked();//true once per millisecond.
  B.loop(ticked);
//your code goes here
 
}
//end of octoid example sketch
