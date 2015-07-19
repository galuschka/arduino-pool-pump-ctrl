#include <EEPROM.h>
#include <inttypes.h>
#include <avr/wdt.h>

#include "ctrl.h"
#include "display.h"    // LCD wrapper (info/menu/duplicate content on serial output)
#include "relay.h"      // relay control (on/off and duration, "total on since ...")
#include "lumi.h"       // luminance ctrl (dusk, dawn, backup, restore, ...)
#include "temp.h"       // temperature ctrl (night, backup, restore, ...)

Ctrl::Ctrl( Display * displayArg,
            Switch  * pumpSwitchArg,
            Switch  * lampSwitchArg,
            Relay   * pumpRelayArg,
            Relay   * lampRelayArg,
            Lumi    * lumiArg,
            Temp    * tempArg )
        : display(    displayArg )
        , pumpSwitch( pumpSwitchArg )
        , lampSwitch( lampSwitchArg )
        , pumpRelay(  pumpRelayArg )
        , lampRelay(  lampRelayArg )
        , lumi(       lumiArg )
        , temp(       tempArg )
        , sec(        -1 )  // 0 in 1st loop !
        , totalOn(    0 )
        , todayOn(    0 )
{}

#ifdef DEBUG
void Ctrl::minLoop()
{
  unsigned long m = (sec / 60);
  unsigned long h = (m / 60);  m %= 60;

  Serial.print( " alive: " );
  const char * unit = " h:mm";
  if (h >= 24) {
    Serial.print( h / 24 );    h %= 24;
    Serial.print( "," );
    if (h < 10)
      Serial.print( "0" );
    unit = " d,hh:mm";
  }
  Serial.print( h );
  Serial.print( ":" );
  if (m < 10)
    Serial.print( "0" );
  Serial.print( m );
  Serial.println( unit );
}
#endif

void Ctrl::backup( byte whence )
{
  if (whence == Lumi::NOCHANGE)
    return;

  if (whence != Lumi::MANUAL)
    temp->night( whence == Lumi::DUSK );  // explicit autoOn and save min/max

  int addr = 0; // current EEPROM address
  int aLen;     // address, where to store length of sub structure

  addr = save( addr, (uint8_t) EE_FORMAT );  // to check content

  addr = save( addr, (uint8_t) EE_TYPE_RSVD );
  addr = save( addr, (uint8_t) 13 );  // 13 bytes reserved
  addr += 13;

  addr = save( addr, (uint8_t) EE_TYPE_CTRL );
  aLen = addr++;
  addr = save( addr, (uint32_t) (totalOn + sec) );
  addr = save( addr, (uint32_t) (sec - lumi->dawn()) );  // todayOn
  do addr = save( addr, (uint8_t) 0 ); while (addr & 3);
  save( aLen, (uint8_t) (addr - (aLen + 1)) );
  wdt_reset();

  addr = save( addr, (uint8_t) EE_TYPE_PUMP );
  aLen = addr;
  addr = pumpRelay->backup( addr + 1 );
  do addr = save( addr, (uint8_t) 0 ); while (addr & 3);
  save( aLen, (uint8_t) (addr - (aLen + 1)) );
  wdt_reset();

  addr = save( addr, (uint8_t) EE_TYPE_LAMP );
  aLen = addr;
  addr = lampRelay->backup( addr + 1 );
  do addr = save( addr, (uint8_t) 0 ); while (addr & 3);
  save( aLen, (uint8_t) (addr - (aLen + 1)) );
  wdt_reset();

  addr = save( addr, (uint8_t) EE_TYPE_LUMI );
  aLen = addr;
  addr = lumi->backup( addr + 1 );
  do addr = save( addr, (uint8_t) 0 ); while (addr & 3);
  save( aLen, (uint8_t) (addr - (aLen + 1)) );
  wdt_reset();

  addr = save( addr, (uint8_t) EE_TYPE_TEMP );
  aLen = addr;
  addr = temp->backup( addr + 1 );
  do addr = save( addr, (uint8_t) 0 ); while (addr & 3);
  save( aLen, (uint8_t) (addr - (aLen + 1)) );
  wdt_reset();

  save( addr, (uint8_t) EE_TYPE_END );
}

