//project defines, stepper motor tester, leonardo version

//can now include both. Leaving in flags so that systems dedicated to one or the other have room for a bit more code
//#define L298 SEEEDV1_2
#define L298 AndyProMicro
#define UsingSpibridges 0


#ifdef ARDUINO_AVR_LEONARDO
#define Serial4Debug Serial
#define MotorDebug Serial
#define SerialRing Serial1
#endif
