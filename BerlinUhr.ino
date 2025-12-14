/****************************************************************************
 *
 *  BerlinUhr with ESP8266 + WS2812 + WiFi-Setup + NTP (nightly) + LDR dimming
 *
 *  Time handling (Option B: own DST calculation):
 *  - NTP delivers UTC
 *  - Internal RTC runs in UTC (rtcBaseTime + millis)
 *  - Display time:
 *      - Winter: UTC + 1h
 *      - Summer (EU rule): UTC + 2h
 *  - Nightly NTP resync is scheduled by scanning for the next local
 *    display time-of-day (robust across DST transitions).
 *
 *  Order of WS2812B LEDs
 *
 *  1. Row:   each LED 1min     --> LED[0-3]
 *  2. Row:   each LED 5min     --> LED[4-14]
 *  3. Row:   each LED 1 hour   --> LED[15-18]
 *  4. Row:   each LED 5 hours  --> LED[19-22]
 *  5. Row:   Second (fade)     --> LED[23]
 *
 ****************************************************************************/

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include <math.h>

// -------------------------- Hardware config ---------------------------------

#define LED_PIN           D3
#define LED_COUNT         24
#define LDR_PIN           A0

#define SECOND_LED        23

// -------------------------- NeoPixel setup ----------------------------------

Adafruit_NeoPixel strip
(
  LED_COUNT,
  LED_PIN,
  NEO_GRB + NEO_KHZ800
);

// -------------------------- Color defines -----------------------------------

enum
{
  dRED = 0,
  dGREEN,
  dBLUE
};

uint8_t colorDefines[LED_COUNT][3];

// -------------------------- State machine -----------------------------------

typedef enum
{
  STATE_WIFI_CONFIG = 0,
  STATE_WAIT_FOR_TIME,
  STATE_SHOW_CLOCK
} ClockState_t;

ClockState_t myState = STATE_WIFI_CONFIG;

// -------------------------- WiFi / NTP --------------------------------------

WiFiManager wifiManager;

// Nightly NTP sync time (LOCAL display time)
const int ntpSyncHour = 3;
const int ntpSyncMinute = 5;

// Wait for NTP validity during a sync attempt
const unsigned long ntpWaitTimeoutMs = 20000;

// Internal RTC base (UTC)
time_t rtcBaseTime = 0;
unsigned long rtcBaseMillis = 0;

// Next scheduled nightly sync moment (UTC epoch)
time_t nextNtpSyncEpoch = 0;

// Sync attempt tracking
bool ntpSyncInProgress = false;
unsigned long ntpSyncStartMs = 0;

// WiFi supervision
unsigned long wifiLostSinceMs = 0;
const unsigned long wifiLostTimeoutMs = 60000;

// -------------------------- Clock variables ---------------------------------

volatile int t_sec = 0;
volatile int t_min = 0;
volatile int t_hour = 0;

// -------------------------- Timing ------------------------------------------

// Smooth seconds fade needs a higher update rate
unsigned long lastRenderMillis = 0;
const unsigned long renderIntervalMs = 20;

unsigned long lastBrightnessMillis = 0;
const unsigned long brightnessIntervalMs = 1000;

// -------------------------- Brightness / LDR --------------------------------

uint8_t currentBrightness = 50;
const uint8_t minBrightness = 5;
const uint8_t maxBrightness = 200;

// -------------------------- Color constants ---------------------------------

const uint8_t COLOR_RED[3]     = {255, 0, 0};
const uint8_t COLOR_GREEN[3]   = {0, 255, 0};
const uint8_t COLOR_BLUE[3]    = {0, 0, 255};
const uint8_t COLOR_MAGENTA[3] = {255, 0, 255};
const uint8_t COLOR_YELLOW[3]  = {255, 255, 0};
const uint8_t COLOR_WHITE[3]   = {255, 255, 255};

// Minutes 5-row indices
const uint8_t MIN5_START_LED = 4;
const uint8_t MIN5_LED_COUNT = 11;

// Quarter positions in 5-minute row (15 / 30 / 45)
const uint8_t MIN5_QUARTER_1 = 2;   // 15 min
const uint8_t MIN5_QUARTER_2 = 5;   // 30 min
const uint8_t MIN5_QUARTER_3 = 8;   // 45 min


// -------------------------- Function declarations ---------------------------

