#include <OneWire.h>
#include "ctrl.h"
#include "display.h"
#include "relay.h"
#include "switch.h"
#include "lumi.h"
#include "temp.h"

byte const atPool[] = { TempDevAddrPool };
byte const atIns[]  = { TempDevAddrIns  };
byte const atSol[]  = { TempDevAddrSol  };
byte const atAir[]  = { TempDevAddrAir  };
byte const atBox[]  = { TempDevAddrBox  };


const struct {
  char const * name;
  byte const * addr;
  byte         sensorNum;
  byte         displayNum;

} aTemp[] = { { "Solar" ,atSol  ,Temp::SENSOR_SOL  ,Display::NUM_SOL  }
             ,{ "Pool " ,atPool ,Temp::SENSOR_POOL ,Display::NUM_POOL }
             ,{ "Ins  " ,atIns  ,Temp::SENSOR_INS  ,Display::NUM_INS  }
             ,{ "Air  " ,atAir  ,Temp::SENSOR_AIR  ,Display::NUM_AIR  }
             ,{ "Ctrl " ,atBox  ,Temp::SENSOR_BOX  ,Display::NUM_BOX  }
};

Temp::Temp()
  : index( SENSOR_COUNT ) // -> begin at 1st sensor in 1st loop
  , state(  0 )           // not conv -> do conv on 1st step
  , devok(  0 )           // no sensor yet read
  , autoon( 0 )           // yet not switched on

  , shiftPausing( 11 )
  , shiftB4Start( 17 )
  , shiftRunning( 15 )
  , shiftB4Stop(  16 )
  , res( NO_ACTION )
{
}

void Temp::setup( Ctrl * ctrlArg, OneWire * owArg )
{
  ctrl = ctrlArg;
  ow   = owArg;

  usecNextaction = usecNextstart = micros() + (2 _M);  // give rest of system 2 more secs to startup

  for (byte i = 0; i < NELEMENTS(aTemp); ++i) {
    mem * const m = & t[aTemp[i].sensorNum];

    displayNum[aTemp[i].sensorNum] = aTemp[i].displayNum;
    sensorNum[aTemp[i].displayNum] = aTemp[i].sensorNum;

    m->name = aTemp[i].name;
    m->addr = aTemp[i].addr;
    m->conv = 0; // overwritten in check(), when addr is valid
    m->res  = 0; // set to correct value, on 1st successful read
    m->min[0] = 0x7fff;  // invalid value to set all min/max on very first read
    check( m );
  }

  next( /*restart:*/ true );  // valid index in 1st loop call
}

void Temp::loop(void)
{
#ifdef TEMP_DEBUG
  if (res != NO_ACTION) {
    switch (res)
    {
      case NOT_COMPLETE:
        Serial.println( "    no switch, when any data invalid" );
        break;
      case STILL_NIGHT:
        Serial.println( "    still night" );
        break;
      case CAUSE_NIGHT:
        Serial.println( "    switched off cause dusk" );
        break;

      case STAY_TIME:
      case STAY_TEMP:
      case STAY_COLD:
      case CAUSE_TIME:
      case CAUSE_TEMP:
        switch (res) {
          case CAUSE_TIME:
          case CAUSE_TEMP: Serial.print( "        switched " ); break;
          default:         Serial.print( "    not switched " ); break;
        }
        switch (res) {
          case STAY_TIME:
          case CAUSE_TIME: Serial.print( "cause time  " ); break;
          default:         Serial.print( "cause temp. " ); break;
        }
        Serial.print( before / 60000.0 ); // minutes as float
        Serial.print( " / " );
        Serial.print( time / 60000.0 ); // minutes as float
        Serial.print( " / " );
        Serial.print( threshold / 16.0 );  // raw -> C
        Serial.print( " / " );
        Serial.print( diff / 16.0 );  // raw -> Kelvin
        Serial.println( " (before/time/thres/diff)" );
        break;
    }
    res = NO_ACTION;
  }
#else
  res = NO_ACTION;
#endif

  long delta = micros() - usecNextaction;
  if (delta < 0)
    return;

  char const * const err = act();
  if (err) { // error happened
    // not finalize
    restart();
#ifdef DEBUG
    Serial.print("error on device ");
    Serial.print(t[index].name);
    Serial.print(": ");
    Serial.println(err);
#endif
    return;
  }
}

