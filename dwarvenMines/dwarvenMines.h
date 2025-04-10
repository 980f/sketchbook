#include "Arduino.h" //couldn't figure out Serial0
/*
  build and debug for QN2025 vortex special effects.

  post mortem:
  leverset needs to be reworked to accept pushed from multiple sources:gpio,remoteGpio,debug debouncing ditto, do not use i/o debouncer for delayed reaction stuff, use resettable monostable.

  punch list:
  done: "off" is small set of varied lights for illumination
  done: sets of Patterns and use pattern for lever feedback, and test selector for pattern in clirp. configurable values to and from EEPROM, or store a string to run through the clirp.

  done: vortex off: wait for run/reset change of state?
  done: mac diagnostic printout byte order
  don't: phase per ring of leds, an array of offsets => VortexLighting needs to implement 4 rings rather than one strand.
  done: and it was! [check whether a "wait for init" is actually needed in esp_now init]

  Note: 800 kbaud led update rate, 24 bits per LED, 33+k leds/second @400 LEDS 83 fps. 12 ms per update plus a little for the reset. 10Hz is enough for special FX work, 24Hz is movie theater rate.

  Node Messages:
  VOR1 to send lighting desires (@see vortexLighting.h)
  GPIO to send lever/other input physical state (@see remoteGPIO)

*/

#include "EEPROM.h"

#if defined(ARDUINO_LOLIN32_LITE)
#warning "Using pin assignments for 26pin w/battery interface"
#define BOARD_LED 22
// worker/remote pin allocations:
const unsigned LED_PIN = 13; // drives the chain of programmable LEDs
#elif defined(WAVESHARE_ESP32_S3_ZERO)
#warning "Using pin assignments for S3 Zero"
// there isn't actually an LED on 2, but  it is near the P/S pins.
#define BOARD_LED 2
// worker/remote pin allocations:
const unsigned LED_PIN = 21; // drives the chain of programmable LEDs
#elif defined(ARDUINO_NOLOGO_ESP32C3_SUPER_MINI)
#warning "Using pin assignments for C3 Super Mini"
#define BOARD_LED 8
// worker/remote pin allocations:
const unsigned LED_PIN = 3; // drives the chain of programmable LEDs
#else
// #warning "Using pin assignments that worked for WROOM"
#define BOARD_LED 2
// worker/remote pin allocations:
const unsigned LED_PIN = 13; // drives the chain of programmable LEDs
#endif

// #include "ezOTA.h" //setup wifi and allow for firmware updates over wifi
// EzOTA flasher;
// esp stuff pollutes the global namespace with macros for some asm language operations.
#undef cli

/////////////////////////
// debug cli
/// flags editable by cli:
const unsigned numSpams = 5;
bool spam[numSpams]; // 5 ranges, can add more and they are not actually prioritized
#define TRACE spam[4]
#define BUG3 spam[3]
#define BUG2 spam[2]
#define EVENT spam[1]
#define URGENT spam[0]

#include "simpleDebouncedPin.h"
#include "simplePin.h"
#include "simpleTicker.h"

// debug state
struct CliState {
  //  unsigned workerIndex = 0;
  unsigned leverIndex = ~0;  // enables diagnostic spew on selected lever
  unsigned patternIndex = 1; // 1 rainbow, 0 half ring.

  SimpleOutputPin onBoard{BOARD_LED}; // Wroom LED
  Ticker pulser;
  bool onTick() {
    if (pulser.done()) {
      pulser.next(onBoard.toggle() ? 2500 : 1500);
      return true;
    }
    // note: must test done() before isRunning() as it won't be indicating 'isRunning' if it just got done.
    if (!pulser.isRunning()) {
      onBoard << true;
      pulser.next(250);
      return true;
    }
    return false;
  }
} clistate;


#include "sui.h" //Simple User Interface

#include "boss.h"
#include "stripper.h"

//class DwarvenMiner {

SUI dbg(Serial, Serial);

