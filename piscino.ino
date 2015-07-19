#include <EEPROM.h>
#include <OneWire.h>
#include <LiquidCrystal.h>
#include <avr/wdt.h>

#include "ctrl.h"
#include "version.h"

#include "display.h"    // LCD wrapper (info/menu/duplicate content on serial output)
#include "switch.h"     // manual switches (de-chatter and call relay->...)
#include "relay.h"      // relay control (on/off and duration, "total on since ...")
#include "lumi.h"       // luminance ctrl (dusk,dawn,midnight,status...)
#include "temp.h"       // temperature reading

OneWire         ow(  PIN_OneWire );

#ifdef KEYPAD
LiquidCrystal   lcd( PIN_LCD_RS,               PIN_LCD_Ena,
                     PIN_LCD_DB4, PIN_LCD_DB5, PIN_LCD_DB6, PIN_LCD_DB7 );
#else
LiquidCrystal   lcd( PIN_LCD_RS,  PIN_LCD_RW,  PIN_LCD_Ena,
                     PIN_LCD_DB4, PIN_LCD_DB5, PIN_LCD_DB6, PIN_LCD_DB7 );
#endif

Display  display;
Switch   pumpSwitch( PIN_PumpSwitch );
Switch   lampSwitch( PIN_LampSwitch );
Relay    pumpRelay(  PIN_PumpRelay );
Relay    lampRelay(  PIN_LampRelay );
Lumi     lumi(       PIN_Luminance );
Temp     temp;

Ctrl     ctrl( & display
              ,& pumpSwitch
              ,& lampSwitch
              ,& pumpRelay
              ,& lampRelay
              ,& lumi
              ,& temp );

void setup(void)
{
  pumpRelay.init();
  lampRelay.init();
  pumpSwitch.init();
  lampSwitch.init();

  DEBUG_EXPR( Serial.begin(9600) )
  DEBUG_EXPR( Serial.println( "Piscino " VERSION " - (c) Holger Galuschka" ) )

  lcd.begin( 16, 2 );  // 16 Zeichen / 2 Zeilen
  display.setup(    & ctrl, & lcd );

  pumpRelay.setup(  & ctrl, Display::NUM_PUMP );
  lampRelay.setup(  & ctrl, Display::NUM_LAMP );

  pumpSwitch.setup( & ctrl, Display::NUM_PUMP );
  lampSwitch.setup( & ctrl, Display::NUM_LAMP );

  lumi.setup(       & ctrl );        // lampRelay->autoOn() used, to switch lamp
  temp.setup(       & ctrl, & ow );  // pumpRelay->autoOn() used, to switch filter pump

  ctrl.restore();  // restore values from last backup (at dawn or driven manual by menu)

  wdt_enable( WDTO_250MS );
  wdt_reset();
}

void loop(void)
{
  static unsigned long usecOfNextSec =  0;  // micros(), when next second is expected

  wdt_reset();

  long diff = micros() - usecOfNextSec; // using micros to run max. about 2000 times on accidential value
  if (diff >= 0)
  {
    ++ctrl.sec;          // 0 in 1st loop!
    usecOfNextSec += 1 _M;  // in case we have been working more than 1 sec in current loop, we will do next task in very next loop()

#ifdef DEBUG
    if (! (ctrl.sec % 60 ))
      ctrl.minLoop();
#endif

    display.secLoop();   // fall back from menu to info / go off

    pumpRelay.secLoop(); // may fall back from "temporary on"
    lampRelay.secLoop(); // may fall back from "temporary on"

    ctrl.backup( lumi.secLoop() );  // read luminance (returns true on dusk and dawn)
  }

  temp.loop();

#ifdef KEYPAD
  ctrl.keypad = analogRead( 0 );
#if 0
  static int oldKey = 0x3ff;
  if (oldKey != ctrl.keypad) {
    oldKey = ctrl.keypad;
    Serial.print( "    new keypad resistor value: " );
    Serial.println( ctrl.keypad );
  }
#endif
#endif

  switch (pumpSwitch.loop()) {
    case Switch::NOTE_MENU:    display.toggleMode();                  break;
    case Switch::NOTE_KEY:     display.key( Display::NUM_PUMP );      break;
    case Switch::NOTE_SWMODE:  pumpRelay.swMode( pumpSwitch.mode() ); break;
    // pump switch may pressed somewhere else -> not to restart (?)
  }
  switch (lampSwitch.loop()) {
    case Switch::NOTE_MENU:    display.toggleMode();                  break;
    case Switch::NOTE_KEY:     display.key( Display::NUM_LAMP );      break;
    case Switch::NOTE_SWMODE:  lampRelay.swMode( lampSwitch.mode() ); break;
    case Switch::NOTE_TIMEOUT: display.restart();                     break;
  }
}