void Temp::restart(void)
{
  boolean fastread = ctrl->pumpRelay->isOn();
  if (fastread)
    usecNextstart += 7500 _k;  // run every 7.5 secs (1/8 of one minute)
  else
    usecNextstart += 60 _M;    // run every minute when pump is off

  long delta = usecNextstart - micros();
  if (delta < 0)  // loop(s) was/were too long
    usecNextstart = micros() + 10 _M; // restart in 10 sec

  usecNextaction = usecNextstart;

  state &= 0xe;  // 0xe is 7 << 1 (bit 0 used for conv/data)
  if (state)
    state -= 2;   // 0xe,0xc,0xa,8,6,4,2 -> 0xc,0xa,8,6,4,2,0
  else if (fastread)
    state = 0xe;  // 0 -> 0xe (but just, when fast read)

  next( /*restart:*/ true );
}

boolean Temp::next( boolean restart )
{
  state &= 0xe;  // bit 0 should be cleared anyway...

  // look for the next (first) sensor to read
  for (index = (restart ? 0 : (index + 1)); index < SENSOR_COUNT; ++index)

    if (t[index].conv           // valid sensor address
     && ((index == SENSOR_SOL)  // solar sensor is expected to change often...: read on every restart
      || (index == SENSOR_INS)  // insertion sensor is expected to change often too
      || ! (state & 0xe)        // low change temp. sensors to read now
      || ! (devok & (1 << index))))  // low change temp. sensors not yet read successful

        return true;

  return false;
}

void Temp::check( mem * m )
{
  if (OneWire::crc8(m->addr, 7) != m->addr[7]) {
#ifdef DEBUG
    Serial.print(m->name);
    Serial.println("  CRC of address is not valid!");
#endif
    return;
  }

  // the first ROM byte indicates which chip
  switch (m->addr[0]) {
    case 0x10: // DS1820 (old)
    case 0x28: // DS18B20
    case 0x22: // DS1822
      break;
    default:
#ifdef DEBUG
      Serial.print(m->name);
      Serial.println("  Device is not a DS18x20 family device.");
#endif
      return;
  }

  m->conv = 750000; // addr is ok ==> let know, that we can read temperature
}

char const * Temp::act(void)
{
  if (index >= SENSOR_COUNT)
    return "no valid device in list";

  char const * ret = 0;
  switch (state & 1)
  {
    case 0:
      ret = conv();
      break;

    case 1:
      ret = data();
      if (ret) {
        DEBUG_EXPR( Serial.print("error on device ") )
        DEBUG_EXPR( Serial.print(t[index].name) )
        DEBUG_EXPR( Serial.print(": ") )
        DEBUG_EXPR( Serial.println(ret) )
        ret = 0;
      }

      if (next()) {
        ret = conv();
        break;
      }
      res = finalize();
      restart();
      break;
  }
  return ret;
}

char const * Temp::conv(void)
{
  devok &= ~(1 << index);

  if (! ow->reset())
    return "no device detected";

  mem * const m = & t[index];

  ow->select( m->addr );
  ow->write( 0x44, 0 );        // start conversion, with parasite power on at the end

  usecNextaction = micros() + m->conv + 100000; // add safety
  state |= 1; // "conv running"
  return 0;
}

