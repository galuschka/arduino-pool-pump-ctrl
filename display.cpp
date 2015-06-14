#include <Arduino.h>
#include "version.h"
#include "ctrl.h"
#include "switch.h"     // Switch::ON, Switch::OFF, ...
#include "relay.h"      // Relay::show()
#include "lumi.h"       // Lumi::show()
#include "temp.h"       // Temp::show()
#include "display.h"

struct infoPos {
  byte         num;     // to once build index[] array
  byte         pos;     // row and col of info
  byte         len;     // total space for that part of info
  char         abbr;    // abbreviation letter
  char const * name;    // long name
};

static infoPos const infoMatrix[] =
{
      //     col   0123456789abcdef
      // row     +------------------+
      // 0       | L:22° S:32° H:50 |
      // 1       | W:28° E:33° baPA |
      //         +------------------+

       { Display::NUM_AIR,     0, 5, 'L', "Luft"        }
      ,{ Display::NUM_POOL, 0x10, 5, 'W', "Wasser"      }
      ,{ Display::NUM_SOL,     6, 5, 'S', "Solar"       }
      ,{ Display::NUM_INS,  0x16, 5, 'E', "Einstr" STR_OUML "md" STR_UUML "se" }
      ,{ Display::NUM_BOX,     0, 0, 'C', "Controller"  }

      ,{ Display::NUM_LUM,   0xc, 4, 'H', "Helligkeit"  }
      ,{ Display::NUM_LAMP, 0x1c, 2, 'B', "Beleuchtung" }
      ,{ Display::NUM_PUMP, 0x1e, 2, 'P', "Pumpe"       }
};

byte matrixIndex[Display::NUM_COUNT];  // num -> idx

Display::Display()
 : flags( FLAG_ON | FLAG_MENU )
{
}

void Display::setup( Ctrl * ctrlArg, LiquidCrystal * lcdArg )
{
  ctrl = ctrlArg;
  lcd = lcdArg;

  for (byte idx = 0; idx < NELEMENTS(infoMatrix); ++idx)
    matrixIndex[infoMatrix[idx].num] = idx;

  timeout = millis() + (5 _k); // initial switch to info mode
  if (! timeout) timeout = 1;  // jump over "permanent on" indicaton
  lcd->display();
  lcd->noCursor();
  lcd->clear();
  cursor = 0;

  strcpy( menucont, "|Piscino " VERSION "       " ); // VERSION must be macro
  strcpy( & menucont[0x11],          "|Holger Galuschka|" );
  strcpy( infocont, "|                |                |" );

  memset( check, '$', 7 ); check[7] = 0;
  memset( hint, 0, 0x10 );

  showcont( menucont );

  flags |= FLAG_INFO_CHANGED | FLAG_MENU_CHANGED;
}

void Display::showcont( char const * cont )
{
//lcd->clear();
  lcd->home();
  lcd->print( cont + 1 );
  lcd->setCursor( 0, 1 );
  lcd->print( cont + 0x12 );
//lcd->setCursor( cursor & 0xf, cursor >> 4 );
}

void Display::dumpcont(void)
{
#ifdef DEBUG
  if (! (flags & (FLAG_INFO_CHANGED | FLAG_MENU_CHANGED)))
    return;

  for (byte i = 0; i < 2; ++i) {
    if (! (flags & (FLAG_INFO_CHANGED << i)))
      continue;
    flags &= ~(FLAG_INFO_CHANGED << i);

    Serial.print( "    LCD " );
    Serial.print( i ? "menu: " : "info: " );
    char * cont = i ? menucont : infocont;
    byte x = cont[0x23]; // expected to be 0
    cont[0x23] = 0;
    Serial.print( cont );
    if ((! i) && hint[0]) {
      Serial.print( " (here: \"" );
      Serial.print( hint );
      Serial.print( "\")" );
      hint[0] = 0;
    }
    if (x) {
      Serial.print( " oops: \"" );
      cont[0x23] = x;
      check[7] = 0;
      Serial.print( cont + 0x23 );
      cont[0x23] = 0;
      memset( check, '$', 7 );
    }
    Serial.println();
  }
#endif
}

