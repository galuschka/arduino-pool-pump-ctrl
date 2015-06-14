#ifndef Relay_h
#define Relay_h

#include <Arduino.h>
#include "display.h"

class Relay
{
  private:
    byte      pin;
    byte      infonum; // num to use, when calling display->info()
    byte      on;      // 1=on 0=off
    byte      autoon;  // mode, if automatic mode is active
    byte      swmode;  // switch mode (SW_ON, SW_OFF, SW_AUTO, SW_TEMP)
    byte      prev;    // previous mode (SW_ON, SW_OFF, SW_AUTO)
    short     timeout; // timeout value in minutes, when temporary switching on
    Ctrl    * ctrl;    // we need to know dusk and dawn time to collect total running time

    unsigned long tempStop;   // millis(), when "temporary on" times out
    unsigned long switched;   // millis(), when we did turn on or off last time
    unsigned long run;        // milli seconds running last time
    unsigned long paused;     // milli seconds paused last time
    unsigned long todayOn;    // total millis(), we run from refSec (excl. running())
    unsigned long totalOn;    // total secs running until yesterday (excl. running()+todayOn())
    unsigned long refSec;     // either dusk or dawn, when todayOn time starts
    unsigned long secEnter;   // ctrl->sec, when we did enter the menu item "adjust timeout"

    void      turn( byte on ); // really turn on/off

  public:
    Relay( byte pin );
    void    init(void); // init PIN mode and switch off
    void    setup( Ctrl * ctrl, byte infonum );
    void    secLoop(void);

    unsigned long pausing();  // millis idle (0, when on)
    unsigned long running();  // millis on (0, when off)
    unsigned long before();   // millis on today before last switch
    unsigned long today();    // total seconds running today (pump: since dawn / lamp: since dusk)
    unsigned long total();    // total seconds running since boot up

    void    night( byte isNight );  // currently becoming night or day

    void    swMode( byte swmode );  // manual switching on/off/auto
    void    autoOn( byte autoon );  // automatic switching on/off
    byte    isOn() { return on; };  // fast detect running or not

    int     backup( int addr );     // in: start address behind length / return: end address + 1
    void    restore( int addr, uint8_t len );

    char  * show( char * buf, byte menuitem, byte init, const char * name );
};

#endif

