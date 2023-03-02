/**
control timing of drawer kicker for Orkules puzzle.

1) activate drawer latch (HW note: is not polarity sensitive, can use half-H driver)
2) wait enough time for it to retract 
3) activate kicker
4) wait long enough for it to fully extend. A little extra time is not harmful but don't leave it on for more than a second.
5) reverse kicker direction. Break before make etc are not required, the possible intermediate states are just fine with this driver (H-bridge with free wheel and brake as the transitional states)
6) wait long enough for it to retract
7) turn driver off (00 state on pins).

a) drive LED while the trigger is active.

TBD: turn latch off after some period of time rather than following the trigger level.

TBD: if trigger goes away while sequence is in progress do we maintain the latch active until the sequence completes?

*/


/**
field configurablity (EEPROM contents):
The three timing parameters in milliseconds.
[milliseconds],U  time between unlock and start kick
[milliseconds],K  time between start kick and end kick
[milliseconds],O  time between end kick and power off


If cheap enough:
pin choices and polarities

[pin number],[0|1],L  latch
[pin number],[0|1],F  kick out
[pin number],[0|1],B  retract


preserve settings:
Z

list settings and state
[space]

--------------------------
Testing conveniences


Trigger sequence
T

Test light
Q,q

Test kicker
<,>

All off
x

*/


void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}
