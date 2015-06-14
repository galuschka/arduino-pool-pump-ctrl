#ifndef Display_h
#define Display_h

#include <Arduino.h>
#include <LiquidCrystal.h>
#include "ctrl.h"


class Display
{
  public:
    enum NUM {  // values for num arg in info
      NUM_AIR = 0
     ,NUM_POOL
     ,NUM_SOL
     ,NUM_INS
     ,NUM_BOX

     ,NUM_LUM   // liminance / lightness
     ,NUM_LAMP  // off, on, auto (off), auto (on)
     ,NUM_PUMP  // off, on, auto (off), auto (on)

     ,NUM_COUNT
     ,NUM_TEMP = NUM_LUM
    };
    enum FLAGS {  // values for flags
      FLAG_ON           = 1
     ,FLAG_MENU         = 2  // menu mode - no info output, but normal output
     ,FLAG_INFO_CHANGED = 4  // infocont changed: show on serial
     ,FLAG_MENU_CHANGED = 8  // menucont changed: show on serial
    };

  private:
    long    timeout;        // to switch back to info or to switch off
    byte    flags;          // on/off, info/menu, ...
    byte    cursor;         // 0x20: row, 0x1f: col
    byte    menunum;        // menu status
    char    menucont[0x24]; // |...|...| 0..0f: 1st line, 11..20: 2nd line, 10,21: |
    char    infocont[0x24]; // |L:22° B:32° H:50|W:28° S:45° PABA|
    char    check[0x8];     // check overwrites
    char    hint[0x10];     // hint for change

    LiquidCrystal * lcd;
    Ctrl          * ctrl;

  public:
    Display();
    void setup( Ctrl * ctrl, LiquidCrystal * lcd );
    void secLoop(void);

    void refresh( byte init );  // init or refresh menu content
    void restart(void);         // turn LCD on for e.g. 15 minutes

    void    toggleMode(void);   // toggle menu/info mode
    boolean menu(void);         // true: we are in menu mode
    void    key( byte num );    // menu control with lamp and pump key

    static char * itoa(       char * buf, int bufsize, int digit );  // bufsize: incl. \0
    static char * itox(       char * buf, int bufsize, int digit );  // bufsize: incl. \0
    static char * hms(        char * buf,   signed long secs );  // print "hh:mm:ss"
    static char * dhms(       char * buf, unsigned long secs );  // print " x:yy h" or " y:zz m"
    static char * kelvin(     char * buf, int rawtemp );         // print " x,y" (note: might write '-' to buf[-1])
    static char * percentage( char * buf, unsigned long val, unsigned long max );  // print "xxx.yy %"

    void info( byte num, short val );

  private:
    void print( char const * str );
    void print( int digit );
    void printat( byte col, byte row, char const * str );
    void printat( byte col, byte row, int digit );

    void showcont( char const * cont );  // show on LCD
    void dumpcont(void);                 // show on serial
};

#endif
