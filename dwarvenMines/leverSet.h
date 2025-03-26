//an Arduino fragment, include in your .ino, no matching .cpp exists at present
//expect dbg.cout ChainPrinter for debug messages.
//todo: remove dependency on dbg.cout, pass in a debug channel.

#include "simpleDebouncedPin.h"

struct LeverSet: Printable {
    /**
       Each lever
    */
    struct Lever: Printable {
      // latched version of "presently"
      bool solved = 0;
      // debounced input
      DebouncedInput presently;

      unsigned pinNumber() const {
        return presently.pin.number;
      }
      // check up on bouncing.
      bool onTick(MilliTick now) { // implements latched edge detection
        if (presently.onTick(now)) {   // if just became stable
          solved |= presently;
          return true;
        }
        return false;
      }

      // restart puzzle. If a lever sticks we MUST fix it. We cannot fake one.
      void restart() {
        solved = presently;
      }

      void setup(MilliTick bouncer) {
        presently.filter(bouncer);
      }

      Lever(unsigned pinNumber) : presently{pinNumber, false} {}

      size_t printTo(Print& p) const override {
        return p.print(solved ? "ON" : "off");
      }
    };//// end Lever class

    std::array<Lever, numStations> lever; // using std::array over traditional array to get initializer syntax that we can type

  public:
    enum class Event {
      NonePulled,  // none on
      FirstPulled, // some pulled when none were pulled
      SomePulled,  // nothing special, but not all off
      LastPulled,  // all on
    };

    Event onTick(MilliTick now) {
      // update, and note major events
      unsigned prior = numSolved();
      for (unsigned index = numStations; index-- > 0;) {
        bool changed = lever[index].onTick(now);
        if (changed && clistate.leverIndex == index) {
          Serial.printf("lever[%u] just became: %x,  latched: %x\n", index, lever[index].presently, lever[index].solved);
        }
      }

      unsigned someNow = numSolved();
      if (someNow == prior) { // no substantial change
        return someNow ? Event::SomePulled : Event::NonePulled;
      }
      // something significant changed
      if (someNow == numStations) {
        return Event::LastPulled; // takes priority over FirstPulled when simultaneous
      }
      if (prior == 0) {
        return Event::FirstPulled;
      }
      return Event::SomePulled; // a different number but nothing special.
    }

    bool& operator[](unsigned index) {
      return lever[index].solved;
    }

    void restart() {
      Serial.println("Lever::Restart");
      ForStations(index) {
        lever[index].restart();
      }
    }

    unsigned numSolved() const {
      unsigned sum = 0;
      ForStations(index) {
        //        if (lever[index].solved) {
        //          ++sum;
        //        }
        sum += lever[index].solved;
      }
      return sum;
    }

    void listPins(Print &stream) const {
      stream.println("Lever logical pin assignments");
      ForStations(index) {
        stream.printf("\t%u:D%u", index, lever[index].pinNumber());
      }
      stream.println();
    }

    void setup(MilliTick bouncer) {
      ForStations(index) {
        lever[index].presently.filter(bouncer);
      }
    }

    LeverSet() : lever{16, 17, 5, 18, 19, 21} {}//SET PIN ASSIGNMENTS FOR LEVERS HERE

    size_t printTo(Print& stream) const override {
      size_t length = 0;
      length += stream.print("Levers:");
      ForStations(index) {
        length += stream.print("\t");
        length += stream.print(index);
        length += stream.print(": ");
        length += stream.print(lever[index]);
      }
      length += stream.println();
      return length;
    }
};
