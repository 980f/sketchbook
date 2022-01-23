libTester sketch

is for regression testing 980F's shared arduino pieces.

digitalPins record polarity, so that you can define 'true/on' per program logic but then independently define true to be high or low.
This is especially useful when you have a last minute change to add a signal buffer that happens to be inverting.

--
millievent is a baisc wrapper around common uses of the millis() time function.
MilliTicked is a shared entity for reading the clock once per loop, so that multiple actions share a time value when they execute in the same invocation of loop.
OneShots are for timeouts where the value changes with each firing, and is known where the firing occurs.
MonoStables are for timeouts where the value is set in code far remote from where the firing occurs. 
BiStables are for simple periodic waveform generation, tweaked by SoftPWM to stay running at the same rate between pwm period updates.

--