char const * Temp::data()
{
  if (! ow->reset())
    return "no device detected";

  mem * const m = & t[index];

  ow->select( m->addr );
  ow->write( 0xbe );         // Read Scratchpad

  byte buf[10];
  int i;
  for (i = 0; i < 9; i++)   // we need 9 bytes
    buf[i] = ow->read();

  if (OneWire::crc8( buf, 8 ) != buf[8])
    return "data CRC invalid";

  if (((buf[0] == 0x50) && (buf[1] == 0x05)) ||
      ((buf[0] == 0xff) && (buf[1] == 0x07)) ||
      ((! buf[0]) && (! buf[1]) && (! buf[8]))) {
#ifdef DEBUG
    Serial.print("  strange data for device ");
    Serial.print(m->name);
    Serial.print(":");
    for (i = 0; i < 9; i++) {
      Serial.print(" ");
      if (buf[i] < 0x10) Serial.print("0");
      Serial.print(buf[i],HEX);
    }
    Serial.println();
#endif
    return "strange data";
  }
#if 0
  else {
    Serial.print("          data for device ");
    Serial.print(m->name);
    Serial.print(":");
    for (i = 0; i < 9; i++) {
      Serial.print(" ");
      if (buf[i] < 0x10) Serial.print("0");
      Serial.print(buf[i],HEX);
    }
    Serial.println();
  }
#endif

  int16_t raw = (buf[1] << 8) | buf[0];

  if (m->addr[0] == 0x10) {
    raw = raw << 3; // 9 bit resolution default
    if (buf[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - buf[6];
    }
  }
  // if (raw & 0x800)  raw |= 0xf000;  // 12 bit negative to 16 bit negative

  if (! m->res) {
    // 1st successful read
    if (m->addr[0] == 0x10)
       m->res = ~0;
    else
    switch (buf[4] & 0x60) {
      case    0: m->res = ~7; m->conv =  93750; break; //  9 bit resolution,  93.75 ms
      case 0x20: m->res = ~3; m->conv = 187500; break; // 10 bit resolution, 187.5 ms
      case 0x40: m->res = ~1; m->conv = 375000; break; // 11 bit resolution, 375 ms
      default:   m->res = ~0; m->conv = 750000; break; // 12 bit resolution, 750 ms conversion time
    }

    raw &= m->res; // at lower res, the low bits are undefined, so let's zero them
    m->temp = raw;
    // init sum:
    m->sum = raw << 3;
    m->avg = raw;

    if (m->min[0] == 0x7fff)
      for (byte x = 0; x < PERIOD_COUNT; ++x)
        m->min[x] = m->max[x] = raw;
  } else {
    raw &= m->res; // at lower res, the low bits are undefined, so let's zero them
    m->temp = raw;
    // low pass filter:
    m->sum += (raw - m->avg);    // 1/8 of delta added to avg
    m->avg = (m->sum + 4) >> 3;  // round sum / 8

    if (m->min[0] > m->avg)
        m->min[0] = m->avg;
    else
    if (m->max[0] < m->avg)
        m->max[0] = m->avg;
  }

  // note: m->temp is unchanged, if read fails

  devok |= (1 << index);
  ctrl->display->info( displayNum[index], tocelsius(raw) );
  return 0;
}

void Temp::night( byte isNight )  // currently becoming night or day
{
  if (isNight) {                  // -> dusk
    ctrl->pumpRelay->autoOn( autoon = 0 );  // don't run at night
  }
  else {
    int const day = ((ctrl->sec + ctrl->totalOn) / 86400L + 1);
    for (byte d = 0; d < SENSOR_COUNT; ++d) {
      mem * const m = & t[d];

      if (! (day % 7)) {        // 7 days
        byte p = PERIOD_WEEK;   // -> p+1 = PERIOD_MONTH
        if (! (day % 28)) {     // 4 weeks
          ++p;                  // PERIOD_MONTH -> p+1 = PERIOD_YEAR
          if (! (day % 336))    // 12 "months"
            ++p;                // PERIOD_YEAR -> p+1 = PERIOD_OVERALL
        }
        while (p != PERIOD_DAY) {
          if (m->min[p+1] > m->min[p])
              m->min[p+1] = m->min[p];  // e.g. new minimum this year: minimum last week
          if (m->max[p+1] < m->max[p])
              m->max[p+1] = m->max[p];  // e.g. new maximum this year: maximum last week

          m->min[p] = m->min[p-1];  // e.g. min/max of this week set to min/max of yesterday
          m->max[p] = m->max[p-1];
          --p;
        }
      }
      if (m->min[PERIOD_WEEK] > m->min[PERIOD_DAY])
          m->min[PERIOD_WEEK] = m->min[PERIOD_DAY];  // new minimum this week: minimum yesterday
      if (m->max[PERIOD_WEEK] < m->max[PERIOD_DAY])
          m->max[PERIOD_WEEK] = m->max[PERIOD_DAY];  // new maxnimum this week: maximum yesterday

      m->min[PERIOD_DAY] =
      m->max[PERIOD_DAY] = m->avg;  // min/max of "today" reset to actual value
    }
  }
}

byte Temp::finalize(void)
{
  if (ctrl->lumi->night()) {
    if (autoon) {
      ctrl->pumpRelay->autoOn( autoon = 0 );  // don't run at night
      DEBUG_EXPR( Serial.println( "    stay off at night" ) )
      return CAUSE_NIGHT;
    }
    return STILL_NIGHT;
  }

  time      = ctrl->pumpRelay->running();
  before    = ctrl->pumpRelay->before();
  diff      = t[SENSOR_SOL].temp - t[SENSOR_POOL].temp;
  threshold = 0;  // to indicate "temperature not to check"

  if (time) { // running

    // it may take some time, until the "hot" water arrives at pool insertion
    if (devok & (1 << SENSOR_INS))                        // insertion sensor value is valid
      if (t[SENSOR_INS].temp > t[SENSOR_SOL].temp)        // higher temperature at insertion
        diff = t[SENSOR_INS].temp - t[SENSOR_POOL].temp;  // use the higher difference

    if (time < (60 _k)) { // running less than 1 minute:
      if (! autoon) // manual switched on
        ctrl->pumpRelay->autoOn( autoon = 1 );

      // not to switch off, since running less than 1 minute
      return STAY_TIME;
    }

    if (time >= (2 * 60 * 60 _k)) { // running 2 hours or more
      if (autoon)
        ctrl->pumpRelay->autoOn( autoon = 0 );  // maximum running time reached

      // to switch off due to running more than 2 hours
      return CAUSE_TIME;
    }

    // the longer we run, the higher delta is needed to stay running
    // previous run counts half

    threshold = (short) (((time >> shiftRunning) + (before >> shiftB4Stop)) & 0x7fff);

    if (diff <= threshold) { // switch off when diff decreases
      if (autoon)
        ctrl->pumpRelay->autoOn( autoon = 0 ); // turn off, when "normal" difference

      return CAUSE_TEMP;
    }

    if (! autoon) // manual switched on
      ctrl->pumpRelay->autoOn( autoon = 1 );

    return STAY_TEMP;
  }

  // ... not running:

  if ((devok & ((1 << SENSOR_POOL) | (1 << SENSOR_SOL)))
            != ((1 << SENSOR_POOL) | (1 << SENSOR_SOL))) {
    // not to switch on, since values unknown
    return NOT_COMPLETE;  // exact values unknown -> don't switch on when we can't trust
  }

  time = ctrl->pumpRelay->pausing();

  if (time < (1 * 60 _k)) { // pausing less than 1 minute
    if (autoon) // manual switched off
      ctrl->pumpRelay->autoOn( autoon = 0 );

    // not to switch on, since pausing less than 1 minute
    return STAY_TIME;
  }
  if (time >= (2 * 60 * 60 _k)) {
    if (diff >= 0) {
      if (! autoon)
        ctrl->pumpRelay->autoOn( autoon = 1 );  // maximum idle time reached

      // switch on, due to pausing more than 2 houre and diff >= 0
      return CAUSE_TIME;
    }
    if (autoon)
      ctrl->pumpRelay->autoOn( autoon = 0 );

    // not to switch on, even so pausing more than 2 houre but diff < 0
    return STAY_COLD; // colder than pool: not run
  }

  // the longer we paused, the lower delta is needed to switch on
  // but the longer we run before, the higher delta is needed to switch on

  threshold = (0x7fff / ((short) ((time >> shiftPausing) & 0x7fff))) + (before >> shiftB4Start);

  if (diff >= threshold) {
    if (! autoon)
      ctrl->pumpRelay->autoOn( autoon = 1 );  // turn on, when "big" difference
    return CAUSE_TEMP;
  }

  if (autoon)
    ctrl->pumpRelay->autoOn( autoon = 0 );

  return STAY_TEMP;
}

int8_t Temp::tocelsius(short raw)
{
  if (raw >= 0x0800)  // 128 C and more...
    return 0x7f;      // will use 127 C

  return ((raw + 8) / 16);  // we don't expect temperatures below -128 C
}

int Temp::backup( int addr )        // in: start address / return: end address + 1
{
  addr = Ctrl::save( addr, (uint8_t) TEMP_FORMAT );  // format number of sub

  for (byte index = 0; index < SENSOR_COUNT; ++index)
  {
    mem * const m = & t[index];
    // even (int16_t) m->min[0] == (int16_t) 0x7fff) leads to compiler warning
    if (m->min[0] == 0x7fff)
      continue;  // not any valid value until now

    addr = Ctrl::save( addr, (uint8_t)     m->name[0] );
    addr = Ctrl::save( addr, (uint8_t *) & m->min[0], PERIOD_COUNT * sizeof(uint16_t) );
    addr = Ctrl::save( addr, (uint8_t *) & m->max[0], PERIOD_COUNT * sizeof(uint16_t) );
  }

  addr = Ctrl::save( addr, (uint8_t) '%' );  // -> settings
  addr = Ctrl::save( addr, (uint8_t) shiftPausing );
  addr = Ctrl::save( addr, (uint8_t) shiftB4Start );
  addr = Ctrl::save( addr, (uint8_t) shiftRunning );
  addr = Ctrl::save( addr, (uint8_t) shiftB4Stop );

  return addr;
}

void Temp::restore( int addr, uint8_t len )
{
  if (! len)
    return;

  char x = Ctrl::read1( addr++ );
  --len;
  if (x != TEMP_FORMAT)
    return;  // unknown format

  while (len) {
    x = Ctrl::read1( addr++ );
    --len;

    if (x == '%') {
      if (len < 4)
        break;
      shiftPausing = Ctrl::read1( addr++ ); --len;
      shiftB4Start = Ctrl::read1( addr++ ); --len;
      shiftRunning = Ctrl::read1( addr++ ); --len;
      shiftB4Stop  = Ctrl::read1( addr++ ); --len;
    } else {
      if (len < (2 * PERIOD_COUNT * 2))
        break;

      for (byte i = 0; i < SENSOR_COUNT; ++i) {
        mem * const m = & t[i];
        if (m->name[0] == x) {
          Ctrl::readN( addr,                      (uint8_t *) & m->min[0], PERIOD_COUNT * 2 );
          Ctrl::readN( addr + (PERIOD_COUNT * 2), (uint8_t *) & m->max[0], PERIOD_COUNT * 2 );
#if 0 // quick and dirty work around to fix min 0 values
          for (byte p = 0; p < PERIOD_COUNT; ++p) {
            if (p && ! m->max[p])
              m->max[p] = m->max[p-1];
            if (! m->min[p])
              if (! p)
                m->min[p] = m->max[p];
              else
                m->min[p] = m->min[p-1];
          }
#endif
          break;
        }
      }
      addr += (2 * PERIOD_COUNT * 2);
      len  -= (2 * PERIOD_COUNT * 2);
    }
  }
}

char * Temp::showThres( char * buf, byte menuitem, byte init )
{
  static const char * settings[] = { "Pause (ein)" ,"Heute (ein)"
                         ,"l" STR_AUML "uft (aus)" ,"Heute (aus)" };

  if (menuitem >= 10)
    return 0;

  memset( buf + 1, ' ', 33 );
  buf[   0] = '|';
  buf[0x11] = '|';
  buf[0x22] = '|';
  buf[0x23] = 0;

  if (menuitem == 1) {
    Display::dhms( buf + 1, (before + 500) / 1000 );
    buf[6] = buf[7];
    Display::dhms( buf + 7, (time + 500) / 1000 );
    buf[12] = buf[13];
    Display::kelvin( buf + 13, threshold );
    Display::kelvin( buf + 0x1e, diff );
    ctrl->display->restart();  // do not switch back to info from here
    return buf;
  }

  memcpy( buf + 1, "Einstellung   +1", 16 );
  if (menuitem & 1)
    buf[15] = '-';
  memcpy( buf + 0x12, settings[(menuitem - 2) >> 1], 11 );
  buf[0x1d] = ':';

  if (init)
    secEnter = ctrl->sec;
  else if (ctrl->sec > (secEnter + 3)) {
    switch (menuitem - 2) {
      case 0: if (shiftPausing >  6) shiftPausing--; break;
      case 1: if (shiftPausing < 15) shiftPausing++; break;
      case 2: if (shiftB4Start < 19) shiftB4Start++; break;
      case 3: if (shiftB4Start > 10) shiftB4Start--; break;
      case 4: if (shiftRunning < 19) shiftRunning++; break;
      case 5: if (shiftRunning > 10) shiftRunning--; break;
      case 6: if (shiftB4Stop  < 19) shiftB4Stop++;  break;
      case 7: if (shiftB4Stop  > 10) shiftB4Stop--;  break;
    }
  }
  switch ((menuitem - 2) >> 1) {
    case 0: Display::itoa( buf + 0x20, 3, 15 - shiftPausing ); break;
    case 1: Display::itoa( buf + 0x20, 3, shiftB4Start - 10 ); break;
    case 2: Display::itoa( buf + 0x20, 3, shiftRunning - 10 ); break;
    case 3: Display::itoa( buf + 0x20, 3, shiftB4Stop  - 10 ); break;
  }
  buf[0x22] = '|';

  return buf;
}


char * Temp::showValue( char * buf, byte menuitem, byte displayNum )
{
  if ((menuitem > 3) || (displayNum >= Display::NUM_TEMP))
    return 0;  // no more info to show / no more sensor

  // |0123456789abcdef|

  // 0123456789abcdef_1
  // |jetzt:     -11,9|
  // |Tag: -12,3.-11,5|
  // 123456789abcdef_12

  // |Wo.:  -9,9..-7,3|
  // |Mo.: -12,3.. 5,5|

  // |Jahr: -8,9..35,3|
  // |ges.:-12,3..35,3|

  mem * const m = & t[sensorNum[displayNum]];

  if (m->min[0] == 0x7fff)
    return 0;  // keine Werte fuer diesen Sensor

  memset( buf + 1, ' ', 33 );
  buf[   0] = '|';
  buf[0x11] = '|';
  buf[0x22] = '|';
  buf[0x23] = 0;

  switch (menuitem) {
    case 1:
      memcpy( buf +  1, "jetzt:", 6 );
      Display::kelvin( buf + 13, m->temp );

      memcpy( buf + 0x12, "Tag:", 4 );
      Display::kelvin( buf + 0x18, m->min[PERIOD_DAY] );
      memcpy( buf + 0x1c, "..", 2 );
      Display::kelvin( buf + 0x1e, m->max[PERIOD_DAY] );
      ctrl->display->restart();
      break;

    case 2:
      memcpy( buf +  1, "Wo.:", 4 );
      Display::kelvin( buf +  7, m->min[PERIOD_WEEK] );
      memcpy( buf + 11, "..", 2 );
      Display::kelvin( buf + 13, m->max[PERIOD_WEEK] );

      memcpy( buf + 0x12, "Mo.:", 4 );
      Display::kelvin( buf + 0x18, m->min[PERIOD_MONTH] );
      memcpy( buf + 0x1c, "..", 2 );
      Display::kelvin( buf + 0x1e, m->max[PERIOD_MONTH] );
      break;

    case 3:
      memcpy( buf +  1, "Jahr:", 5 );
      Display::kelvin( buf +  7, m->min[PERIOD_YEAR] );
      memcpy( buf + 11, "..", 2 );
      Display::kelvin( buf + 13, m->max[PERIOD_YEAR] );

      memcpy( buf + 0x12, "ges.:", 5 );
      Display::kelvin( buf + 0x18, m->min[PERIOD_OVERALL] );
      memcpy( buf + 0x1c, "..", 2 );
      Display::kelvin( buf + 0x1e, m->max[PERIOD_OVERALL] );
      break;
  }
  return buf;
}