void Display::secLoop(void)
{
  dumpcont();

  if (! (flags & FLAG_ON) || ! timeout)
    return;

  long diff = millis() - timeout;
  if (diff < 0) {
    if (flags & FLAG_MENU)
      refresh( 0 );
    return;
  }

  if (! (flags & FLAG_MENU)) {
    flags &= ~FLAG_ON;
    lcd->noDisplay();
    return;
  }

  toggleMode();
}

void Display::restart(void)  // turn on for a while
{
  if (flags & FLAG_MENU)
    timeout = millis() +  (1 * 60 _k);  // menu mode back to info in 1 minute
  else
    timeout = millis() + (15 * 60 _k);  // info mode switch off after 15 minutes

  if (! timeout) timeout = 1;     // jump over "permanent on" indicaton

  if (flags & FLAG_ON)
    return; // just change timeout / stay on

  flags |= FLAG_ON;
  lcd->display();

  showcont( (flags & FLAG_MENU) ? menucont : infocont );
}

void Display::info( byte num, short val )
{
  if (num >= NUM_COUNT)
    return;

  infoPos const * pos = & infoMatrix[ matrixIndex[num] ];
  if (! pos->len)  // not to show box temp. (shown in menu)
    return;

  char buf[8];
  char * cp;
  char * ep;
  switch (num) {
    case NUM_PUMP:
    case NUM_LAMP:
      cp = ep = & buf[1];
      *++ep = 0;
      switch (val & 3) {
        case Switch::OFF:  *cp = 'O'; break;
        case Switch::ON:   *cp = 'I'; break;
        case Switch::AUTO: *cp = 'A'; break;
        case Switch::TEMP: *cp = 'T'; break;
      }
      *cp  |=             ((~val << 3) & 0x20);  // lower case, when "auto:off"
      *--cp = pos->abbr | ((~val << 2) & 0x20);  // lower case, when "off"
      break;

    default:
      cp = itoa( buf, sizeof(buf) - 1, val );
      ep =     & buf[ sizeof(buf) - 2 ];
      if (num != NUM_LUM) {
        *ep = CHR_DEGREE;
        *++ep = 0;
      }
      break;
  }

  byte len = ep - cp;

  while ((len + 2) < pos->len) { *--cp = ' '; ++len; }
  if    ((len + 1) < pos->len) { *--cp = ':'; ++len; }
  if    (len       < pos->len) { *--cp = pos->abbr; ++len; }

  if (memcmp( & infocont[pos->pos + 1 + (pos->pos >> 4)], cp, len )) {
    memcpy( & infocont[pos->pos + 1 + (pos->pos >> 4)], cp, len );
    strncpy( hint, cp, 0xf );
    flags |= FLAG_INFO_CHANGED;
  }

  if ((flags & (FLAG_ON|FLAG_MENU)) == FLAG_ON) {
    lcd->setCursor( pos->pos & 0xf, pos->pos >> 4 );
    lcd->print( cp );
  }
}

boolean Display::menu(void)
{
  return (flags & FLAG_MENU) != 0;
}

void Display::toggleMode(void)
{
  flags ^= FLAG_MENU;
  if (flags & FLAG_MENU) {
    menunum = 0;
    refresh( 1 );
  } else {
    if (flags & FLAG_ON)
      showcont( infocont );  // would not be done in restart, when already ON
  }

  restart();
}

void Display::key( byte num )
{
  if (num == NUM_LAMP)
    menunum = (menunum + 0x10) & 0xf0;
  else
    menunum = (menunum & 0xf0) | ((menunum + 1) & 0xf);

  refresh( 1 );
  restart();
}

