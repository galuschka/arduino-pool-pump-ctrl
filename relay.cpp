#include "relay.h"
#include "display.h"
#include "switch.h"  // Switch::AUTO
#include "lumi.h"    // lumi->dusk(), lumi->dawn()


Relay::Relay( byte pinArg )
  : pin( pinArg )
  , on(       0 )
  , autoon(   0 )
  , swmode( Switch::AUTO )
  , prev(   Switch::AUTO )
  , timeout( 60 )  // 60 minutes: temporary switch on for 1 hour
  , switched( 0 )
  , run(      0 )
  , paused(   0 )
  , todayOn(  0 )
  , totalOn(  0 )
  , refSec(   0 )
{
}

void Relay::init(void) // init PIN mode and switch off
{
  digitalWrite( pin, HIGH );  // hopefully the relay stays open during bootup
  pinMode( pin, OUTPUT );
  digitalWrite( pin, HIGH );  // LOW active ==> HIGH to switch off
}

void Relay::setup( Ctrl * ctrlArg, byte infonumArg )
{
  ctrl    = ctrlArg;
  infonum = infonumArg;

  ctrl->display->info( infonum, swmode | (autoon << 2) | (on << 3) );
}

void Relay::secLoop(void)
{
  if (swmode == Switch::TEMP) {
    long diff = millis() - tempStop;
    if (diff >= 0)
      swMode( prev ); // same as if we would switch manual
  }
}

void Relay::swMode( byte swmodeArg )
{
  tempStop = millis() + ((long) timeout * (60 _k));  // restart timer: timeout after n minutes

  if (swmode == swmodeArg)
    return;  // no change

  byte newOn = 0;
  switch (swmodeArg)
  {
    case Switch::OFF:  newOn = 0;      break;
    case Switch::AUTO: newOn = autoon; break;  // whatever last autoOn call did
    case Switch::TEMP: newOn = 1;      break;  // initialy on - later whetever prev is
    case Switch::ON:   newOn = 1;      break;
    default:                           return;  // nothing to do on unknown mode
  }

  if ((swmode == Switch::OFF) || (swmode == Switch::AUTO))
    prev = swmode; // save last used OFF or AUTO mode (to what we have to switch back)

  swmode = swmodeArg;
  turn( newOn );
  ctrl->display->info( infonum, swmode | (autoon << 2) | (on << 3) );
}

void Relay::autoOn( byte autoonArg )  // automatic switching
{
  if (autoon == autoonArg)
    return;

  autoon = autoonArg;
  if (swmode == Switch::AUTO)
    turn( autoon );

  ctrl->display->info( infonum, swmode | (autoon << 2) | (on << 3) );
}

void Relay::night( byte isNight )
{
  if (infonum == Display::NUM_LAMP) {
    if (! isNight)  // -> dawn
      return;  // lamp: reference is last dusk
  } else {
    if (isNight)  // -> dusk
      return;  // pump: reference is last dawn
  }

  if (on) {
    unsigned long now = millis();
    unsigned long time = (now - switched);

    switched = now;  // we start extra period
    run = time;
    todayOn += time;
    paused = 0; // indicate no pause (run while dusk/dawn)
  }
  totalOn += (todayOn + 500L) / 1000L;
  todayOn  = 0;
  refSec   = ctrl->sec;
}

void Relay::turn( byte onArg )
{
  if (on == onArg)
    return;  // no change

  on = onArg;
  digitalWrite( pin, on ? LOW : HIGH );  // LOW active ==> LOW to switch on

  unsigned long now  = millis();
  unsigned long time = (now - switched);
  switched = now;

  // how much seconds we run "today"?
  // we have to remember the total running time and the time running today:

  if (! on) {  // we switch off -> add this time running
    run = time;
    todayOn += time;
  } else
    paused = time;
}

unsigned long Relay::running()
{
  if (! on)
    return 0;
  return millis() - switched;
}