/***************** initColorDefines *******************************************
 * params: none
 * return: void
 * Description:
 * Initialize the base RGB colors for all LEDs (rows of the Berlin clock).
 ******************************************************************************/
void initColorDefines();

/***************** setupWifi ***************************************************
 * params: none
 * return: void
 * Description:
 * Robust WiFi init and WiFiManager autoconnect.
 ******************************************************************************/
void setupWifi();

/***************** startNtp ****************************************************
 * params: none
 * return: void
 * Description:
 * Starts NTP sync (UTC).
 ******************************************************************************/
void startNtp();

/***************** isTimeValid *************************************************
 * params: none
 * return: bool
 * Description:
 * Checks if system time is valid (after a defined epoch).
 ******************************************************************************/
bool isTimeValid();

/***************** syncRtcFromSystemTime ***************************************
 * params: none
 * return: bool
 * Description:
 * Copies system time (time(nullptr), UTC) into internal RTC base.
 ******************************************************************************/
bool syncRtcFromSystemTime();

/***************** getCurrentRtcTime *******************************************
 * params: none
 * return: time_t
 * Description:
 * Returns internal RTC time: rtcBaseTime + millis delta (UTC).
 ******************************************************************************/
time_t getCurrentRtcTime();

/***************** lastSunday **************************************************
 * params: year, month
 * return: int
 * Description:
 * Returns day-of-month of the last Sunday in given month (1..31).
 ******************************************************************************/
int lastSunday(int year, int month);

/***************** isDstEurope *************************************************
 * params: utc
 * return: bool
 * Description:
 * EU DST rule (CET/CEST):
 * - starts last Sunday in March at 01:00 UTC
 * - ends   last Sunday in October at 01:00 UTC
 ******************************************************************************/
bool isDstEurope(time_t utc);

/***************** getUtcOffsetSeconds *****************************************
 * params: utc
 * return: int32_t
 * Description:
 * Returns UTC offset in seconds: 3600 winter, 7200 summer (EU rule).
 ******************************************************************************/
int32_t getUtcOffsetSeconds(time_t utc);

/***************** getDisplayTime **********************************************
 * params: utc
 * return: time_t
 * Description:
 * Converts UTC epoch to display epoch (UTC + offset, offset depends on DST).
 ******************************************************************************/
time_t getDisplayTime(time_t utc);

/***************** scheduleNextNightlySync *************************************
 * params: currentUtc
 * return: void
 * Description:
 * Finds the next UTC epoch where the display time equals ntpSyncHour:ntpSyncMinute:00.
 * Uses a bounded scan (max 48h) for robustness across DST transitions.
 ******************************************************************************/
void scheduleNextNightlySync(time_t currentUtc);

/***************** updateTimeFromRtc *******************************************
 * params: none
 * return: void
 * Description:
 * Updates t_hour/t_min/t_sec from internal RTC using display time (UTC+offset).
 ******************************************************************************/
void updateTimeFromRtc();

/***************** shouldStartNightlySync **************************************
 * params: currentUtc
 * return: bool
 * Description:
 * Returns true if current time has passed nextNtpSyncEpoch.
 ******************************************************************************/
bool shouldStartNightlySync(time_t currentUtc);

/***************** superviseWiFi ***********************************************
 * params: nowMs
 * return: void
 * Description:
 * Keeps WiFi stable; only return to WIFI_CONFIG if WiFi is gone for long time.
 ******************************************************************************/
void superviseWiFi(unsigned long nowMs);

/***************** updateBrightnessFromLdr ************************************
 * params: none
 * return: void
 * Description:
 * Reads LDR and updates currentBrightness.
 ******************************************************************************/
void updateBrightnessFromLdr();

/***************** gamma8 ******************************************************
 * params: inValue
 * return: uint8_t
 * Description:
 * Simple gamma correction for smoother perceived fades.
 ******************************************************************************/
uint8_t gamma8(uint8_t inValue);

/***************** renderBerlinClock *******************************************
 * params: nowMs
 * return: void
 * Description:
 * Renders Berlin clock using LED mapping and colorDefines.
 ******************************************************************************/
void renderBerlinClock(unsigned long nowMs);

/***************** showStaticRainbow *******************************************
 * params: none
 * return: void
 * Description:
 * Fills LEDs with a static rainbow (for config state).
 ******************************************************************************/
void showStaticRainbow();

// -------------------------- Setup / Loop ------------------------------------

/***************** setup ******************************************************
 * params: none
 * return: void
 * Description:
 * Initializes serial, LEDs and starts state machine.
 ******************************************************************************/
