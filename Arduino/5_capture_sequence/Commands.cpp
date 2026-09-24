#include "Commands.h"

#include "Config.h"
#include "LogRing.h"
#include "Sequence.h"
#include "Settings.h"
#include "Timer1.h"


// ----------------------------------------------------------------------------
// SETTING READBACKS
//
// Shared by the GET commands and by the SET responses, so a write always
// echoes in exactly the format a read would produce.
// ----------------------------------------------------------------------------

static String describeDuration(const char *label, const Duration &d)
{
  return String("OK ") + label + " " + durationDetail(d);
}

static String getDelayResponseString()
{
  return describeDuration("DELAY", DelayTime);
}

static String getCaptureResponseString()
{
  return describeDuration("CAPTURE", CaptureTime);
}

static String getCycleCountResponseString()
{
  return String("OK CYCLE_COUNT ") + CycleCount;
}

static String getPulseResponseString()
{
  return String("OK PULSE ") + LaserSignalPulseUs + "us";
}

static String getVerboseResponseString()
{
  return String("OK VERBOSE ") + (VerboseEnabled ? 1 : 0);
}

static String getConfirmTimeoutResponseString()
{
  if (ConfirmTimeoutMs == 0) {
    return String("OK CONFIRM_TIMEOUT 0 (disabled)");
  }

  return String("OK CONFIRM_TIMEOUT ") + ConfirmTimeoutMs + "ms";
}


// Only the commands that take an argument need a constant, since those are
// matched with startsWith() and the length is used to slice the argument off.
// The bare verbs are compared inline in processCommand().

static const String GET_DELAY_COMMAND = "GET DELAY";
static const String SET_DELAY_COMMAND = "SET DELAY";

static const String GET_CAPTURE_COMMAND = "GET CAPTURE";
static const String SET_CAPTURE_COMMAND = "SET CAPTURE";

static const String GET_CYCLE_COUNT_COMMAND = "GET CYCLE_COUNT";
static const String SET_CYCLE_COUNT_COMMAND = "SET CYCLE_COUNT";

static const String GET_PULSE_COMMAND = "GET PULSE";
static const String SET_PULSE_COMMAND = "SET PULSE";

static const String GET_CONFIRM_TIMEOUT_COMMAND = "GET CONFIRM_TIMEOUT";
static const String SET_CONFIRM_TIMEOUT_COMMAND = "SET CONFIRM_TIMEOUT";

static const String GET_VERBOSE_COMMAND = "GET VERBOSE";
static const String SET_VERBOSE_COMMAND = "SET VERBOSE";


// Returns false unless the whole argument is a plain non-negative integer.
static bool parseUnsigned(String value, unsigned long &out)
{
  value.trim();

  if (value.length() == 0) {
    return false;
  }

  for (unsigned int i = 0; i < value.length(); i++) {
    if (!isDigit(value[i])) {
      return false;
    }
  }

  unsigned long parsed = value.toInt();

  // Rejects anything that overflowed toInt(), e.g. "99999999999".
  if (value != String(parsed)) {
    return false;
  }

  out = parsed;

  return true;
}


static void handleSetDuration(const String &argument,
                              const char *label,
                              Duration &target,
                              String (*describe)())
{
  unsigned long value;

  if (!parseUnsigned(argument, value)) {
    Serial.println(String("ERROR ") + label + " VALUE");

    return;
  }

  Duration resolved;

  if (!resolveDuration(value, resolved)) {
    Serial.println(String("ERROR ") + label + " RANGE " + MIN_US + "-" + MAX_US + "us");

    return;
  }

  target = resolved;

  Serial.println(describe());
}


// Reports the failure itself and returns false; on success the value is in out
// and the caller stores it wherever it belongs.
static bool parseScalarArg(const String &argument,
                           const char *label,
                           unsigned long minValue,
                           unsigned long maxValue,
                           unsigned long &out)
{
  unsigned long value;

  if (!parseUnsigned(argument, value)) {
    Serial.println(String("ERROR ") + label + " VALUE");

    return false;
  }

  if (value < minValue || value > maxValue) {
    Serial.println(String("ERROR ") + label + " RANGE " + minValue + "-" + maxValue);

    return false;
  }

  out = value;

  return true;
}


// Shared by the HELP block and the banner, which is the only reason it is not
// local to one of them.
static void padTo(String &line, uint8_t column)
{
  while (line.length() < column) {
    line += ' ';
  }
}


