//project defines, stepper motor tester, leonardo version

//don't need special attributes for isr's, so blank define this.
#define ISRISH 

//#define SEEEDV1_2

#define UsingDRV8833 0
#define UsingL298 1

//common aspect of L298 and DRV8333 is the active drive pattern of complementary output pairs.
#define UsingBicomp 1

#define Serial4Debug Serial