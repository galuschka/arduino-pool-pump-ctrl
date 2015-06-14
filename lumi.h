#ifndef Lumi_h
#define Lumi_h

#include <Arduino.h>
#include "ctrl.h"

class Lumi
{
  public:
    enum WHENCE {  // return value of secLoop() and whence arg of Ctrl::backup()
      NOCHANGE = 0
     ,DUSK
     ,DAWN
     ,MANUAL       // to perform manual backup
    };
  private:
    byte          pin;
    byte          status;    // 1: is night | 2: detect deep night/light day
    byte          cntDetect; // how often did we detect sunrise/sunset in sequence (low pass filter)
    byte          _rsvd;

    word          lumSwitch; // luminance to switch on
    word          lumDawn;   // luminance to detect sunset/sunrise
    word          lumNight;  // luminance to detect deep night (start dawn detection)
    word          lumDay;    // luminance to detect light day (start dusk detection)
    word          lumCurr;   // last read luminance value

    short         secCorr;   // correction to calculate midnight (we assume DST)
      signed long timeOff;   // daytime, when to switch off (negative: before astron. midnight)

    unsigned long secOff;    // ctrl->sec, when to switch off today (calculated, when switched on)
    unsigned long dayLight;  // length of day light 0..86400

    unsigned long midnight;  // calculated sec counter value of next midnight
    unsigned long secDusk;   // sunset time (secs counter)
    unsigned long secDawn;   // sunrise time (secs counter) (==> midnight somewhere at (secDusk + secDawn) / 2)

    unsigned long secEnter;  // ctrl->sec, when we did enter the menu item "adjust timeOff/secCorr"

    Ctrl        * ctrl;

  public:
    Lumi( byte pin );  // analog pin!
    void    setup( Ctrl * ctrl );
    byte    secLoop(void);      // read luminance and return true on dusk and dawn

    boolean       night() { return status & 1; };
    unsigned long dusk()  { return secDusk; };
    unsigned long dawn()  { return secDawn; };

    int     backup( int addr );      // in: start address behind length / return: end address + 1
    void    restore( int addr, uint8_t len );

    char  * showTime( char * buf, byte menuitem, byte init );  // time as calculated by dusk and dawn
    char  * showDown( char * buf, byte menuitem, byte init );  // down = dusk+dawn ;-)
};

#endif

