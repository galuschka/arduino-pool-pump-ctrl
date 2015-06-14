#include "ctrl.h"
#include "lumi.h"
#include "relay.h"
#include "display.h"

//     12
//     ___
//    / | \     <- day:   (status & 1) == 0
// 6 |--+--| 18
//    \_|_/     <- night: (status & 1) == 1
//           _
//      0   '`---- early night / early day: (status & 2) == 2
//
//  status:
//          0.. 6:  1  deep night:  check luminance >= lumDawn
//          6..12:  2  early day:   check luminance >= lumDawn / 2 + 50% to clear 2
//         12..18:  0  light day:   check luminance <= lumDawn
//         18..24:  3  early night: check luminance <= lumDawn / 2 to clear 2
//  status & 4 -> autoOn:
//         18..24:  7  autoOn during detect deep night
//          0.. 6:  5  autoOn during detect dawn
// on 9-11:
//          lumDusk/Dawn: 70/30     (changed to 70/40 to "increase midnight")
//          dayLight:     13:10     (-> night: 10:50)
//          dawn:          6:17 DST (-> 5:17 -> midnight 23:50)

Lumi::Lumi( byte pinArg )
  : pin(      pinArg )
  , status(        0 )  // see above
  , cntDetect(     0 )  // how often did we detect sunrise/sunset in sequence
  , lumSwitch( 0x200 )  // below this value: switch on light
  , lumDawn(   0x200 )  // when 6 times in sequence above/below this value: sunrise/sunset
  , lumNight(  0x100 )  // = (lumDawn / 2)    e.g. lumDawn = 40% -> lumNight = 20%
  , lumDay(    0x300 )  // = 50% + lumNight   e.g. lumDawn = 40% -> lumDay   = 70%
  , lumCurr(   0x200 )  // current luminance value
  , secCorr(       0 )  // correction to calculate midnight (DST / local geo offset)
  , timeOff(   -3600 )  // 1h before midnight
  , secOff(        0 )  // ctrl->sec, when to switch off today (calculated, when switched on)
  , dayLight(  43200L)  // as long as we don't know better, we assume 12 hours day light
  , midnight(      0 )  // yet unknown
  , secDusk(       0 )  // sunset time (secs counter)
  , secDawn(       0 )  // sunrise time (secs counter)
{
}

void Lumi::setup( Ctrl * ctrlArg )
{
  ctrl = ctrlArg;
}

byte Lumi::secLoop(void)
{
  if (ctrl->sec % 10)
    return NOCHANGE;

  lumCurr = analogRead( pin );  // current luminance
  ctrl->display->info( Display::NUM_LUM, (((lumCurr * 25) + 0x80) >> 8) & 0x7f );

  if (status & 4) {
    // secOff is the absolute time value, when we have to switch off
    signed long diff = secOff - ctrl->sec;
    if (diff <= 0) {
      status &= 3;
      ctrl->lampRelay->autoOn( 0 );
    }
  }

  switch (status & 3)  // 1-2-0-3
  {
    case 1:  //  0.. 6:  night and check luminance >= lumDawn
      if (lumCurr < lumDawn) {
        cntDetect = 0;
        break;
      }
      if (++cntDetect < 6)
        break;

      if (secDusk) { // when not set, we detected night at bootup
        // at day time, secDawn is dawn of today and secDusk is dusk of yesterday
        // ==> secDusk of today will be 86400 secs later
        unsigned long const newDayLight = (secDusk + 86400L - ctrl->sec);
        // avgChange = ((avgChange * 3) + (newDayLight - dayLight) + 2) / 4;
        dayLight = newDayLight;
      }

      secDawn = ctrl->sec;  // remember dawn time
      midnight = secDawn + (dayLight / 2L) + secCorr + 43200L;  // next midnight

      cntDetect = 0;
      if (status & 4) {
        status = 2;
        ctrl->lampRelay->autoOn( 0 );  // auto off in any case at dawn
      }
      status = 2;
      ctrl->pumpRelay->night( 0 );  // start new day when dawn (to even check values at night)
      return DAWN;

    case 2:  //  6..12:  check luminance >= 50% + lumDawn / 2 to clear 2
      if (lumCurr < lumDay)
        cntDetect = 0;
      else if (++cntDetect >= 6)
        status &= 5;
      break;

    case 0:  // 12..18:  check luminance < lumDawn

      if (status & 4) {             // switched on
        if (lumCurr >= lumSwitch) {   // lighter than needed (temporary dark)
          status = 0;
          ctrl->lampRelay->autoOn( 0 );
        }
      } else {                      // not yet switched on
        if (lumCurr < lumSwitch) {  // already dark enough to switch on
          status |= 4;
          ctrl->lampRelay->autoOn( 1 );
          if (midnight)
            secOff = midnight + timeOff;  // midnight is next(!) midnight
          else
            secOff = ctrl->sec + 14400;  // 4h
        }
      }

      if (lumCurr >= lumDawn) {   // not yet sunset
        cntDetect = 0;
        break;
      }
      if ((++cntDetect < 6) && ctrl->sec)  // detect night during boot up
        break;

      secDusk = ctrl->sec;  // remember dusk time
      cntDetect = 0;
      status |= 3;
      ctrl->lampRelay->night( 1 );  // start new day when dusk (to check values next day)

      return DUSK;

    case 3:  // 18..24:  night and check luminance <= lumDawn / 2 to clear 2
      if (lumCurr > lumNight)
        cntDetect = 0;
      else if (++cntDetect >= 6)
        status &= 5;
      break;
  }

  return NOCHANGE;
}

