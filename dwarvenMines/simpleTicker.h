#pragma once
///////////////////////////////////////////////////////////////////////
//minimal version of ticker service, will add full one later:

using MilliTick = decltype(millis());

struct Ticker {
  static constexpr unsigned perSecond = 1000;
  static constexpr MilliTick PerSeconds(unsigned seconds) {
    return seconds * perSecond;
  }
  static constexpr MilliTick PerMinutes(unsigned minutes) {
    return PerSeconds(minutes * 60);
  }
  ////////////////////////////////
  // constants and snapshot/cache, reading millis more than once per loop is bad form, creates ambiguities.
  static const MilliTick Never = ~0u;
  static MilliTick now; //cached/shared sampling of millis()

  //this is usually called from loop() and if it returns true then call "onTick" routines.
  static bool check() {
    auto sample = millis();
    if (now != sample) {
      now = sample;
      return true;
    }
    return false;
  }

  //////////////////////////////////
  // each timer:
  MilliTick due = Never;


  /** @returns whether the timer was actually running and has stopped, but clears the timer memory so you must act upon this being true when you read it. */
  bool done() {
    bool isDone = due <= now;
    if (isDone) {
      due = Never; //run only once per 'next' call.
    }
    //this is inlined version so I am leaving out refinements such as check for a valid  'due' value.
    return isDone;
  }

  /** @returns whether the timer has been stopped, which is not the same as !isRunning as it might also be done. */
  bool isStopped() const {
    return due == Never;
  }

  /** @returns whether timer is running, returns false if done and that can get confusing, so you should look at @see isStopped() as that might be what you are actually thinking of */
  bool isRunning() const {
    return (due != Never) && (due > now);
  }

  /** @returns whether the 'future' expiration time is in the past due to wrapping the ticker counter. The program has to run for 49 days for that to occur.
    When not running this starts the timer, when already running this extends the time  */
  bool next(MilliTick later) {
    if (later == Never) {
      stop();
      return false;
    }
    due = later + now;
    return due < now; //timer service wrapped.
  }

  void stop() {
    due = Never;
  }

  unsigned remaining() const {
    if (due != Never) {
      return due - now;
    }
    return ~0;
  }
};

MilliTick Ticker::now = 0;//until first actual read.
