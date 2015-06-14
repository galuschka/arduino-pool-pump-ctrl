#ifndef Switch_h
#define Switch_h

#include <Arduino.h>
#include "ctrl.h"
#include "display.h"

class Switch
{
  public:
    enum MODE {
       OFF  = 0
      ,ON   = 1
      ,AUTO = 2
      ,TEMP = 3
    };
    enum NOTE {
       NOTE_NOTHING = 0
      ,NOTE_MENU    = 1
      ,NOTE_KEY     = 2
      ,NOTE_SWMODE  = 3
      ,NOTE_TIMEOUT = 4  // restart timeout
    };
  private:
    enum TOGGLE_COUNT {
       TOGGLE_OFF  = 2  // to press 2 times to switch off
      ,TOGGLE_AUTO = 3  // to press 3 times to set automatic mode
      ,TOGGLE_TEMP = 4  // to press 4 times to temporary switch on (back to previous state after 1 min)
      ,TOGGLE_ON   = 5  // to press 5 times to switch on

      ,TOGGLE_MIN  = 2
      ,TOGGLE_MAX  = 5
    };

    byte      pin;         // pin to look for
    byte      infonum;     // num to use, when calling display->info()
    byte      chatter;     // chatter detect
    byte      phyStatus;   // physical status (even chatter)
    byte      clnStatus;   // clean status (chatter purged)
    byte      logStatus;   // logical status (AUTO, ...)
    byte      toggleCnt;   // number of High->Low transits (restarts, when idle > 1sec)
    byte      menuMode;    // menu mode, when key has been pressed (ignore release when changed)
    long      lastChatter; // millis(), when phyStatus toggled last time (chatter or not)
    long      lastToggle;  // millis(), when phyStatus toggled last time (not chatter)
    Switch  * other;       // both pressed? -> enter/exit menu
    Ctrl    * ctrl;        // all other globals
#ifdef KEYPAD
    short     keymin;      // threshold to detect "key pressed"
    short     keymax;      // threshold to detect "key pressed"
#endif

  public:
    Switch( byte pin );
    void    init(void);                      // init PIN mode
    void    setup( Ctrl * ctrl, byte num );  // num is Display::NUM_... to set relay and other
    byte    loop(void);

    boolean idle(void);        // return false, when pressed or chatter detected (= "not idle")
    byte    mode(void);        // logStatus

  private:
    byte    toggleDetect( byte val );  // called after chatter gone
};

#endif