// ----------------------------------------------------------------------------
// HELP
//
// Lists every command the parser accepts.
//
// Body lines are indented and the block is bracketed by OK HELP and
// OK HELP END, so a host reading line by line can swallow the whole thing
// without having to recognise each entry.
//
// The limits are built from the constants rather than written into the text,
// so they cannot drift out of step with what the parser actually enforces.
//
// The text lives in flash via F(). It is about 700 bytes, which would be a
// tenth of the Mega's SRAM if it sat in RAM with the String work below.
// ----------------------------------------------------------------------------

static const uint8_t HELP_TEXT_COLUMN = 28;
static const uint8_t HELP_DESCRIPTION_COLUMN = 42;

static void printHelpLine(const __FlashStringHelper *usage,
                          const __FlashStringHelper *text)
{
  String line = String("  ") + usage;

  padTo(line, HELP_TEXT_COLUMN);

  Serial.println(line + text);
}

static void printHelpField(const __FlashStringHelper *usage,
                           const String &limits,
                           const __FlashStringHelper *description)
{
  String line = String("  ") + usage;

  padTo(line, HELP_TEXT_COLUMN);

  line += limits;

  padTo(line, HELP_DESCRIPTION_COLUMN);

  Serial.println(line + description);
}

static void printHelp()
{
  Serial.println(F("OK HELP"));

  printHelpLine(F("START"), F("run CYCLE_COUNT cycles"));
  printHelpLine(F("ABORT | STOP"), F("stop, drop laser_enable, free shutter"));
  printHelpLine(F("STATUS"), F("state, cycle progress, camera level"));
  printHelpLine(F("INFO"), F("reprint the boot banner"));
  printHelpLine(F("HELP | ?"), F("this list"));

  Serial.println();

  printHelpField(F("SET DELAY <us>"),
                 String(MIN_US) + "-" + MAX_US + "us",
                 F("laser_confirm to shutter open"));

  printHelpField(F("SET CAPTURE <us>"),
                 String(MIN_US) + "-" + MAX_US + "us",
                 F("shutter gate width"));

  printHelpField(F("SET CYCLE_COUNT <n>"),
                 String(MIN_CYCLE_COUNT) + "-" + MAX_CYCLE_COUNT,
                 F("cycles per START"));

  printHelpField(F("SET PULSE <us>"),
                 String(MIN_PULSE_US) + "-" + MAX_PULSE_US + "us",
                 F("laser_signal low-pulse width"));

  printHelpField(F("SET CONFIRM_TIMEOUT <ms>"),
                 String(MIN_CONFIRM_TIMEOUT_MS) + "-" + MAX_CONFIRM_TIMEOUT_MS + "ms",
                 F("confirm wait, 0 disables"));

  printHelpField(F("SET VERBOSE <0|1>"),
                 String("0-1"),
                 F("measured event log"));

  Serial.println();

  Serial.println(F("  GET reads back any of the six settings, e.g. GET DELAY."));
  Serial.println(F("  SET, HELP and INFO are rejected with ERROR BUSY while running."));

  Serial.println(F("OK HELP END"));
}


// ----------------------------------------------------------------------------
// BOOT BANNER
//
// Printed once at startup, and again on demand via INFO. Answers the three
// questions asked at the start of every bring-up session: what is running on
// this board, how is it wired, and what is it currently set to.
//
// Bracketed by INFO and INFO END on the same principle as the HELP block, so
// a host reading line by line can swallow the whole thing without recognising
// each row. READY still follows on boot and is still the last line printed,
// so an existing host that waits for READY is unaffected by any of this.
//
// The banner is deliberately not the boot marker. READY is. That keeps INFO
// honest when it reprints the same block mid-session, and it means a stray
// READY still tells a host the board reset under it.
//
// Text is held in flash with F(). The values are what force the String work,
// and they are transient.
// ----------------------------------------------------------------------------

static const uint8_t BANNER_VALUE_COLUMN = 22;
static const uint8_t BANNER_NOTE_COLUMN = 27;

static void printBannerRow(const __FlashStringHelper *name, const String &value)
{
  String line = String("    ") + name;

  padTo(line, BANNER_VALUE_COLUMN);

  Serial.println(line + value);
}

