//project defines, stepper motor tester, leonardo version

//don't need special attributes for isr's, so blank define this.
#define ISRISH 

#define SEEEDV1_2 1

#define UsingSpibridges 1


#ifdef ARDUINO_AVR_LEONARDO
#define Serial4Debug Serial

#define SerialRing Serial1
#endif