void Ctrl::restore( void )
{
#if 0
#define DEBUG(x,y,z)  display->printat( x, y, z );
#define DBGLN(x,y,z)  display->printat( x, y, z );
#else
#define DEBUG(x,y,z)
#define DBGLN(x,y,z)
#endif

  DEBUG( 0, 0, "restore from EEPROM content: " )
  if (EEPROM.read( 0 ) != EE_FORMAT) {
    DEBUG(  0, 1, "unknown format " )
    DBGLN( 15, 1, (int) EEPROM.read( 0 ) )
    return;
  }

  int addr = 1;
  do // while (addr < 1000)
  {
    uint8_t const type = EEPROM.read( addr );
    uint8_t const len  = EEPROM.read( addr + 1 );
    addr += 2;
    switch (type)
    {
      case EE_TYPE_CTRL:
        if (len >= 4) {
          totalOn = read4( addr );
          if (len >= 8)
            todayOn = read4( addr + 4 );
        }
        DEBUG(  0, 0, "addr:           " )
        DEBUG(  6, 0, (int) addr )
        DEBUG(  8, 0, "type: " )
        DEBUG( 14, 0, (int) type )
        DEBUG(  0, 1, "len:            " )
        DEBUG(  5, 1, (int) len )
        DEBUG(  7, 1, "totalOn: " )
        DEBUG( 10, 1,  totalOn )
        DEBUG(  7, 1, "todayOn: " )
        DBGLN( 10, 1,  todayOn )
        break;

      case EE_TYPE_PUMP:
        pumpRelay->restore( addr, len );
        ///DBGLN(  7, 1, "pump relay" )
        break;

      case EE_TYPE_LAMP:
        lampRelay->restore( addr, len );
        ///DBGLN(  7, 1, "lamp relay" )
        break;

      case EE_TYPE_LUMI:
        lumi->restore( addr, len );
        ///DBGLN(  7, 1, "luminance values" )
        break;

      case EE_TYPE_TEMP:
        temp->restore( addr, len );
        ///DBGLN(  7, 1, "temperature values" )
        break;

      case EE_TYPE_END:
        ///DBGLN(  7, 1, "end of data" )
        return;

      case EE_TYPE_RSVD:
        break;

      default:
        DEBUG(  0, 0, "addr:           " )
        DEBUG(  6, 0, (int) addr )
        DEBUG(  8, 0, "type: " )
        DEBUG( 14, 0, (int) type )
        DEBUG(  0, 1, "len:            " )
        DEBUG(  5, 1, (int) len )
        DBGLN(  7, 1, "unknown type" )
        break;
    }
    addr += len;
  }
  while (addr < 1000);

  DBGLN(  7, 1, "address out of space" )
}


int Ctrl::save( int addr, const uint8_t * data, uint8_t len )
{
  do {
    uint8_t reg = EEPROM.read( addr );
    if (reg != *data)
      EEPROM.write( addr, *data );
    ++addr;
    ++data;
  } while (--len);

  return addr;
}

int Ctrl::save( int addr, uint8_t val )
{
  return save( addr, (const uint8_t *) & val, 1 );
}

int Ctrl::save( int addr, uint16_t val )
{
  return save( addr, (const uint8_t *) & val, 2 );
}

int Ctrl::save( int addr, uint32_t val )
{
  return save( addr, (const uint8_t *) & val, 4 );
}


int Ctrl::readN( int addr, uint8_t * data, uint8_t len )
{
  do
    *data++ = EEPROM.read( addr++ );
  while (--len);
  return addr;
}

char Ctrl::read1( int addr )
{
  return EEPROM.read( addr );
}

short Ctrl::read2( int addr )
{
  int16_t x = 0;
  readN( addr, (uint8_t *) & x, 2 );
  return x;
}

long Ctrl::read4( int addr )
{
  int32_t x = 0;
  readN( addr, (uint8_t *) & x, 4 );
  return x;
}


const char * Ctrl::show( char * buf, byte menuitem, byte init )
{
  memset( buf + 1, ' ', 33 );
  buf[   0] = '|';
  buf[0x11] = '|';
  buf[0x22] = '|';
  buf[0x23] = 0;

  switch (menuitem)
  {
    case 1:
      memcpy( buf +    1, "Betrieb: ", 9 );
      Display::dhms( buf +   10, sec );
      memcpy( buf + 0x12, "gesamt:  ", 9 );
      Display::dhms( buf + 0x1b, sec + totalOn );
      break;

    case 2:
      if (! init)
        return 0;
      memcpy( buf +    1, "backup starten ?", 16 );
      memcpy( buf + 0x12, "(gelb:nein/b:ja)", 16 );
      break;

    case 3:
      if (! init)
        return 0;
#if 0
                            // 01234567     8      9abcdef
      display->printat( 0, 0, "backup l" STR_AUML "uft ..." );
      display->printat( 0, 1, "        "   " "    "       " );
#endif
      backup( Lumi::MANUAL );
      memcpy( buf +    1, "backup", 6 );
      memcpy( buf + 0x12, "ausgef" STR_UUML "hrt", 10 );
      break;

    default:
      return 0;
  }

  return buf;
}
