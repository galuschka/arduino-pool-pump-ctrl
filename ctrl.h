#ifndef Ctrl_h
#define Ctrl_h

#include <Arduino.h>
#include <inttypes.h>

//define KEYPAD  // enable this switch, when using keypad shield
#define  NDEBUG  // DEBUG or NDEBUG

#ifdef DEBUG
#define TEMP_DEBUG  // TEMP_DEBUG or TEMP_NDEBUG: action cause before/time/thres/diff
#define DEBUG_EXPR( expr ) expr;
#else
#define TEMP_NDEBUG
#define DEBUG_EXPR( expr )
#endif

enum PIN
{
  // analog pins:
    PIN_Luminance   =  0  // where to read luminance

  // digital pins:
   ,PIN_OneWire     =  2  // OneWire-Bus

#ifdef KEYPAD             // using SainSmart LCD Keypad Shield
   ,PIN_PumpSwitch  = 10  // Schalter "Filter-Pumpe"
   ,PIN_LampSwitch  = 11  // Schalter "Licht"
   ,PIN_PumpRelay   = 12  // Relais "Filter-Pumpe"
   ,PIN_LampRelay   = 13  // Relais "Licht"

   ,PIN_LCD_DB4     =  4  //
   ,PIN_LCD_DB5     =  5  //
   ,PIN_LCD_DB6     =  6  //
   ,PIN_LCD_DB7     =  7  //
   ,PIN_LCD_RS      =  8  // register select
   ,PIN_LCD_Ena     =  9  // enable
#else
   ,PIN_PumpSwitch  =  3  // Schalter "Filter-Pumpe"
   ,PIN_LampSwitch  =  4  // Schalter "Licht"
   ,PIN_PumpRelay   =  5  // Relais "Filter-Pumpe"
   ,PIN_LampRelay   =  6  // Relais "Licht"

   ,PIN_LCD_DB4     =  7  // pin 11 of DEM16217
   ,PIN_LCD_DB5     =  8  // pin 12 of DEM16217
   ,PIN_LCD_DB6     =  9  // pin 13 of DEM16217
   ,PIN_LCD_DB7     = 10  // pin 14 of DEM16217
   ,PIN_LCD_RS      = 11  // pin  4 of DEM16217
   ,PIN_LCD_RW      = 13  // pin  5 of DEM16217
   ,PIN_LCD_Ena     = 12  // pin  6 of DEM16217
#endif
};

#define TempDevAddrSol   0x28, 0xE0, 0xBC, 0x08, 0x06, 0x00, 0x00, 0x94
#define TempDevAddrIns   0x28, 0x15, 0x9E, 0xE7, 0x05, 0x00, 0x00, 0x41
#define TempDevAddrPool  0x28, 0x43, 0xBD, 0x0A, 0x06, 0x00, 0x00, 0x5B
#define TempDevAddrAir   0x28, 0xC5, 0x28, 0xE0, 0x05, 0x00, 0x00, 0xD0
#define TempDevAddrBox   0x28, 0x9F, 0x31, 0xE0, 0x05, 0x00, 0x00, 0xBE

#define NELEMENTS(i) (sizeof(i)/sizeof(i[0]))

#define CHR_DEGREE   0337
#define STR_DEGREE  "\337"  // 0xdf
#define STR_AUML    "\341"  // 'a'=0x61 -> 'ae'=0xe1
#define STR_OUML    "\357"  // 'o'=0x6f -> 'oe'=0xef
#define STR_UUML    "\365"  // 'u'=0x75 -> 'ue'=0xf5
#define STR_SZLIG   "\363"  // 's'=0x73 -> 'ss'=0xf3

#define _k  * 1000L
#define _M  * 1000L _k
#define _G  * 1000L _M


class Display;
class Switch;
class Relay;
class Lumi;
class Temp;

class Ctrl
{
  public:
    Display     * display;
    Switch      * pumpSwitch, * lampSwitch;
    Relay       * pumpRelay,  * lampRelay;
    Lumi        * lumi;
    Temp        * temp;
    unsigned long sec;      // second counter from startup
    unsigned long totalOn;  // about sum of second counter of last runs (incl. todayOn)
    unsigned long todayOn;  // to calculate percentage value of "relais run today"
#ifdef KEYPAD
    int           keypad;   // analog value of keypad resistor status
#endif
  private:
    enum {
      EE_FORMAT = 1

     ,EE_TYPE_RSVD = 0
     ,EE_TYPE_CTRL
     ,EE_TYPE_PUMP
     ,EE_TYPE_LAMP
     ,EE_TYPE_LUMI
     ,EE_TYPE_TEMP

     ,EE_TYPE_COUNT
     ,EE_TYPE_END = 0xff
    };

  public:
    Ctrl( Display * display
         ,Switch  * pumpSwitch
         ,Switch  * lampSwitch
         ,Relay   * pumpRelay
         ,Relay   * lampRelay
         ,Lumi    * lumi
         ,Temp    * temp
        );

    void         minLoop( void );       // called every full minute
    void         backup( byte whence ); // called with retval of lumi::secLoop and manual by menu: Ctrl::show()
    void         restore( void );  // called once at startup (end of setup())

    static int    save( int addr, uint8_t const * data, uint8_t len = 1 );
    static int    save( int addr, uint8_t         val );
    static int    save( int addr, uint16_t        val );
    static int    save( int addr, uint32_t        val );

    static int   readN( int addr, uint8_t       * data, uint8_t len = 1 );
    static char  read1( int addr );
    static short read2( int addr );
    static long  read4( int addr );

    const char * show( char * buf, byte menuitem, byte init );
};

#endif