static void printBannerPin(const __FlashStringHelper *name,
                           uint8_t pin,
                           const __FlashStringHelper *note)
{
  String line = String("    ") + name;

  padTo(line, BANNER_VALUE_COLUMN);

  line += pin;

  padTo(line, BANNER_NOTE_COLUMN);

  Serial.println(line + note);
}

// Drawn column by column around a centre line, so the tube, the objective and
// the slide stack up. Backslashes are doubled because these are C literals.
static void printMicroscope()
{
  Serial.println(F("              ___"));
  Serial.println(F("             |[_]|"));
  Serial.println(F("             |   |"));
  Serial.println(F("             | | |"));
  Serial.println(F("            _|___|_"));
  Serial.println(F("           |       |"));
  Serial.println(F("            \\_____/"));
  Serial.println(F("             \\___/"));
  Serial.println(F("        _______________"));
  Serial.println(F("       |  [=========]  |"));
  Serial.println(F("       |_______________|"));
  Serial.println(F("              | |"));
  Serial.println(F("         _____|_|_____"));
  Serial.println(F("        /             \\"));
  Serial.println(F("       /_______________\\"));
}

void printBanner()
{
  Serial.println(F("INFO"));

  printMicroscope();

  Serial.println();

  Serial.println(String("  ") + FLASH_STR(FIRMWARE_NAME));

  Serial.println(String("  version ") + FLASH_STR(FIRMWARE_VERSION) +
                 "   built " + FLASH_STR(BUILD_STAMP));

  Serial.println(String("  Arduino Mega 2560   serial ") + SERIAL_BAUD + " 8N1");

  Serial.println();

  Serial.println(F("  Pins"));

  printBannerPin(F("laser_enable"), LASER_ENABLE_PIN,
                 F("output, held HIGH for the run"));

  printBannerPin(F("laser_signal"), LASER_SIGNAL_PIN,
                 F("output, idles HIGH, pulses LOW"));

  printBannerPin(F("laser_confirm"), LASER_CONFIRM_PIN,
                 F("input, interrupt on RISING"));

  printBannerPin(F("camera_capturing"), CAMERA_CAPTURING_PIN,
                 F("input, active HIGH"));

  printBannerPin(F("shutter_enable"), SHUTTER_ENABLE_PIN,
                 F("output, OC1A, driven by Timer1"));

  Serial.println();

  Serial.println(F("  Settings"));

  printBannerRow(F("DELAY"), durationDetail(DelayTime));
  printBannerRow(F("CAPTURE"), durationDetail(CaptureTime));
  printBannerRow(F("CYCLE_COUNT"), String(CycleCount));
  printBannerRow(F("PULSE"), String(LaserSignalPulseUs) + "us");

  printBannerRow(F("CONFIRM_TIMEOUT"),
                 ConfirmTimeoutMs == 0 ? String("0 (disabled)")
                                       : String(ConfirmTimeoutMs) + "ms");

  printBannerRow(F("VERBOSE"), VerboseEnabled ? F("1 (event log on)") : F("0"));

  Serial.println();

  // Read live rather than assumed. A miswired or idle camera shows up here at
  // boot instead of as an ERROR CAMERA NOT CAPTURING on the first START.
  Serial.println(F("  Inputs now"));

  printBannerRow(F("camera_capturing"),
                 digitalRead(CAMERA_CAPTURING_PIN) == CAMERA_ACTIVE_LEVEL
                     ? F("CAPTURING") : F("IDLE"));

  printBannerRow(F("laser_confirm"),
                 digitalRead(LASER_CONFIRM_PIN) == HIGH ? F("HIGH") : F("LOW"));

  Serial.println();

  Serial.println(F("  HELP lists the commands, STATUS reports live state."));

  Serial.println(F("INFO END"));
}


