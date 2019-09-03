//project defines, stepper motor tester, leonardo version

//#define UsingL298 1 //SEEEDV1_2
#define UsingL298 2 //AndyProMicro
#define UsingSpibridges 0
//getting spammed by the motor not in use.
#define OnlyMotor 1


#ifdef ARDUINO_AVR_LEONARDO
#define Serial4Debug Serial
#define MotorDebug Serial
//SerialRing used 972 code bytes and ~200 ram bytes.
//#define SerialRing Serial1
#endif
