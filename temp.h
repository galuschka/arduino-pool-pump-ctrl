#ifndef Temp_h
#define Temp_h

#include <OneWire.h>
#include <inttypes.h>
#include "ctrl.h"
#include "relay.h"
#include "switch.h"
#include "display.h"

class Temp
{
  public:
    enum SENSOR {  // values for num arg in info
      SENSOR_SOL  = 0
     ,SENSOR_POOL
     ,SENSOR_INS
     ,SENSOR_AIR
     ,SENSOR_BOX

     ,SENSOR_COUNT
    };

  private:
    enum PERIOD {
      PERIOD_DAY  = 0
     ,PERIOD_WEEK
     ,PERIOD_MONTH
     ,PERIOD_YEAR
     ,PERIOD_OVERALL

     ,PERIOD_COUNT
    };
    enum RESULT {  // return values for loop
      NO_ACTION  = 0
     ,RES_ERROR
     ,NOT_COMPLETE

     ,STILL_NIGHT // no change because "still night"
     ,STAY_TIME   // no change because max. or min. time
     ,STAY_TEMP   // no change because threshold not reached
     ,STAY_COLD   // no change even after 2h: solar colder than pool

     ,CAUSE_NIGHT // dusk caused switch off
     ,CAUSE_TIME  // time limits caused a change
     ,CAUSE_TEMP  // temperature values caused a change
    };
    enum EEPROM_CONST {
      TEMP_FORMAT // we might want to change eeprom layout of temperature values only
    };

    struct mem {
      char const * name;
      byte const * addr;
      long         conv;  // conversion delay (set, when addr is ok)
      short        res;   // resolution mask (set on first data read)
      short        temp;  // last read temperature value
      short        avg;   // = (sum + 4) >> 3
      short        sum;   // sum of 8 values for low pass filter
      int16_t      min[PERIOD_COUNT]; // minimum avg of this day/week/month/year/overall
      int16_t      max[PERIOD_COUNT]; // maximum avg of this day/week/month/year/overall
    };

    byte index;      // device under test
    byte state;      // read status (0xe: counter 0x1: conv)
    byte devok;      // bit mask of successfully read temperature
    byte autoon;     // last value, when called relay->autoOn

    mem  t[SENSOR_COUNT];     // config and read/calc. values of the sensors
    byte displayNum[SENSOR_COUNT];     // Display::NUM of each sensor
    byte sensorNum[Display::NUM_TEMP]; // Sensor::NUM of each temp. display number

 // settings:
    byte          shiftPausing;
    byte          shiftB4Start;
    byte          shiftRunning;
    byte          shiftB4Stop;

    unsigned long secEnter;  // ctrl->sec, when we did enter the menu item "adjust shift..."

 // to debug:
    byte          res;        // result of finalize (show in next loop)
    unsigned long time;       // time run / time paused
    unsigned long before;     // time run today before last switch on
    short         diff;       // temperature difference
    short         threshold;  // threshold to switch

 // loop ctrl:
    long      usecNextaction; // micros, when to perform next action
    long      usecNextstart;  // micros, when to start next read

    OneWire * ow;
    Ctrl    * ctrl;


    void         check( mem * m );    // check addr
    char const * act(void);           // perform next action
    char const * conv(void);          // start conversion
    char const * data(void);          // read data
    boolean      next( boolean restart = false ); // increase index to next having conv
    byte         finalize(void);      // temperatures read - calculate pump switching
    void         restart(void);       // set index to 1st having conv or SENSOR_COUNT
    int8_t       tocelsius(short raw);// 0..07ff -> 0..7f / ?800..?fff -> 80..ff

  public:
    Temp();
    void setup( Ctrl * ctrl, OneWire * ow );
    void loop(void);

    void night( byte isNight );  // currently becoming night or day

    short raw( byte sensorIdx ) { return t[sensorIdx].temp; }
    short avg( byte sensorIdx ) { return t[sensorIdx].avg; }

    int    backup( int addr );        // in: start address behind length / return: end address + 1
    void   restore( int addr, uint8_t len );

    char * showThres( char * buf, byte menuitem, byte init );
    char * showValue( char * buf, byte menuitem, byte num );
};

#endif