void setup()
{
  Serial.begin(115200);
  delay(200);

  Serial.println("BerlinUhr booting...");

  strip.begin();
  strip.show();
  strip.setBrightness(currentBrightness);

  initColorDefines();

  myState = STATE_WIFI_CONFIG;
}

/***************** loop *******************************************************
 * params: none
 * return: void
 * Description:
 * State machine:
 * - WIFI_CONFIG: connect WiFi, start NTP once
 * - WAIT_FOR_TIME: wait until NTP valid, sync internal RTC, schedule nightly sync
 * - SHOW_CLOCK: run from internal RTC; nightly NTP sync once per day
 ******************************************************************************/
void loop()
{
  unsigned long nowMs = millis();

  if (nowMs - lastBrightnessMillis >= brightnessIntervalMs)
  {
    lastBrightnessMillis = nowMs;
    updateBrightnessFromLdr();
  }

  switch (myState)
  {
    case STATE_WIFI_CONFIG:
    {
      Serial.println("STATE: WIFI CONFIG (RAINBOW ACTIVE)");
      showStaticRainbow();

      setupWifi();

      startNtp();
      ntpSyncInProgress = true;
      ntpSyncStartMs = millis();

      Serial.print("SSID after connect: ");
      Serial.println(WiFi.SSID());
      WiFi.printDiag(Serial);

      Serial.println("STATE: WAIT FOR NTP TIME");
      myState = STATE_WAIT_FOR_TIME;
      break;
    }

    case STATE_WAIT_FOR_TIME:
    {
      if (ntpSyncInProgress && ((millis() - ntpSyncStartMs) > ntpWaitTimeoutMs))
      {
        Serial.println("NTP wait timeout -> back to WIFI CONFIG");
        ntpSyncInProgress = false;
        myState = STATE_WIFI_CONFIG;
        break;
      }

      if (isTimeValid())
      {
        Serial.println("NTP time received.");

        if (!syncRtcFromSystemTime())
        {
          Serial.println("RTC update from NTP failed -> WIFI CONFIG");
          ntpSyncInProgress = false;
          myState = STATE_WIFI_CONFIG;
          break;
        }

        ntpSyncInProgress = false;

        time_t nowUtc = getCurrentRtcTime();
        time_t disp = getDisplayTime(nowUtc);
        struct tm *u = gmtime(&nowUtc);
        struct tm *d = gmtime(&disp);

        Serial.print("UTC hour: ");
        Serial.println(u != nullptr ? u->tm_hour : -1);

        Serial.print("Display hour: ");
        Serial.println(d != nullptr ? d->tm_hour : -1);

        Serial.print("DST active: ");
        Serial.println(isDstEurope(nowUtc) ? "YES" : "NO");

        Serial.println("STATE: SHOW CLOCK");
        myState = STATE_SHOW_CLOCK;
      }

      superviseWiFi(nowMs);
      break;
    }

    case STATE_SHOW_CLOCK:
    {
      if ((nowMs - lastRenderMillis) >= renderIntervalMs)
      {
        lastRenderMillis = nowMs;

        updateTimeFromRtc();
        renderBerlinClock(nowMs);
      }

      time_t currentUtc = getCurrentRtcTime();

      if (shouldStartNightlySync(currentUtc))
      {
        Serial.println("Nightly NTP sync due -> WAIT_FOR_TIME");

        if (WiFi.status() != WL_CONNECTED)
        {
          Serial.println("WiFi not connected -> reconnect");
          WiFi.mode(WIFI_STA);
          WiFi.begin();
        }

        startNtp();
        ntpSyncInProgress = true;
        ntpSyncStartMs = millis();

        myState = STATE_WAIT_FOR_TIME;
      }

      superviseWiFi(nowMs);
      break;
    }

    default:
    {
      myState = STATE_WIFI_CONFIG;
      break;
    }
  }
}

// -------------------------- Implementation ----------------------------------