int Lumi::backup( int addr )
{
  addr = Ctrl::save( addr, (uint32_t) timeOff   );
  addr = Ctrl::save( addr, (uint32_t) dayLight  );
  addr = Ctrl::save( addr, (uint16_t) secCorr   );
  addr = Ctrl::save( addr, (uint16_t) lumSwitch );
  addr = Ctrl::save( addr, (uint16_t) lumDawn   );
  return addr;
}

void Lumi::restore( int addr, uint8_t len )
{
  if (len >= 14) {
    timeOff  = Ctrl::read4( addr + 0 );
    dayLight = Ctrl::read4( addr + 4 );
#if 1
    secCorr  = Ctrl::read2( addr + 10 );
    lumSwitch = ((((word) Ctrl::read1( addr + 12 ) << 8) + 12) / 25);
    lumDawn   = ((((word) Ctrl::read1( addr + 13 ) << 8) + 12) / 25);
#else
    secCorr   = Ctrl::read2( addr +  8 );
    lumSwitch = Ctrl::read2( addr + 10 );
    lumDawn   = Ctrl::read2( addr + 12 );
#endif
    lumNight  = (lumDawn / 2);     // e.g. lumDawn = 40% -> lumNight = 20%
    lumDay    = 0x200 + lumNight;  // e.g. lumDawn = 40% -> lumDay   = 70%
  }
}

static short adjtbl[] = { 3600, 600, 60, 10, 1 };  // 1 hour, 10 min, 1 min, 10 sec, 1 sec

char * Lumi::showTime( char * buf, byte menuitem, byte init )
{
  if ((menuitem > 11) || ((! midnight) && (menuitem > 1)))
    return 0;

  memset( buf, ' ', 0x22 );
  buf[   0] = '|';
  buf[0x11] = '|';
  buf[0x22] = '|';
  buf[0x23] = 0;

  if (menuitem == 1) {
    memcpy(         buf +    1, "Korr.: ", 7 );
    if (secCorr < 0) {
      buf[8] = '-';
      Display::hms( buf +    9, -secCorr );
    } else {
      buf[8] = '+';
      Display::hms( buf +    9, secCorr );
    }
    buf[0x11] = '|';
    if (midnight) {
      memcpy(       buf + 0x12, "Uhrzeit:", 8 );
      Display::hms( buf + 0x1a, ctrl->sec - midnight );  // modulo done in hms()
    }
    return buf;
  }

  // menuitem  adj
  //      2: +3600
  //      3: -3600
  //      4:  +600
  //      5:  -600
  //      6:   +60
  //      7:   -60
  //      8:   +10
  //      9:   -10
  //     10:    +1
  //     11:    -1

  short adj = adjtbl[ (menuitem - 2) >> 1 ];

  memcpy(       buf +    1, "Zeit  +=", 8 );
  Display::hms( buf +    9, adj );
  if (menuitem & 1)
    buf[7] = '-';
  else
    adj = -adj;  // decrement secCorr and midnight to increase time

  if (init)
    secEnter = ctrl->sec;
  else if (ctrl->sec > (secEnter + 3)) {
    secCorr += adj;
    if ((secCorr > +14400) || (secCorr < -14400)) {  // +/-4h
      secCorr -= adj;  // undo when out of range
      memcpy( buf +    1, "Limit erreicht  ", 16 );
    } else {
      midnight += adj;
    }
  }
  memcpy(       buf + 0x12, "Uhrzeit:", 8 );
  Display::hms( buf + 0x1a, ctrl->sec - midnight );
  buf[0x11] = '|';
  buf[0x22] = '|';
  return buf;
}

