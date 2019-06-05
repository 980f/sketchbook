//wheter EDS IR module is to be talked to:
#define UsingEDSir 1

//pick a motor interface:
#define UsingUDN2540 0
#define UsingDRV8833 0
#define UsingULN2003 1

//arduino processor designators are awkward to read so I map them to my style:

//board is defective for this choice, can't escape the quotes where it needs to be done:#if ARDUINO_BOARD == \"ESP8266_WEMOS_D1MINI\"
#ifdef ARDUINO_ESP8266_WEMOS_D1MINI
#define UsingESP32 0
#define UsingD1 1
#endif

#ifdef ARDUINO_ARCH_ESP32
#define UsingESP32 1
#define UsingD1 0
#endif