/***************** initColorDefines *******************************************/
void initColorDefines()
{
  // minutes 1 (0..3) - red
  for (uint8_t i = 0; i < 4; i++)
  {
    colorDefines[i][dRED] = COLOR_RED[dRED];
    colorDefines[i][dGREEN] = COLOR_RED[dGREEN];
    colorDefines[i][dBLUE] = COLOR_RED[dBLUE];
  }

  // minutes 5 (4..14)
  for (uint8_t i = 0; i < MIN5_LED_COUNT; i++)
  {
    uint8_t ledIndex = MIN5_START_LED + i;

    // Quarter markers: 15 / 30 / 45 minutes -> red
    if ((i == MIN5_QUARTER_1) ||
        (i == MIN5_QUARTER_2) ||
        (i == MIN5_QUARTER_3))
    {
      colorDefines[ledIndex][dRED]   = COLOR_YELLOW[dRED];
      colorDefines[ledIndex][dGREEN] = COLOR_YELLOW[dGREEN];
      colorDefines[ledIndex][dBLUE]  = COLOR_YELLOW[dBLUE];
    }
    else
    {
      colorDefines[ledIndex][dRED]   = COLOR_GREEN[dRED];
      colorDefines[ledIndex][dGREEN] = COLOR_GREEN[dGREEN];
      colorDefines[ledIndex][dBLUE]  = COLOR_GREEN[dBLUE];
    }
  }

  // hours 1 (15..18) - blue
  for (uint8_t i = 15; i < 19; i++)
  {
    colorDefines[i][dRED] = COLOR_BLUE[dRED];
    colorDefines[i][dGREEN] = COLOR_BLUE[dGREEN];
    colorDefines[i][dBLUE] = COLOR_BLUE[dBLUE];
  }

  // hours 5 (19..22) - magenta
  for (uint8_t i = 19; i < 23; i++)
  {
    colorDefines[i][dRED] = COLOR_MAGENTA[dRED];
    colorDefines[i][dGREEN] = COLOR_MAGENTA[dGREEN];
    colorDefines[i][dBLUE] = COLOR_MAGENTA[dBLUE];
  }

  // seconds LED (23) - white
  colorDefines[SECOND_LED][dRED] = COLOR_WHITE[dRED];
  colorDefines[SECOND_LED][dGREEN] = COLOR_WHITE[dGREEN];
  colorDefines[SECOND_LED][dBLUE] = COLOR_WHITE[dBLUE];
}

