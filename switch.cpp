#include "ctrl.h"
#include "switch.h"
#include "display.h"

Switch::Switch( byte pinArg )
  : pin(        pinArg )
  , chatter(         0 )
  , phyStatus(       0 )
  , clnStatus(       0 )
  , logStatus(    AUTO )  // start in automatic mode
  , toggleCnt(       0 )
  , lastChatter( -1000 )  // "idle" even on 1st loop
  , lastToggle(  -1000 )  // "idle" even on 1st loop
  , other( 0 )
  , ctrl( 0 )
{
}

void Switch::init(void) // init PIN mode and switch off
{
  pinMode( pin, INPUT );      // sets the digital pin as input
}

void Switch::setup( Ctrl * ctrlArg, byte infonumArg )
{
  ctrl    = ctrlArg;
  infonum = infonumArg;

  switch (infonum)
  {
    case Display::NUM_PUMP:
      other = ctrl->lampSwitch;
#ifdef KEYPAD
      keymin =   0;
      keymax =  66;
#endif
      break;

    case Display::NUM_LAMP:
      other = ctrl->pumpSwitch;
#ifdef KEYPAD
      keymin = 389;
      keymax = 596;
#endif
      break;
  }
}

byte Switch::loop()
{
#ifdef KEYPAD
  byte const val = ((596 <= ctrl->keypad) && (ctrl->keypad <= 860)) || ((keymin <= ctrl->keypad) && (ctrl->keypad <= keymax));
#else
  byte const val = digitalRead( pin ) & 1;   // read the input pin
#endif
  long const now = millis();

  if (phyStatus == val)
  {
    if (chatter)
    {
      long const chatterdiff = now - lastChatter;
      if (chatterdiff < 50)
        return NOTE_NOTHING;

      // more than 50ms stable: chatter gone
      chatter = 0;
      if (clnStatus != val) { // stable state other than how chatter begun
        lastToggle = lastChatter;
        return toggleDetect( val );  // we didn't do that!
      }
      return NOTE_NOTHING;
    }

    if (val || ctrl->display->menu())
      return NOTE_NOTHING;  // stable on is not for further interest (all done)

    // check long pause in info mode to check the toggleCnt action

    long const diff = now - lastToggle;

    if ((diff >= 1000) && toggleCnt)  // 1sec stable off
    {
      // -> set logical status
      byte newStatus = 0xff;  // not coded -> do nothing
      switch (toggleCnt) {
        case TOGGLE_OFF:  newStatus = OFF;  break;
        case TOGGLE_ON:   newStatus = ON;   break;
        case TOGGLE_AUTO: newStatus = AUTO; break;
        case TOGGLE_TEMP: newStatus = TEMP; break;
      }
      toggleCnt = 0; // avoid further checks (1 toggle and 6 or more toggles will be ignored)

      if ((newStatus != 0xff) && (logStatus != newStatus)) {
        logStatus = newStatus;
        return NOTE_SWMODE;
      }
    }
    return NOTE_NOTHING;
  }

  // here we detected change of physical status -> purge chatter

  long const diff = now - lastToggle;

  if (diff < 50) {  // inside 50ms, we do not toggleDetect!
    chatter = 1;
    lastChatter = now;
    phyStatus = val;
    return NOTE_NOTHING; // no action while chatter
  }

  chatter = 0;
  lastToggle = now;
  phyStatus = val;
  if (val && (diff >= 1000))
    toggleCnt = 0; // restart counting, when pause was 1 sec or more
  return toggleDetect( val );
}

byte Switch::toggleDetect( byte const val )
{
  clnStatus = val;
  byte note = NOTE_NOTHING;
  byte currMenuMode = ctrl->display->menu();
  if (val)
  {
    if (other->clnStatus)
      note = NOTE_MENU;
    else if (currMenuMode)
      note = NOTE_KEY;
    else
      note = NOTE_TIMEOUT;
  } else {
    if (menuMode || currMenuMode)  // changed while key pressed or in menu - not to count
      toggleCnt = 0;
    else if (toggleCnt < TOGGLE_MAX) // <1sec stable: count High->Low transitions
      ++toggleCnt;
  }
  menuMode = currMenuMode;
  return note;
}

byte Switch::mode(void)
{
  return logStatus;
}

boolean Switch::idle(void)
{
  return ! (clnStatus || phyStatus || toggleCnt);
}