void Display::refresh( byte init )
{
  char const * ccp = 0;
  char * cp = 0;
  char buf[0x24];

  do
  {
    // Serial.print( "   menunum: " );
    // Serial.println( itox( buf, sizeof(buf), menunum ) );

    if (! (menunum & 0xf)) {
      if (! init)
        return;
      switch (menunum >> 4) {
        case 0: ccp = "|gelb: next cat. |blau: next item |"; break;
        case 1: ccp = "|Systemwerte     |anzeigen";          break;
        case 2: ccp = "|Uhrzeit         |anzeigen";          break;
        case 3: ccp = "|D" STR_AUML
                         "mmerungswerte |anzeigen";          break;
        case 4: ccp = "|Einschaltzeiten |Bel. anzeigen";     break;
        case 5: ccp = "|Einschaltzeiten |Pumpe anzeigen";    break;

        case 6: ccp = "|Temperatur-     |Differenz-Werte |"; break;
        default:
                if (menunum >= ((7 + NUM_TEMP) << 4))
                  break;

                strcpy( buf, "|Temperatur-Werte|\"" );
                cp = strchr( buf, 0 );
                strcpy( cp, infoMatrix[ (menunum >> 4) - 7 ].name );
                cp = strchr( cp, 0 );
                *cp = '"';
                *++cp = 0;
                cp = buf;
                break;
      }
    } else {
      switch (menunum >> 4) {
        case 0: menunum = 0x10; break;  // next cat. also with blue key (just in intro)
        case 1: ccp = ctrl->show(            buf, menunum & 0xf, init ); break;
        case 2: ccp = ctrl->lumi->showTime(  buf, menunum & 0xf, init ); break;
        case 3: ccp = ctrl->lumi->showDown(  buf, menunum & 0xf, init ); break;
        case 4: ccp = ctrl->lampRelay->show( buf, menunum & 0xf, init, infoMatrix[ matrixIndex[NUM_LAMP] ].name ); break;
        case 5: ccp = ctrl->pumpRelay->show( buf, menunum & 0xf, init, infoMatrix[ matrixIndex[NUM_PUMP] ].name ); break;
        case 6:  cp = ctrl->temp->showThres( buf, menunum & 0xf, init                                 ); break;
        default: cp = ctrl->temp->showValue( buf, menunum & 0xf, infoMatrix[ (menunum >> 4) - 7 ].num ); break;
      }
    }

    if (! ccp)
      if (! (ccp = cp)) {  // no info
        if (! init)  // just refresh
          return;    // no refresh neccessary

        // init but no info: restart at first item (rotate)
        if (menunum & 0xf) // no item info
          menunum &= 0xf0; // header of this cat.
        else               // no header info -> end of menu -> restart with 1st cat.
          menunum = 0x10;  // skip "intro" (gelb: next cat. / blau: next item)
      }
  } while (! ccp);

  byte len = strlen(ccp);
  cp = menucont;
  byte max = 0x22;
  if (*ccp != '|') {
    ++cp;
    --max;
  }
  if (len > max)
    len = max;
  if (init || memcmp( cp, ccp, len )) {
    memcpy( cp, ccp, len );
    if (len < max)
      memset( cp + len, ' ', max - len );

    flags |= FLAG_MENU_CHANGED;
    showcont( menucont );
  }
}

char * Display::itoa( char * buf, int bufsize, int digit )  // bufsize: incl. \0
{
  byte neg = 0;
  if (digit < 0) {
    digit = -digit;
    neg = 1;
  }

  char * cp = & buf[bufsize - 1];
  *cp = 0;

  do {
    *--cp = (digit % 10) ? (digit % 10) + '0' : 'O';  // o more readable than 0 (similar to 8 on LCD)
    digit /= 10;
  } while (digit);

  if (neg)
    *--cp = '-';

  return cp;
}

char * Display::itox( char * buf, int bufsize, int digit )  // bufsize: incl. \0
{
  char * cp = & buf[bufsize - 1];
  *cp = 0;

  do {
    *--cp = (digit & 0xf) + (((digit & 0xf) < 10) ? '0' : ('a' - 10));
    digit >>= 4;
  } while (digit);

  return cp;
}

char * Display::hms( char * buf, signed long secs )  // print "hh:mm:ss"
{
  secs = (secs + 345600L) % 86400L;  // add 4 days and use remainder to one day
  buf[0] = ' ';
  itoa( buf + 0, 3, secs / 3600L ); secs %= 3600L;
  buf[2] = ':';
  buf[3] = 'O';
  itoa( buf + 3, 3, secs / 60 );
  buf[5] = ':';
  buf[6] = 'O';
  itoa( buf + 6, 3, secs % 60 );
  return buf;
}

