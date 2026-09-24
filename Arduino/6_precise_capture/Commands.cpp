#include "Commands.h"

#include "Config.h"
#include "Sequence.h"
#include "Settings.h"


// ----------------------------------------------------------------------------
// SETTINGS TABLE
//
// GET, SET, HELP and the banner all read from this, so a setting's name,
// range and unit are written once.
// ----------------------------------------------------------------------------

struct Setting
{
  const char *name;
  uint32_t *value;
  uint32_t min;
  uint32_t max;
  const char *unit;
  const char *meaning;
};

static const Setting SETTINGS[] = {
  { "DELAY", &DelayUs, MIN_WINDOW_US, MAX_WINDOW_US, "us",
    "laser_confirm to shutter open" },
  { "CAPTURE", &CaptureUs, MIN_WINDOW_US, MAX_WINDOW_US, "us",
    "shutter gate width" },
  { "CYCLE_COUNT", &CycleCount, MIN_CYCLE_COUNT, MAX_CYCLE_COUNT, "",
    "cycles per START" },
  { "PULSE", &PulseUs, MIN_PULSE_US, MAX_PULSE_US, "us",
    "laser_signal low-pulse width" },
  { "CONFIRM_TIMEOUT", &ConfirmTimeoutMs, MIN_CONFIRM_TIMEOUT_MS,
    MAX_CONFIRM_TIMEOUT_MS, "ms", "confirm wait, 0 disables" },
};

static const uint8_t SETTING_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);


static const Setting *findSetting(const String &name)
{
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    if (name == SETTINGS[i].name) {
      return &SETTINGS[i];
    }
  }

  return nullptr;
}

// Only CONFIRM_TIMEOUT can be 0.
static String describeValue(const Setting &setting)
{
  if (*setting.value == 0) {
    return "0 (disabled)";
  }

  return String(*setting.value) + setting.unit;
}

static String describeRange(const Setting &setting)
{
  return String(setting.min) + "-" + setting.max;
}

static void printSetting(const Setting &setting)
{
  Serial.println(String("OK ") + setting.name + " " + describeValue(setting));
}


// Returns false unless the whole argument is a plain non-negative integer.
static bool parseUnsigned(String text, uint32_t &out)
{
  text.trim();

  if (text.length() == 0) {
    return false;
  }

  for (unsigned int i = 0; i < text.length(); i++) {
    if (!isDigit(text[i])) {
      return false;
    }
  }

  uint32_t parsed = text.toInt();

  // Catches overflow, e.g. "99999999999".
  if (text != String(parsed)) {
    return false;
  }

  out = parsed;

  return true;
}


static void handleGet(const String &name)
{
  const Setting *setting = findSetting(name);

  if (setting == nullptr) {
    Serial.println("ERROR UNKNOWN COMMAND");

    return;
  }

  printSetting(*setting);
}

static void handleSet(const String &arguments)
{
  int space = arguments.indexOf(' ');

  String name = space < 0 ? arguments : arguments.substring(0, space);
  String value = space < 0 ? String() : arguments.substring(space + 1);

  const Setting *setting = findSetting(name);

  if (setting == nullptr) {
    Serial.println("ERROR UNKNOWN COMMAND");

    return;
  }

  uint32_t parsed;

  if (!parseUnsigned(value, parsed)) {
    Serial.println(String("ERROR ") + setting->name + " VALUE");

    return;
  }

  if (parsed < setting->min || parsed > setting->max) {
    Serial.println(String("ERROR ") + setting->name + " RANGE " + describeRange(*setting));

    return;
  }

  *setting->value = parsed;

  printSetting(*setting);
}


static void handleStart()
{
  if (!cameraCapturing()) {
    Serial.println("ERROR CAMERA NOT CAPTURING");

    return;
  }

  Serial.println("STARTED");

  switch (runSequence()) {
    case RUN_DONE:
      Serial.println("DONE");
      break;

    case RUN_ABORTED:
      Serial.println("ABORTED");
      break;

    case RUN_CAMERA_NOT_CAPTURING:
      Serial.println("ERROR CAMERA NOT CAPTURING");
      break;

    case RUN_CONFIRM_TIMEOUT:
      Serial.println("ERROR LASER CONFIRM TIMEOUT");
      break;
  }
}