/***************** setupWifi ***************************************************/
void setupWifi()
{
  Serial.println("Starting WiFiManager (BLOCKING)");

  WiFi.persistent(true);

  WiFi.mode(WIFI_STA);
  delay(200);

  Serial.print("SSID after reset: ");
  Serial.println(WiFi.SSID());
  WiFi.printDiag(Serial);

  WiFi.setAutoReconnect(true);

  wifiManager.setDebugOutput(true);
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setConfigPortalTimeout(0);

  wifiManager.setConnectTimeout(20);
  wifiManager.setConnectRetries(3);

  if (!wifiManager.autoConnect("BerlinUhr-Setup"))
  {
    Serial.println("autoConnect failed -> start portal");
    wifiManager.startConfigPortal("BerlinUhr-Setup");
  }

  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

/***************** startNtp ****************************************************/
void startNtp()
{
  // Always request UTC
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

/***************** isTimeValid *************************************************/
bool isTimeValid()
{
  time_t now = time(nullptr);

  // valid after 2016-01-01
  if (now < 1451606400)
  {
    return false;
  }

  return true;
}

/***************** syncRtcFromSystemTime ***************************************/
bool syncRtcFromSystemTime()
{
  time_t now = time(nullptr);
  if (now < 1451606400)
  {
    return false;
  }

  rtcBaseTime = now;
  rtcBaseMillis = millis();

  scheduleNextNightlySync(now);

  Serial.print("Next nightly NTP sync (UTC epoch): ");
  Serial.println((unsigned long)nextNtpSyncEpoch);

  return true;
}

/***************** getCurrentRtcTime *******************************************/
time_t getCurrentRtcTime()
{
  if (rtcBaseTime == 0)
  {
    return time(nullptr);
  }

  unsigned long elapsedMs = millis() - rtcBaseMillis;
  return rtcBaseTime + (elapsedMs / 1000);
}

/***************** lastSunday **************************************************/
int lastSunday(int year, int month)
{
  // month: 1..12
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = 31;
  t.tm_hour = 12; // avoid edge cases around midnight
  t.tm_min = 0;
  t.tm_sec = 0;

  // mktime on ESP8266 is okay here because we only use tm_wday and tm_mday.
  // Even if "local" is UTC-like, weekday calculation is consistent.
  mktime(&t);

  // tm_wday: 0=Sunday..6=Saturday
  return 31 - t.tm_wday;
}

/***************** isDstEurope *************************************************/
bool isDstEurope(time_t utc)
{
  struct tm *t = gmtime(&utc);
  if (t == nullptr)
  {
    return false;
  }

  int year = t->tm_year + 1900;
  int month = t->tm_mon + 1;
  int day = t->tm_mday;
  int hour = t->tm_hour;

  if (month < 3 || month > 10)
  {
    return false;
  }

  if (month > 3 && month < 10)
  {
    return true;
  }

  // DST start: last Sunday in March at 01:00 UTC
  if (month == 3)
  {
    int ls = lastSunday(year, 3);
    if (day > ls)
    {
      return true;
    }
    if (day < ls)
    {
      return false;
    }
    // day == last Sunday
    return (hour >= 1);
  }

  // DST end: last Sunday in October at 01:00 UTC
  if (month == 10)
  {
    int ls = lastSunday(year, 10);
    if (day < ls)
    {
      return true;
    }
    if (day > ls)
    {
      return false;
    }
    // day == last Sunday
    return (hour < 1);
  }

  return false;
}

/***************** getUtcOffsetSeconds *****************************************/
int32_t getUtcOffsetSeconds(time_t utc)
{
  // Winter: UTC+1, Summer: UTC+2
  if (isDstEurope(utc))
  {
    return 7200;
  }

  return 3600;
}

/***************** getDisplayTime **********************************************/
time_t getDisplayTime(time_t utc)
{
  return utc + getUtcOffsetSeconds(utc);
}

/***************** scheduleNextNightlySync *************************************/
void scheduleNextNightlySync(time_t currentUtc)
{
  // Scan forward to find next time where display time matches ntpSyncHour:ntpSyncMinute:00.
  // Bounded scan: max 48 hours in steps of 60 seconds.
  time_t startUtc = currentUtc;

  // Start at next full minute
  startUtc = startUtc - (startUtc % 60) + 60;

  const int maxMinutes = 48 * 60;

  for (int i = 0; i < maxMinutes; i++)
  {
    time_t candidateUtc = startUtc + (time_t)i * 60;
    time_t candidateDisp = getDisplayTime(candidateUtc);

    struct tm *d = gmtime(&candidateDisp);
    if (d == nullptr)
    {
      continue;
    }

    if ((d->tm_hour == ntpSyncHour) &&
        (d->tm_min == ntpSyncMinute) &&
        (d->tm_sec == 0))
    {
      nextNtpSyncEpoch = candidateUtc;

      Serial.print("Next nightly sync display time: ");
      Serial.print(d->tm_hour);
      Serial.print(":");
      Serial.print(d->tm_min);
      Serial.print(":");
      Serial.print(d->tm_sec);
      Serial.print("  (DST ");
      Serial.print(isDstEurope(candidateUtc) ? "YES" : "NO");
      Serial.println(")");

      return;
    }
  }

  // Fallback: if scan fails (shouldn't), schedule 24h later
  nextNtpSyncEpoch = currentUtc + 24 * 3600;
}

/***************** updateTimeFromRtc *******************************************/
void updateTimeFromRtc()
{
  time_t nowUtc = getCurrentRtcTime();
  time_t nowDisp = getDisplayTime(nowUtc);

  struct tm *ti = gmtime(&nowDisp);
  if (ti == nullptr)
  {
    return;
  }

  t_sec = ti->tm_sec;
  t_min = ti->tm_min;
  t_hour = ti->tm_hour;
}

/***************** shouldStartNightlySync **************************************/
bool shouldStartNightlySync(time_t currentUtc)
{
  if (nextNtpSyncEpoch == 0)
  {
    scheduleNextNightlySync(currentUtc);
    return false;
  }

  if (currentUtc < nextNtpSyncEpoch)
  {
    return false;
  }

  // Schedule next immediately to avoid retrigger loops
  scheduleNextNightlySync(currentUtc);

  return true;
}

/***************** superviseWiFi ***********************************************/
void superviseWiFi(unsigned long nowMs)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    if (wifiLostSinceMs == 0)
    {
      wifiLostSinceMs = nowMs;
    }

    WiFi.reconnect();

    if ((nowMs - wifiLostSinceMs) > wifiLostTimeoutMs)
    {
      Serial.println("WiFi lost for long time -> back to WIFI CONFIG");
      wifiLostSinceMs = 0;
      myState = STATE_WIFI_CONFIG;
    }
  }
  else
  {
    wifiLostSinceMs = 0;
  }
}