void processCommand(String command)
{
  command.trim();
  command.toUpperCase();

  if (command == "START") {
    startSequence();

    return;
  }

  // Not in the spec, but a laser controller needs a way to stop.
  if (command == "ABORT" || command == "STOP") {
    abortSequence();

    Serial.println("OK ABORTED");

    return;
  }

  if (command == "STATUS") {
    Serial.print("OK STATUS ");
    Serial.print(isRunning() ? "RUNNING" : (State == COMPLETE ? "COMPLETE" : "IDLE"));
    Serial.print(" CYCLE ");
    Serial.print(CurrentCycle);
    Serial.print("/");
    Serial.print(CycleCount);
    Serial.print(" CAMERA ");
    Serial.println(
        digitalRead(CAMERA_CAPTURING_PIN) == CAMERA_ACTIVE_LEVEL ? "CAPTURING" : "IDLE");

    return;
  }

  // HELP and INFO are rejected while running for a different reason than SET
  // is. Each block is several hundred bytes, which is most of a second at 9600
  // baud once the 64 byte transmit buffer backs up, and loop() is what starts
  // each next cycle. Blocking it that long would stretch the gap between them.
  if (command == "HELP" || command == "?") {
    if (isRunning()) {
      Serial.println("ERROR BUSY");

      return;
    }

    printHelp();

    return;
  }

  // A host that opened the port after boot missed the banner, which is the
  // common case, so it has to be askable for.
  if (command == "INFO") {
    if (isRunning()) {
      Serial.println("ERROR BUSY");

      return;
    }

    printBanner();

    return;
  }

  if (command == GET_DELAY_COMMAND) {
    Serial.println(getDelayResponseString());

    return;
  }

  if (command == GET_CAPTURE_COMMAND) {
    Serial.println(getCaptureResponseString());

    return;
  }

  if (command == GET_CYCLE_COUNT_COMMAND) {
    Serial.println(getCycleCountResponseString());

    return;
  }

  if (command == GET_PULSE_COMMAND) {
    Serial.println(getPulseResponseString());

    return;
  }

  if (command == GET_CONFIRM_TIMEOUT_COMMAND) {
    Serial.println(getConfirmTimeoutResponseString());

    return;
  }

  if (command == GET_VERBOSE_COMMAND) {
    Serial.println(getVerboseResponseString());

    return;
  }

  // Rejecting SET while running is what makes it safe for the Timer1 ISR to
  // read the settings without volatile or interrupt guards; see Settings.h.
  if (command.startsWith("SET ")) {
    if (isRunning()) {
      Serial.println("ERROR BUSY");

      return;
    }

    if (command.startsWith(SET_DELAY_COMMAND)) {
      handleSetDuration(command.substring(SET_DELAY_COMMAND.length()),
                        "DELAY", DelayTime, getDelayResponseString);

      return;
    }

    if (command.startsWith(SET_CAPTURE_COMMAND)) {
      handleSetDuration(command.substring(SET_CAPTURE_COMMAND.length()),
                        "CAPTURE", CaptureTime, getCaptureResponseString);

      return;
    }

    if (command.startsWith(SET_CYCLE_COUNT_COMMAND)) {
      unsigned long value;

      if (parseScalarArg(command.substring(SET_CYCLE_COUNT_COMMAND.length()),
                         "CYCLE_COUNT", MIN_CYCLE_COUNT, MAX_CYCLE_COUNT, value)) {
        CycleCount = (uint16_t)value;

        Serial.println(getCycleCountResponseString());
      }

      return;
    }

    if (command.startsWith(SET_PULSE_COMMAND)) {
      unsigned long value;

      if (parseScalarArg(command.substring(SET_PULSE_COMMAND.length()),
                         "PULSE", MIN_PULSE_US, MAX_PULSE_US, value)) {
        LaserSignalPulseUs = value;

        Serial.println(getPulseResponseString());
      }

      return;
    }

    if (command.startsWith(SET_CONFIRM_TIMEOUT_COMMAND)) {
      unsigned long value;

      if (parseScalarArg(command.substring(SET_CONFIRM_TIMEOUT_COMMAND.length()),
                         "CONFIRM_TIMEOUT",
                         MIN_CONFIRM_TIMEOUT_MS, MAX_CONFIRM_TIMEOUT_MS, value)) {
        ConfirmTimeoutMs = value;

        Serial.println(getConfirmTimeoutResponseString());
      }

      return;
    }

    if (command.startsWith(SET_VERBOSE_COMMAND)) {
      unsigned long value;

      if (parseScalarArg(command.substring(SET_VERBOSE_COMMAND.length()),
                         "VERBOSE", 0, 1, value)) {
        VerboseEnabled = (value != 0);

        // Drop anything queued under the previous setting so timestamps in
        // the log always belong to the run being watched.
        resetLog();

        Serial.println(getVerboseResponseString());
      }

      return;
    }
  }

  Serial.println("ERROR UNKNOWN COMMAND");
}


void readCommand()
{
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');

    processCommand(command);
  }
}
