#define BroadcastNode_WIFI_CHANNEL 6

bool TRACE = false; //must preceded remoteGPIO for it to use it
#include "remoteGpio.h"
RemoteGPIO my;

//initial version hard coded 8 low active inputs
const unsigned lolin32_lite[] = {18, 19, 22, 25, 26, 27, 32, 33, 23, 16, 17 };
//C3 comes with nice breadboard base
const unsigned c3_super_mini[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}; //they made this simple :) fyi: 20,21 are uart


void explode() {
  Serial.println("Rebooting in a few seconds... hoping that magically fixes things");
  for (unsigned countdown = 4; countdown-- > 0;) {
    Serial.printf("\t%d", countdown);
    delay(1000);
  }
  ESP.restart();
}


void setup() {
  Serial.begin(460800);
  Serial.printf("Program: %s  Running on WiFi channel %u\n\n", __FILE__ , BroadcastNode_WIFI_CHANNEL);
  my.spew = true;

  if (!my.setup(55)) {
    Serial.println("Failed to initialize ESP-NOW");
    explode();
  }
  //  my.forceMode(INPUT_PULLUP);//GP25 seems to become a low output at random, likely during wifi setup.
  Serial.println("Setup complete.");
}

//espressif defines this in global namespace:
#undef cli
#include "sui.h"

SUI<unsigned, true, 3> dbg(Serial, Serial);

//edited in pieces, applyed via '!'
RemoteGPIO::PinFig fig;
// uint8_t number;
// byte mode;
// bool highActive;
// MilliTick ticks;//debounce for input, pulsewidth for output, 0 is stay on, Never is don't ever go on.
//unsigned pinOfInterest = ~0;

void canned(Block<const unsigned> set) {
  Serial.println("\nApplying canned config:\n");
  for (unsigned pi = set.quantity(); pi-- > 0;) {
    fig.number = set[pi];
    fig.highActive = false;
    fig.ticks = 57;//tracer value
    fig.mode = INPUT_PULLUP;
    Serial.printf("\tchannel[%d] ", pi);
    Serial.print(fig);
    my.cfg.pin[pi] = fig;
    Serial.print(my.cfg.pin[pi]);
  }
  Serial.println();
}

void clido(const unsigned char key, bool wasUpper) {
  Serial.print(": ");
  unsigned param = dbg.cli[0];//many options take this hard to type expression
  switch (key) {
    case '!':
      if (param < RemoteGPIO::numPins) {
        Serial.printf("setting %d to ", param);
        Serial.print(fig);
        Serial.println();
        my.cfg.pin[param] = fig;
        my.confMessage.dataReceived = true;
      }
      break;
    case 'n'://pick a pin configuration index
      fig.number = param; //have to trust that it is valid
      break;
    case 'i':
      fig.mode = param == 1 ? INPUT_PULLUP : INPUT;
      break;
    case 'o':
      fig.mode = OUTPUT;
      //todo: how do we configure open drain?
      break;
    case 'p':
      if (param < 2) {
        fig.highActive = param;
      }
      break;
    case 't':
      fig.ticks = param;
      break;
    case 'k'://canned configurations
      Serial.printf("Selecting builtin config #%d\n", param);
      switch (param) {
        case 0://lolin32
          canned({8, lolin32_lite}); //only 8 inputs on original
          break;
        case 1://C3 super mini
          canned({11, c3_super_mini}); //11 out of 13, leaving the rx/tx available
          break;
        default:
          Serial.println("That is not a valid index");
          break;
      }
      break;
    case ' ':
      //      ForPin(index) {
      //        Serial.printf("\t[%u]", index);
      //        my.cfg.pin[index].printTo(Serial);
      //        my.pingus[index].printTo(Serial);
      //        Serial.println();
      //      }
      Serial.print(my.cfg);
      Serial.println();
      Serial.print(my.command);
      Serial.println();
      break;
    case 26:
      explode();
      break;
    case '?':
      Serial.println("Remote GPIO. \n<pinnumber>n\t i or o \t<0|1>p polarity \t<ms>t debounce or pulse width \n <0..10>! apply other parameters to given logical channel");
      break;
  }
}

void loop() {
  dbg(clido); // process incoming keystrokes

  if (Ticker::check()) { // read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    my.onTick(Ticker::now);
  }
  my.loop();

  //  if (Serial.available()) {
  //    auto key = Serial.read();
  //    Serial.printf("Command %c [%d]\n", char(key), int(key) );
  //    switch (key) {
  //      case 13:
  //        Serial.printf("posting %u\n", my.report.sequenceNumber);
  //        my.post();
  //        break;
  //
  //      case '.':
  //        my.shouldSend = true;
  //        break;
  //      case '=':
  //        my.dumpHex(my.toSend.outgoing(), Serial);
  //        break;
  //      case '!':
  //        TRACE = true;
  //        break;
  //      case '?':
  //        my.forceMode(INPUT_PULLUP);
  //        break;
  //    }
  //  }
}