char * Lumi::showDown( char * buf, byte menuitem, byte init )
{
  // |0123456789abcdef|
  // |Tag seit: 3:45 h|
  // |hell:    12:45 h|

  memset( buf, ' ', 0x22 );

  if ((! midnight) && (menuitem >= 2))
    ++menuitem;  // 2..14 -> 3..15 (skip item 2 since no info)

  short adj;

  switch (menuitem) {
    case 1:
      if (status & 1) {
        if (secDusk) {
          memcpy(        buf +    1, "N. seit: ", 9 );
          Display::dhms( buf +   10, ctrl->sec - secDusk );
        } else {
          memcpy(        buf +    1, "Nacht",     5 );
        }
      } else {
        if (secDawn) {
          memcpy(        buf +    1, "Tag seit:", 9 );
          Display::dhms( buf +   10, ctrl->sec - secDawn );
        } else {
          memcpy(        buf +    1, "Tag",       3 );
        }
      }
      memcpy(        buf + 0x12, "hell:    ", 9 );
      Display::dhms( buf + 0x1b, dayLight );
      break;

    case 2:
      if (secDawn) {
        memcpy(       buf +    1, "Aufgang:", 8 );
        Display::hms( buf +    9, secDawn - midnight );  // modulo done in hms()
      }
      if (secDusk) {
        memcpy(       buf + 0x12, "Unterg.:", 8 );
        Display::hms( buf + 0x1a, secDusk - midnight );  // modulo done in hms()
      }
      break;

    case 3:
      memcpy(       buf +    1, "Absch.: ", 8 );
      Display::hms( buf +    9, timeOff );

      if (midnight) {
        if (secOff) {
          if (secOff < ctrl->sec) {             // turned off in past
            memcpy(       buf + 0x12, "abges.: ", 8 );
            Display::hms( buf + 0x1a, secOff - midnight );
          } else                                // will turn off in future
          if (secOff < (ctrl->sec + 43200L)) {  // less than 12h
            memcpy(       buf + 0x12, "absch.: ", 8 );
            Display::hms( buf + 0x1a, secOff - midnight );
          } else {                              // error: more than 12h
            memcpy(        buf + 0x12, "noch ein:", 9 );
            Display::dhms( buf + 0x1b, secOff - ctrl->sec );
          }
        }
      } else if (status & 4) {
        long const diff = secOff - ctrl->sec;
        if (diff > 0) {
          memcpy(        buf + 0x12, "noch ein:", 9 );
          Display::dhms( buf + 0x1b, diff );
        } else if (diff >= -10) {
          memcpy(        buf + 0x12, "aus", 3 );
          memset(        buf + 0x15, '.', -diff );
        } else {
          memcpy(        buf + 0x12, "sollte aus sein! ", 16 );
        }
      } else if (secOff) {
        memcpy(        buf + 0x12, "aus seit:", 9 );
        Display::dhms( buf + 0x1b, ctrl->sec - secOff );
      }
      break;

    default:
      if (menuitem >= 14)
        return 0;

      if (menuitem >= 10) {
        // menuitem  adj
        //   10: lumSwitch += 10%
        //   11: lumSwitch -= 10%
        //   12: lumDawn   += 10%
        //   13: lumDawn   -= 10%
                                // 0123456789abcdef
        memcpy(       buf +    1, "Schwellwert erh.", 16 );
        if (menuitem & 1)
          memcpy(     buf +  0xd, "red.", 4 );

        word * pval;
        if (menuitem & 2) {
          memcpy(     buf + 0x12, "D" STR_AUML "mmerung:     %", 16 );
          pval = & lumDawn;
        } else {
          memcpy(     buf + 0x12, "Schaltpunkt:   %", 16 );
          pval = & lumSwitch;
        }
        if (init)
          secEnter = ctrl->sec;
        else if (ctrl->sec > (secEnter + 3)) {
          if (menuitem & 1) {
            if ((*pval >= 204) && (*pval <= 922))   // 20..90%: -=10%
              *pval = (((((((*pval * 10) + 0x200) >> 10) & 0xf) - 1) << 10) + 5) / 10;
            else if (*pval >= 20)                   // 2..100%: -=1%
              *pval = (((((((*pval * 25) +  0x80) >> 8) & 0x7f) - 1) << 8) + 12) / 25;
          } else {
            if ((*pval >= 102) && (*pval <= 820))   // 10..80%: +=10%
              *pval = (((((((*pval * 10) + 0x200) >> 10) & 0xf) + 1) << 10) + 5) / 10;
            else if (*pval <= 1004)                 //  0..98%: +=1%
              *pval = (((((((*pval * 25) +  0x80) >> 8) & 0x7f) + 1) << 8) + 12) / 25;
          }
          if (menuitem & 2) {
            lumNight = (lumDawn / 2);
            lumDay   = 0x200 + lumNight;
          }
        }
        Display::itoa( buf + 0x1f, 3, (((*pval * 25) + 0x80) >> 8) & 0x7f );
        buf[0x21] = '%';
        break;
      }

      // menuitem   adj
      //       4: +3600
      //       5: -3600
      //       6:  +600
      //       7:  -600
      //       8:   +60
      //       9:   -60
      adj = adjtbl[ (menuitem - 4) >> 1 ];

      memcpy(       buf +    1, "Absch.+=", 8 );
      Display::hms( buf +    9, adj );
      if (menuitem & 1) {
        buf[7] = '-';
        adj = -adj;
      }
      if (init)
        secEnter = ctrl->sec;
      else if (ctrl->sec > (secEnter + 3)) {
        timeOff += adj;
        if ((timeOff >= +43200L) || (timeOff <= -43200L)) {
          timeOff -= adj;  // undo when out of range
          memcpy( buf +    1, "Limit erreicht", 14 );
        }
      }

      memcpy(       buf + 0x12, "Absch.: ", 8 );
      Display::hms( buf + 0x1a, timeOff );
      break;
  }

  buf[   0] = '|';
  buf[0x11] = '|';
  buf[0x22] = '|';
  buf[0x23] = 0;
  return buf;
}
