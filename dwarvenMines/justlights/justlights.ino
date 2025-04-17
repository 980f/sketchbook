#define BroadcastNode_WIFI_CHANNEL 6

#ifdef ARDUINO_LOLIN_C3_MINI
#define BOARD_LED 8
const unsigned LED_PIN = 3; // drives the chain of programmable LEDs
#else //presume WROOM-DA
#define BOARD_LED 22
const unsigned LED_PIN = 13; // drives the chain of programmable LEDs
#endif

bool TRACE = false; //must preceded the various includes.
bool BUG3 = false;
#include "cheaptricks.h"
#include "vortexLighting.h"

void explode() {
  Serial.println("Rebooting in a few seconds... hoping that magically fixes things");
  for (unsigned countdown = 4; countdown-- > 0;) {
    Serial.printf("\t%d", countdown);
    delay(1000);
  }
  ESP.restart();
}

struct JustLighting : public VortexCommon {

  bool setup() {
    VortexLighting::setup();
    return BroadcastNode::begin(true);//todo: move this into VC
  }

  void onReceive(const uint8_t *data, size_t len, bool broadcast = true) override {
    if (message.accept(Packet{len, *data})) {
      //all action is in loop();
    } else {
      auto outgoing = message.outgoing();
      Serial.printf("Unknown packet: %.*s %d\tOnly know %.4s %d\n", len, data, len, &outgoing.content, outgoing.size); 
    }
  }

  void loop() {
    if (flagged(message.dataReceived)) { // message received
      if (TRACE) {
        if (BUG3) {
          dumpHex(message.incoming(), Serial);
        }
        command.printTo(Serial);
      }
      //this is a repeater, don't acknowledge the command. Might add a pin to condition this.
      //      message.tag[1] |= 0x20 ; //lower case the tag 2nd char for debug.
      //      apply(message);//which sends it back out as well as sending to local lights
      VortexLighting::apply(command);
    }
  }

//  // this is called once per millisecond, from the arduino loop().
//  // It can skip millisecond values if there are function calls which take longer than a milli.
//  void onTick(MilliTick ignored) {
//    // not yet animating anything, so nothing to do here.
//  }

};

JustLighting my;

void setup() {
  Serial.begin(115200);
  Serial.printf("Program: %s  Running on WiFi channel %u\n\n", __FILE__ , BroadcastNode_WIFI_CHANNEL);
  my.spew = true;

  if (!my.setup()) {
    Serial.println("Failed to initialize ESP-NOW");
    explode();
  }
  Serial.println("Top level Setup complete.");
}

CRGB testColor[] = {0x660000, 0x006600, 0x000066, 0x666600, 0x006666, 0x660066};

void runTest() {
  my.command.showem = true;
  my.VortexLighting::apply(my.command);//todo: figure out why gcc can't find apply(Command &) without assistance
}

void loop() {
//  if (Ticker::check()) { // read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
////    my.onTick(Ticker::now);
//  }
  my.loop();

  if (Serial.available()) {
    auto key = Serial.read();
    Serial.printf("Command %c [%d]\n", char(key), int(key) );
    switch (key) {
      case 26:
        explode();
        break;
      case '0': case '1': case '2': case '3': case '4': case '5':
        my.command.setAll(testColor[key - '0']);
        Serial.print(my.command);
        runTest();
        break;
      case 13:
        runTest();
        break;
      case 'r'://rainbox byapssing as much code as possible.
        for (unsigned pixie = VortexFX.total; pixie-- > 0;) {
          pixel[pixie] = testColor[pixie % 6];
        }
        my.stringer.show();
        break;
//      case ' ':
//        //todo: dump pixels array?
//        break;
//      case '.':
//        break;
      case '=':
        my.dumpHex(my.message.outgoing(), Serial);
        break;
      case 'D':
        TRACE = true;
        break;
      case 'd':
        TRACE = false;
        break;
    }
  }
}
