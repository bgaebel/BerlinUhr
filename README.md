# BerlinUhr (Berlin Clock) – ESP8266 + WS2812

A modern implementation of the **Berlin Clock (Mengenlehreuhr)** using an **ESP8266**, **WS2812B LEDs**, **WiFi**, **NTP**, and **adaptive brightness via LDR**.

This project focuses on **robust time handling**, **stable WiFi behavior**, and **clean separation of logic**, avoiding fragile timezone hacks and unreliable auto-connect mechanisms.

---

## Features

- ✅ Correct Berlin Clock layout and logic
- 🌐 WiFi configuration via **WiFiManager** (fallback only)
- 🔁 Reliable WiFi reconnect using stored credentials
- 🕒 Accurate time via **NTP (UTC)**
- 🌍 **Custom EU Daylight Saving Time (DST)** calculation (no system TZ dependency)
- ⏱ Internal UTC-based RTC (`rtcBaseTime + millis()`)
- 🌙 Nightly configurable NTP resynchronization
- 💡 Smooth second indicator fade (cosine + gamma correction)
- 🔆 Automatic brightness control via LDR
- 🎨 Centralized color definitions (no magic RGB values)
- 🔴 Quarter-hour markers (15 / 30 / 45 minutes) highlighted correctly
- ⚙️ Clear state machine design

---

## Hardware Requirements

- ESP8266 (tested with **LOLIN / Wemos D1 mini**)
- WS2812B LED strip (24 LEDs total)
- LDR (photoresistor) + voltage divider
- Stable 5V power supply for LEDs
- Common GND between ESP8266 and LED power

### LED Mapping

| Function            | LED Index |
|---------------------|-----------|
| 1-minute row        | 0 – 3     |
| 5-minute row        | 4 – 14    |
| 1-hour row          | 15 – 18   |
| 5-hour row          | 19 – 22   |
| Seconds indicator   | 23        |

---

## Berlin Clock Logic

### Minutes
- Each LED = 5 minutes (row of 11 LEDs)
- LEDs at **15, 30, 45 minutes** are highlighted in a different color (quarter-hour markers)

### Hours
- Upper row: 5-hour blocks
- Lower row: 1-hour blocks

### Seconds
- Single LED with smooth fade (not simple blinking)

---

## Time Handling (Important)

This project **does not rely on ESP8266 timezone handling**, which is known to be unreliable.

### Design Decisions

- **NTP always provides UTC**
- Internal clock runs in **UTC**
- Display time is calculated as:
  - Winter: UTC + 1 hour
  - Summer: UTC + 2 hours (EU DST rule)
- Daylight Saving Time is calculated manually:
  - Starts: last Sunday in March at 01:00 UTC
  - Ends: last Sunday in October at 01:00 UTC

### Why this approach?

- Deterministic behavior
- No dependency on `setenv()`, `tzset()`, or libc timezone tables
- Correct across resets and long runtimes
- Easy to understand and debug

---

## WiFi Behavior (Read This)

### How WiFi works in this project

1. On boot, the ESP8266 tries to connect using **stored credentials**:
   ```cpp
   WiFi.begin();