SimpleInputPin IamBoss{15, true}; // pin to determine what this device's role is(Boss or worker) instead of comparing MAC id's and declare the MAC ids of each end.

Boss *boss = nullptr;
Stripper *worker = nullptr;
VortexCommon *agent = nullptr; // will be one of {boss|worker}

void getConfig() {
  BossConfig temp;
  EEPROM.begin(sizeof(temp)); // note: if worker needed its own config we would add its size here
  EEPROM.get(0, temp);
  if (temp.isValid()) {
    cfg = temp;
    Serial.println("Loaded configuration from EEPROM:");
    Serial.print(cfg);
  } else { // else object has defaults built in
    EEPROM.put(0, cfg);
    Serial.println("Initialized configuration in EEPROM:");
    Serial.print(cfg);
  }
}

void saveConfig(unsigned checkcode) {
  if (boss) {
    Serial.println("Present state of configuration:");
    Serial.print(cfg);
    if (checkcode == ~0) { // undo all changes
      getConfig();
    } else if (checkcode == cfg.checker) { // save config, whether changed or not
      EEPROM.put(0, cfg);
      EEPROM.commit(); // needed for flash backed paged eeproms to actually save the info.
      Serial.println("Saved configuration to virtual EEPROM");
    } else { // annoy user with incomplete information on how to save the config.
      Serial.println("You must enter the magic number in order to save the configuration.");
    }
  }
}

// test objects
VortexLighting::Message testwrapper;
VortexLighting::Command &tester{testwrapper.m};

//////////////////////////////////////////////////////////////////////////////////////////////
// debug aids

void showUpdateStatus() {
  Serial.printf("Update Allowed:%d\n", boss->updateAllowed);
  Serial.printf("Holdoff running:%d, Remaining:%d\n", boss->holdoff.isRunning(), boss->holdoff.remaining());
  Serial.printf("lastStation:%d, Bgnd.inProgress:%d \tUpdate flags", boss->lastStationSent, boss->backgrounder.inProgress);
  ForStations(si) {
    Serial.printf("\t[%u]=%x", si, boss->needsUpdate[si]);
  }
  Serial.println();
}

bool cliValidStation(unsigned param, const unsigned char key) {
  if (param < numStations) {
    return true;
  }
  dbg.cout("Invalid station : ", param, ", valid values are [0..", numStations - 1, "] for command ", char(key));
  return false;
}

void sendTest() {
  ++tester.sequenceNumber;
  tester.showem = true;
  if (tester.pattern.run == 0) {
    tester.setAll(tester.color);
  }
  tester.printTo(Serial);
  if (boss) {
    boss->applyLights(testwrapper);
  }
  if (worker) {
    worker->apply(testwrapper);//do lights and send notice to host
  }
}

void tweakColor(unsigned which, unsigned param) {
  tester.color[which] = param;
  Serial.printf("color= 0x%06X\n", tester.color.as_uint32_t());
  sendTest();
}

