octoid is a merger of a highly cleaned up OctoBlaster_TTL and James Manley's OctoField field programmer for it.


Differences from OctoBlaster_TTL:

* does not block the serial command interface while doing things, in fact all blocking has been removed. Any 'business logic' blocking is done with state machines, not delay() or functions which have long execution. 
* sets of hard coded pins with a configurable selection of which set has been replaced with using EEPROM to select Arduino pin and polarity for the output channels and 2 trigger pins. You break a relay in the field and you can fix it in the field without a firmware download.
* (soon) accepts configuration from OctoBlaster tools, which config style is determinable from number of config bytes being sent.
* timeouts on program download has been replaced with watching for 5 '@'s in a row which is an unnecessary program sequence and allows the host/programmer to blow off a download at will without having to wait for whatever the timeout was. This is especially handy when debugging sending a program, you can single step and breakpoint in your transmitting routines without the octoid blowing you off.
* does NOT use ram for the full program, which is a massive reduction in ram use.
* audio support removed, was blocking in places and can be brought back in as a psuedo output.
* the trigger out is configurable polarity, handy for when you need to stick an inverting amp into your system at the last minute (a.k.a. known as a transistor)
* (soon) the trigger out is configurable width in milliseconds.
* (soon) the trigger in debounce time is configurable.
* (almost) has a mode where the channels are inputs rather than outputs and these are recorded and then downloaded to another blaster to run the program
* (soon) an input otherwise not used decides whether the device is a blaster or a programmer. That way boards can be preprogrammed on the shelf and then installed in either kind of carrier with the carrier jumpered for which end of the link it is.
* (soon) supports program recording from a dumb panel of switches.
* There is a clone function which with the addition of a button designation would allow one to program one blaster from another (copy sample table and other settings).
* possible: do ICSP download copying its own binary into the target device. This depends upon the ArduinoISP source code being small enough to coreside, and it probably is.

Why all these changes?
You can now add very significant additional features as long as they also do not do blocking and have the octoblaster functionality be a component part of your system. There is a spot where you implement turning your special gizmo on or off per the sequencer, and the usual setup() and loop() are very small and so it is easy to add your own code to each.

New logic features that would be easy to implement:
* mark location and loop to it from the end until trigger removed.
* configure edge vs level on trigger.



I2C programmer:

One variant of programming panel could be over I2C or SPI using a simple 16 bit GPIO device.
It would interface to:
- 8 bistable switches for relay content
- a bistable switch for RECORD mode
- a momentary for trigger
- a momentary for save
- ? what else

- LED for RECORDing active
- LED for RECORD pending (needs save or will be lost on reset)
- LED for busy? (echo of device lamp)
- ? what else

An LED for power would not attach to the GPIO.


Note: the octobanger implements a uart transmit-only connection for the audio device, using asm for precise timing and killing usage of the Serial while it is active. They could have used some gates to share the TX line and saved a lot of programming effort. 