char * Display::dhms( char * buf, unsigned long secs )  // print "ddddd d", "dd;hh d", "hh:mm h" or "mm:ss m"
{
  if (secs <= 3600L) {  // mm:ss
    buf[0] = ' ';
    itoa( buf, 3, secs / 60 );
    buf[2] = ':';
    buf[3] = 'O';
    itoa( buf + 3, 3, secs % 60 );
    buf[5] = ' ';
    buf[6] = 'm';
    return buf;
  }

  secs += 30;  // round minutes
  unsigned long mins = secs / 60L;

  if (mins <= (48 * 60)) {  // hh:mm
    buf[0] = ' ';
    itoa( buf, 3, mins / 60 );
    buf[2] = ':';
    buf[3] = 'O';
    itoa( buf + 3, 3, mins % 60 );
    buf[5] = ' ';
    buf[6] = 'h';
    return buf;
  }

  mins += 30;  // round hours
  unsigned long hours = mins / 60L;

  if (hours < (2400)) {  // dd;hh
    buf[0] = ' ';
    itoa( buf, 3, hours / 24 );
    buf[2] = ';';
    buf[3] = 'O';
    itoa( buf + 3, 3, hours % 24 );
    buf[5] = ' ';
    buf[6] = 'd';
    return buf;
  }

  hours += 12;  // round days
  memset( buf, ' ', 4 );
  itoa( buf, 6, hours / 24L );
  buf[5] = ' ';
  buf[6] = 'd';
  return buf;
}

char * Display::kelvin( char * buf, int rawtemp )  // print " x,y"
{
  int sign = 1;
  if (rawtemp < 0) {
    rawtemp = -rawtemp;
    sign = -1;
  }
  int k10 = ((rawtemp * 10) + 8) >> 4;
  buf[0] = ' ';
  char * ret = itoa( buf, 3, sign * (k10 / 10) );
  buf[2] = ',';
  buf[3] = (k10 % 10) ? (k10 % 10) + '0' : 'O';
  return ret;
}

char * Display::percentage( char * buf, unsigned long val, unsigned long max )  // print "xx,yy %"
{
  unsigned int rel = 10000;
  if (max && (val < max)) {
    if (val < (0xffffffffL / 10000L))
      rel = ((val * 10000L) + (max / 2)) / max;
    else if (val < (0xffffffffL / 100L))
      rel = (val * 100L) / ((max + 50L) / 100L);
    else
      rel = val / ((max + 5000L) / 10000L);
  }

  if (max  < 10000) {
    if (max < 1000)
      rel += 50L;
    else
      rel += 5L;
  }

  if (rel >= 10000) {
    memcpy( buf, "1OO   %", 7 );
    return buf;
  }

  buf[0] = ' ';
  char * ret = itoa( buf, 3, rel / 100 );

  if (max < 1000) {
    buf[2] = ' ';
    buf[3] = ' ';
    buf[4] = ' ';
  } else {
    buf[2] = ',';
    buf[3] = 'O';
    itoa( buf + 3, 3, rel % 100 );
    if (max < 10000)
      buf[4] = ' ';
  }

  buf[5] = ' ';
  buf[6] = '%';
  return ret;
}

void Display::print( char const * str )
{
  if (cursor >= 0x20)
    return;

  byte len = strlen( str );
  if (len > (0x10 - (cursor & 0xf)))
    len = (0x10 - (cursor & 0xf));

  memcpy( & menucont[cursor + 1 + (cursor >> 4)], str, len );
  cursor += len;

  strncpy( hint, str, 0xf );
  flags |= FLAG_MENU_CHANGED;

  if ((flags & (FLAG_ON|FLAG_MENU)) == (FLAG_ON|FLAG_MENU)) {
    lcd->print( str );
    if (cursor == 0x10)
      lcd->setCursor( 0, 1 );
  }
}

void Display::print( int digit )
{
  char buf[12];
  print( itoa( buf, sizeof(buf), digit ) );
}

void Display::printat( byte col, byte row, char const * str )
{
  lcd->setCursor( col, row );
  cursor = (row << 4) | col;
  print( str );
}

void Display::printat( byte col, byte row, int digit )
{
  char buf[12];
  printat( col, row, itoa( buf, sizeof(buf), digit ) );
}