unsigned long Relay::pausing()
{
  if (on)
    return 0;
  return millis() - switched;
}

unsigned long Relay::before()
{
  return todayOn;
}

unsigned long Relay::today()
{
  return (todayOn + running() + 500L) / 1000L;
}

unsigned long Relay::total()
{
  return totalOn + today();
}


int Relay::backup( int addr )
{
  addr = Ctrl::save( addr, (uint32_t) totalOn );
  addr = Ctrl::save( addr, (uint32_t) todayOn );
  addr = Ctrl::save( addr, (uint16_t) timeout );
  addr = Ctrl::save( addr, (uint8_t)  swmode  );
  return addr;
}

void Relay::restore( int addr, uint8_t len )
{
  if (len >= 10) {
    totalOn = Ctrl::read4( addr );
    todayOn = Ctrl::read4( addr + 4 );
    timeout = Ctrl::read2( addr + 8 );
    if (len >= 11) {
      swmode = Ctrl::read1( addr + 10 );
    }
  }
}

char * Relay::show( char * buf, byte menuitem, byte init, const char * name )
{
  // |0123456789abcdef|
  // |B.ein: 2:34 h:mm|
  // |heute: 4:45 h:mm|

  buf[   0] = '|';
  buf[0x11] = '|';
  buf[0x22] = '|';
  buf[0x23] = 0;

  static byte adjtbl[] = { 60, 10, 1 };  // 1 hour, 10 min, 1 min
  short adj;

  switch (menuitem) {
    case 1:
      {
        byte const len = strlen( name );
        memcpy( buf + 1, name, len );
        memset( buf + 1 + len, ' ', 0x10 - len );
      }
      memcpy( buf + 0x12, on ? "ein" : "aus", 3 );
      memcpy( buf + 0x15, ":     ", 6 );
      Display::dhms( buf + 0x1b, (millis() - switched + 500L) / 1000L );
      break;

    case 2:
      memcpy( buf +    1, "vorher:  ", 9 );
      Display::dhms( buf +   10, (run    + 500L) / 1000L );
      memcpy( buf + 0x12, "Pause:   ", 9 );
      Display::dhms( buf + 0x1b, (paused + 500L) / 1000L );
      break;

    case 3:
      memcpy( buf +    1, "heute:   ", 9 );
      Display::dhms( buf +   10, today() );
      memcpy( buf + 0x12, "gesamt:  ", 9 );
      Display::dhms( buf + 0x1b, total() );
      break;

    case 4:
      memcpy( buf +    1, "heute:   ", 9 );
      Display::percentage( buf +   10, today(), ctrl->sec + (refSec ? -refSec : ctrl->todayOn) );
      memcpy( buf + 0x12, "gesamt:  ", 9 );
      Display::percentage( buf + 0x1b, total(), ctrl->sec + ctrl->totalOn );
      break;

    default:
      if (menuitem > 10)
        return 0;

      adj = adjtbl[ (menuitem - 5) >> 1 ];

      memcpy(       buf +    1, "Ausz. +=", 8 );
      Display::hms( buf +    9, (short) adj * 60 );
      if (! (menuitem & 1)) {
        buf[7] = '-';
        adj = -adj;
      }

      if (init)
        secEnter = ctrl->sec;
      else if (ctrl->sec > (secEnter + 3)) {
        if (adj < 0)
          if (timeout <= -adj)  // min.: 1 minute
            memcpy( buf +    1, "Limit erreicht  ", 16 );
          else
            timeout += adj;
        else
          if ((timeout + adj) > 360)  // max.: 6h
            memcpy( buf +    1, "Limit erreicht  ", 16 );
          else
            timeout += adj;
      }
      memcpy(       buf + 0x12, "Auszeit:", 8 );
      Display::hms( buf + 0x1a, timeout * 60 );
      break;
  }

  buf[0x11] = '|';
  buf[0x22] = '|';
  buf[0x23] = 0;
  return buf;
}