/***************** updateBrightnessFromLdr ************************************/
void updateBrightnessFromLdr()
{
  int raw = analogRead(LDR_PIN);

  int mapped = map(raw, 0, 1023, minBrightness, maxBrightness);
  if (mapped < minBrightness)
  {
    mapped = minBrightness;
  }
  if (mapped > maxBrightness)
  {
    mapped = maxBrightness;
  }

  currentBrightness = (uint8_t)mapped;
}

/***************** gamma8 ******************************************************/
uint8_t gamma8(uint8_t inValue)
{
  // Quick curve to smooth low brightness steps.
  uint16_t x = inValue;
  uint16_t y = (x * x + 255) >> 8;
  y = (y * x + 255) >> 8;
  if (y > 255)
  {
    y = 255;
  }
  return (uint8_t)y;
}

/***************** renderBerlinClock *******************************************/
void renderBerlinClock(unsigned long nowMs)
{
  strip.clear();

  // -------------------- Seconds (LED[23]) smooth fade -----------------------
  // Cosine wave 0..1 over 1 second, then gamma correction.
  float phase = (float)(nowMs % 1000) / 1000.0f;
  float wave = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * phase);
  uint8_t levelLin = (uint8_t)(255.0f * wave);
  uint8_t level = gamma8(levelLin);

  strip.setPixelColor
  (
    SECOND_LED,
    strip.Color
    (
      (colorDefines[SECOND_LED][dRED] * level) / 255,
      (colorDefines[SECOND_LED][dGREEN] * level) / 255,
      (colorDefines[SECOND_LED][dBLUE] * level) / 255
    )
  );

  // -------------------- Berlin clock blocks ---------------------------------
  int fiveHourBlocks = t_hour / 5;
  int oneHourBlocks = t_hour % 5;

  int fiveMinBlocks = t_min / 5;
  int oneMinBlocks = t_min % 5;

  // 4. Row: 5 hours (19..22)
  for (int i = 0; i < 4; i++)
  {
    if (i < fiveHourBlocks)
    {
      int idx = 19 + i;
      strip.setPixelColor
      (
        idx,
        strip.Color
        (
          colorDefines[idx][dRED],
          colorDefines[idx][dGREEN],
          colorDefines[idx][dBLUE]
        )
      );
    }
  }

  // 3. Row: 1 hour (15..18)
  for (int i = 0; i < 4; i++)
  {
    if (i < oneHourBlocks)
    {
      int idx = 15 + i;
      strip.setPixelColor
      (
        idx,
        strip.Color
        (
          colorDefines[idx][dRED],
          colorDefines[idx][dGREEN],
          colorDefines[idx][dBLUE]
        )
      );
    }
  }

  // 2. Row: 5 minutes (4..14)
  for (int i = 0; i < 11; i++)
  {
    if (i < fiveMinBlocks)
    {
      int idx = 4 + i;
      strip.setPixelColor
      (
        idx,
        strip.Color
        (
          colorDefines[idx][dRED],
          colorDefines[idx][dGREEN],
          colorDefines[idx][dBLUE]
        )
      );
    }
  }

  // 1. Row: 1 minute (0..3)
  for (int i = 0; i < 4; i++)
  {
    if (i < oneMinBlocks)
    {
      int idx = 0 + i;
      strip.setPixelColor
      (
        idx,
        strip.Color
        (
          colorDefines[idx][dRED],
          colorDefines[idx][dGREEN],
          colorDefines[idx][dBLUE]
        )
      );
    }
  }

  strip.setBrightness(currentBrightness);
  strip.show();
}

/***************** showStaticRainbow *******************************************/
void showStaticRainbow()
{
  for (int i = 0; i < LED_COUNT; i++)
  {
    uint8_t pos = (i * 256 / LED_COUNT) & 0xFF;

    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (pos < 85)
    {
      r = pos * 3;
      g = 255 - pos * 3;
      b = 0;
    }
    else if (pos < 170)
    {
      pos -= 85;
      r = 255 - pos * 3;
      g = 0;
      b = pos * 3;
    }
    else
    {
      pos -= 170;
      r = 0;
      g = pos * 3;
      b = 255 - pos * 3;
    }

    strip.setPixelColor(i, strip.Color(r, g, b));
  }

  strip.setBrightness(currentBrightness);
  strip.show();
}