void clido(const unsigned char key, bool wasUpper, CLIRP<> &cli) {
  Serial.print(": ");
  unsigned param = cli[0]; // clears on read, can only access once!
  switch (key) {//still available: aehvy;[]
    case ':':
      saveConfig(param);
      break;

    case '/': // send tester
      tester.pattern.offset = param;
      sendTest();
      break;
    case '-': // run,period- sets,modulus/ offset k
      tester.pattern.run = param;
      if (cli.argc() > 1) {
        tester.pattern.period = cli[1];
        tester.pattern.makeViable();
      }
      sendTest();
      break;
    case '\\':
      tester.pattern.sets = param;
      if (cli.argc() > 1) {
        tester.pattern.modulus = cli[1];
      }
      sendTest();
      break;
    case '.':
      if (boss)
        switch (param) {
          case 0:
            Serial.printf("Refresh colors (lever) returned : %u", boss->refreshColors());
            break;
          case 1:
            ForStations(si) {//to get all colors posted
              boss->lever[si] = true;
            }
            boss->onSolution("CLI 1.");//but force the issue for testing of effect timing
            Serial.println("simulated solution");
            break;
          case ~0u:
            boss->resetPuzzle();
            Serial.println("Puzzle reset.");
            break;
          case 2:
            boss->lever.restart();
            Serial.printf("After simulated bombing out there are %d levers active\n", boss->lever.numSolved());
            break;
        }
      break;
    case '=':
      if (param) {
        agent->command.sequenceNumber = param;
      } else {
        ++agent->command.sequenceNumber;
      }
      if (boss) {
        Serial.printf("Sending sequenceNumber: %u\n", boss->command.sequenceNumber);
        boss->applyLights(boss->message);
      } else {
        worker->command.showem = true;
        worker->message.dataReceived = true;
      }
      break;
    case 'a':      
      cfg.sweep.Step = param ? param : 150; //how far to move stripe during drilling
      if (cli.argc() > 1) {
        cfg.sweep.Complete = cli[1];
      };
      break;
    case 'b':
      tweakColor(2, param);
      break;
    case 'g':
      tweakColor(1, param);
      break;
    case 'r':
      tweakColor(0, param);
      break;

    case 'c': // set a station color, volatile!
      if (boss) {
        if (cliValidStation(param, key)) {
          cfg.foreground[param] = tester.color;
          Serial.printf("station[%u] color is now 0x%06X\n", param, cfg.foreground[param].as_uint32_t());
        }
        if (param == ~0) {
          cfg.overhead.Lights = tester.color;
          boss->backgrounder.erase();
        }
      }
      break;

    case 'd': // debug flags, lower is off, upper is on
      switch (param) {
        case ~0u:
          LedStringer::spew = wasUpper ? &Serial : nullptr;
          for (auto flag : spam) {
            flag = wasUpper;
          }
          break;
        default:
          if (param < numSpams) {
            spam[param] = wasUpper;
          } else {
            Serial.printf("Known debug flags are 0..%u, or ~ for LedStringer\n", numSpams - 1);
          }
          break;
      }
      break;
    case 'f':
      if (boss) {
        /*boss->*/
        cfg.frameRate = param;
      }
      break;
    case 'j':
      if (boss) {
        showUpdateStatus();
      }
      break;
    case 'k':
      if (boss) {
        cfg.overhead.Width = param;
        if (cli.argc() > 1) {
          cfg.overhead.Start = cli[1];
        }
        boss->backgrounder.erase();
      }
      break;
    case 'l': // select a lever to monitor
      if (boss) {
        if (param == ~0u) {
          // available
        } else if (wasUpper) {
          Serial.printf("l2DR:%x\n", boss->levers2.dataReceived);
          BroadcastNode::dumpHex(boss->levers2.outgoing(), Serial);
        } else if (cliValidStation(param, key)) {
          clistate.leverIndex = param;
          Serial.printf("Selecting station %u lever for diagnostic tracing\n", clistate.leverIndex);
        }
      }

      if (worker) {
        // todo: list some raw pixel values
        Serial.printf("raw pixel data is at %p\n", worker->stringer.leds);
      }
      break;
    case 'm':
      Serial.println(WiFi.macAddress());
      Serial.printf("Boss is %p, worker is %p, agent is %p\n", boss, worker, agent);
      break;
    case 'n':
      BroadcastNode::spew = param > 1;
      BroadcastNode::errors = param > 0;
      Serial.printf("broadcast spam and errors have been set to %x,%x\n", BroadcastNode::spew, BroadcastNode::errors);
      break;
    case 'o':
      if (param < numRelays) {
        relay[param] << wasUpper; // don't use '=', it changed the pin assignment!
        Serial.printf("Relay %u set to %x\n", param, wasUpper);
      } else if (param == ~0u) {
        clistate.onBoard << wasUpper;
        Serial.printf("LED : %x\n", bool(clistate.onBoard));
      } else {
        Serial.printf("output %u not yet debuggable (or value is invalid)\n", param);
      }
      break;
    case 'p': // any pin made into an output!
      pinMode(param, OUTPUT);
      digitalWrite(param, wasUpper);
      break;
    case 'i':
      pinMode(param, wasUpper ? INPUT_PULLUP : INPUT);
      Serial.printf("Pin %u made an input and is %x\n", param, digitalRead(param));
      break;
    case 'q':
      for (unsigned i = 0; i < numRelays; ++i) {
        Serial.printf("relay[%u]:%x (D%u)\n", i, bool(relay[i]), relay[i].number);
      }
      break;
    case 's': // simulate a lever solution
      if (boss && cliValidStation(param, key)) {
        boss->lever[param] = true;
        unsigned after = boss->lever.numSolved();
        Serial.printf("Lever[ %d] latch triggered, reports: % x, %u now activated\n", param, boss->lever[param], after);
      }
      break;
    case 't':
      if (boss) {
        if (wasUpper) {
          cfg.fuseTicks = param;
          boss->timebomb.next(cfg.fuseTicks);
        }
        Serial.printf("Timebomb due: %u, in : %d, Now : %u\n", boss->timebomb.due, boss->timebomb.remaining(), Ticker::now);
      }
      break;
    case 'u': // unsolve
      if (boss && cliValidStation(param, key)) {
        clistate.leverIndex = param;
        boss->lever[clistate.leverIndex] = false;
        Serial.printf("Lever[%u] latch cleared, reports : %x\n", boss->lever[clistate.leverIndex]);
        Serial.printf("There are now %u activated\n", boss->lever.numSolved());
      }
      break;

    case 'w': // locally test a style via "all on"
      ++tester.sequenceNumber;
      if (boss) {
        switch (param) {
          case 0: //set all remote
            tester.setAll(tester.color);
            if (wasUpper) {//just local
              tester.showem = true;
              if (tester.pattern.run == 0) {
                tester.setAll(tester.color);
              }
              boss->VortexLighting::apply(tester);
              Serial.printf("Setting colors on local strand\n");
            } else {//remote and local
              testwrapper.tag[0] = 'D';
              testwrapper.tag[1] = 'W';
              boss->applyLights(testwrapper);
            }
            return;
          //            break;
          case 1:  //set all locally to "solved"
            Serial.printf("Setting LEDs via local connection\n");
            ForStations(si) {
              auto rgb = cfg.foreground[si];
              auto pat = boss->pattern(si, param);
              Serial.printf("\tstation %u, color %06X ", si, rgb.as_uint32_t());
              pat.printTo(Serial);
              boss->stringer.setPattern(rgb, pat);
            }
            boss->stringer.show();
            break;
          default:
            Serial.printf(" uppercase to set all locally, lowercase sends command to worker\n");
            break;
        }
      }

      if (worker) {
        Serial.printf("Setting the whole string to test color via direct access\n");
        worker->stringer.all(tester.color);
      }
      agent->stringer.show();
      break;
    case 'x':
      if (boss) {
        /*boss->*/ cfg.audioLeadinTicks = param;
      }
      break;
    case 'y':
      if (boss) {
        /*boss->*/ cfg.resetTicks = param;
      }
      break;
    case 'z': // set refresh rate, 0 kills it rather than spams.
      if (boss) {
        if (param == ~0) {
          cfg.refreshPeriod = Ticker::Never;
          boss->refreshLeds();
          Serial.printf("Refresh disabled but being invoked now\n");
        } else {
          cfg.refreshPeriod = param ? param : Ticker::Never;
          boss->refreshRate.next(cfg.refreshPeriod);
          if (cfg.refreshPeriod != Ticker::Never) {
            Serial.printf("refresh rate set to %u\n", cfg.refreshPeriod);
          } else {
            Serial.printf("refresh disabled\n");
          }
        }
      }
      break;

    case 26: // ctrl-Z
      Serial.println("Processor restart imminent, pausing long enough for this message to be sent \n");
      // todo: the following doesn't actually get executed before the reset!
      for (unsigned countdown = min(param, 4u); countdown-- > 0;) { // on 4u: std:max/min require identical arguments, should be rewritten to accept convertable arguments, convert second to first.
        delay(666);
        Serial.printf(" %u, \t", countdown);
      }
      ESP.restart();
      break;
    case ' ':
      if (boss) {
        Serial.println("\nVortexFX Boss:");
        boss->lever.printTo(Serial);

        showUpdateStatus();

        Serial.print("Background message:\t");
        boss->backgrounder.command.printTo(Serial);
        if (boss->echoAck.m.sequenceNumber != ~0) {
          Serial.print("Echoed message:\t");
          boss->echoAck.printTo(Serial);
        }
        Serial.print("Last Request: \t");
      } else {
        Serial.println("VortexFX Worker");
        Serial.print("Last Action: \t");
      }
      agent->command.printTo(Serial);
      break;
    case '*':
      if (boss) {
        boss->lever.listPins(Serial);
        Serial.print("Relay assignments : ");
        for (unsigned channel = numRelays; channel-- > 0;) {
          Serial.printf("\t % u : D % u", channel, relay[channel].number);
        }
        Serial.println();
        // add all other pins in use ...
      }
      break;
    case '?':
      Serial.printf("Program: %s ", __FILE__);
      Serial.printf("Wifi channel: %u\n", BroadcastNode_WIFI_CHANNEL);
      //      Serial.printf("usage : \n\tr, g, b: \talter pigment, %u(0x%2X) is
      //      bright\n\tl, s, u: \tlever trace / set / unset\n ",
      //      foreground.MAX_BRIGHTNESS , foreground.MAX_BRIGHTNESS );
      Serial.printf("\t [station]c: set color for a station from the one diddled by r,g,b\n");
      Serial.printf(
        "\t[gpio number]p: set given gpio number to an output and set it to 0 for lower case, 1 for upper case. VERY RISKY!\n");
      Serial.printf("\t[millis]z sets refresh rate in milliseconds, 0 or ~ get you 'Never'\n");
      Serial.printf("\t^Z restarts the program, param is seconds of delay\n");
      Serial.printf("Undocumented : *.=/-\\o[Enter]dtsuq\n");
      break;
    case '!':
      // flasher.setup();
      break;
    default:
      if (key >= ' ') {
        Serial.printf("Unassigned command letter %c [%u]\n", key, key);
      } else {
        Serial.printf("Most control characters are ignored: ^%c [%u]\n", key + ' ', key);
      }
      break;
  }
}