static void printStatus()
{
  Serial.println(String("OK STATUS CYCLE ") + CompletedCycles + "/" + CycleCount +
                 " CAMERA " + (cameraCapturing() ? "CAPTURING" : "IDLE"));
}


static void padTo(String &line, uint8_t column)
{
  while (line.length() < column) {
    line += ' ';
  }
}


// ----------------------------------------------------------------------------
// HELP
// ----------------------------------------------------------------------------

static const uint8_t HELP_RANGE_COLUMN = 28;
static const uint8_t HELP_MEANING_COLUMN = 42;

static void printHelpLine(const __FlashStringHelper *usage,
                          const __FlashStringHelper *meaning)
{
  String line = String("  ") + usage;

  padTo(line, HELP_RANGE_COLUMN);

  Serial.println(line + meaning);
}

static void printHelp()
{
  Serial.println(F("OK HELP"));

  printHelpLine(F("START"), F("run CYCLE_COUNT cycles"));
  printHelpLine(F("ABORT | STOP"), F("release all outputs"));
  printHelpLine(F("STATUS"), F("last run's progress, camera level"));
  printHelpLine(F("INFO"), F("reprint the boot banner"));
  printHelpLine(F("HELP | ?"), F("this list"));

  Serial.println();

  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    const Setting &setting = SETTINGS[i];

    String line = String("  SET ") + setting.name + " <" +
                  (setting.unit[0] ? setting.unit : "n") + ">";

    padTo(line, HELP_RANGE_COLUMN);
    line += describeRange(setting);
    padTo(line, HELP_MEANING_COLUMN);

    Serial.println(line + setting.meaning);
  }

  Serial.println();

  Serial.println(F("  GET reads back any setting, e.g. GET DELAY."));
  Serial.println(F("  Any input during a run aborts it."));

  Serial.println(F("OK HELP END"));
}


// ----------------------------------------------------------------------------
// BOOT BANNER
// ----------------------------------------------------------------------------

static const uint8_t BANNER_VALUE_COLUMN = 22;
static const uint8_t BANNER_NOTE_COLUMN = 27;

static void printBannerRow(const String &name, const String &value)
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

// Backslashes are doubled because these are C literals.
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
                 F("output, OC4A, idles HIGH, pulses LOW"));

  printBannerPin(F("laser_confirm"), LASER_CONFIRM_PIN,
                 LASER_CONFIRM_EDGE == RISING ? F("input, INT4 flag on RISING")
                                              : F("input, INT4 flag on FALLING"));

  printBannerPin(F("camera_capturing"), CAMERA_CAPTURING_PIN,
                 CAMERA_ACTIVE_LEVEL == HIGH ? F("input, active HIGH")
                                             : F("input, active LOW"));

  printBannerPin(F("shutter_enable"), SHUTTER_ENABLE_PIN,
                 F("output, OC1A, driven by Timer1"));

  Serial.println();

  Serial.println(F("  Settings"));

  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    printBannerRow(SETTINGS[i].name, describeValue(SETTINGS[i]));
  }

  Serial.println();

  Serial.println(F("  Inputs now"));

  printBannerRow(F("camera_capturing"), cameraCapturing() ? F("CAPTURING") : F("IDLE"));
  printBannerRow(F("laser_confirm"),
                 digitalRead(LASER_CONFIRM_PIN) == HIGH ? F("HIGH") : F("LOW"));

  Serial.println();

  Serial.println(F("  HELP lists the commands, STATUS reports the last run."));

  Serial.println(F("INFO END"));
}


static void processCommand(String command)
{
  command.trim();
  command.toUpperCase();

  if (command == "START") {
    handleStart();

    return;
  }

  if (command == "ABORT" || command == "STOP") {
    releaseOutputs();

    Serial.println("OK ABORTED");

    return;
  }

  if (command == "STATUS") {
    printStatus();

    return;
  }

  if (command == "HELP" || command == "?") {
    printHelp();

    return;
  }

  if (command == "INFO") {
    printBanner();

    return;
  }

  if (command.startsWith("GET ")) {
    handleGet(command.substring(4));

    return;
  }

  if (command.startsWith("SET ")) {
    handleSet(command.substring(4));

    return;
  }

  Serial.println("ERROR UNKNOWN COMMAND");
}


void readCommand()
{
  if (Serial.available() > 0) {
    processCommand(Serial.readStringUntil('\n'));
  }
}
