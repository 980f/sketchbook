octoid is a merger of a highly cleaned up OctoBlaster_TTL and James M's OctoField field programmer for it.

At present it is missing the interface to a programming panel, an I2C version is imminent.
Also coming soon (possibly first) is QuadBanger mode, 4 outputs 4 programming inputs.

Conditional inclusion of audio track device would free up a pin, and we have a very nice audio device in use at Scare that has easier interfacing than the banger supported ones.
Note: the octobanger implements a uart transmit only connection for the audio device, using asm for precise timing and killing the Serial while it is active. They could have used some gates to share the TX line and saved a lot of programming effort.

A small effort could look at a jumper to decide whether the device is a unit or a programmer. That way boards can be preprogrammed on the shelf and then installed in either kind of carrier with the carrier jumpered for which end of the link it is.

There is a clone module which with the addition of a button designation would allow one to program another's sample table and other settings.

I2C programmer:
8 bistable switches for relay content

bistable switch for RECORD mode
momentary for trigger
momentary for save
? what else

LED for RECORDing active
LED for RECORD pending (needs save or will be lost on reset)
LED for busy? (echo of device lamp)
? what else


LED for power?