// arduino's setup:
void setup() {
  Serial.begin(460800); // use bootup baud rate, so we get ascii garbage when our code connects rather than binary garbage. Also 921600 was too fast for the raspberry pi 3B.

  //  SimpleInputPin doDebug(34, false);
  //  doDebug.setup();
  bool debugging = false; // couldn't find a reliable unused pin doDebug();
  dbg.cout.stifled = !debugging; // opposite sense of following bug flags
  TRACE = debugging;
  BUG3 = debugging;
  BUG2 = debugging;
  // default these on, unless we want to allocate another pin:
  EVENT = true;
  URGENT = true;

  //  flasher.setup();
  //  dbg.cout("OTA emabled for download but not yet for monitoring." );

  if (IamBoss) {
    Serial.println("\n\nSetting up as boss");
    getConfig();
    agent = boss = new Boss();
    boss->setup();
  } else {
    Serial.println("\n\nSetting up as remote worker");
    agent = worker = new Stripper();
    worker->setup();
  }

  Serial.println("Entering forever loop.");
}

// arduino's loop:
void loop() {
  //  flasher.loop();//OTA firmware or file update
  dbg(clido); // process incoming keystrokes
  // time paced logic
  if (Ticker::check()) { // read once per loop so that each user doesn't have to, and also so they all see the same tick even if the clock ticks while we are iterating over those users.
    if (boss) {
      boss->onTick(Ticker::now);
    }
    if (worker) {
      worker->onTick(Ticker::now);
    }
    clistate.onTick();
  }

  // check event flags
  if (boss) {
    boss->loop();
  }
  if (worker) {
    worker->loop();
  }
}
//};
