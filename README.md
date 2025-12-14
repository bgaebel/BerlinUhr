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

- Each LED in the 5-minute row represents 5 minutes
- LEDs at **15, 30, and 45 minutes** are highlighted as quarter-hour markers

### Hours

- Upper row: 5-hour blocks
- Lower row: 1-hour blocks

### Seconds

- Single LED with smooth fade animation (not simple blinking)

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
2. It waits up to ~20 seconds.
3. Only if this fails, the **WiFiManager configuration portal** is started.

### Why not `autoConnect()`?

WiFiManager’s `autoConnect()` was found to be unreliable on ESP8266 in combination with WiFi stack resets and led to:
- `"No wifi saved, skipping"`
- Credentials not being reused after reset

Therefore:
- WiFiManager is used **only as a configuration UI**
- Connection logic is handled explicitly

### Result

- Stable reconnect after reset
- No fast-failing attempts
- No accidental credential erasure

---

## Brightness Control

- The LDR is read once per second
- Brightness is mapped linearly and clamped between configurable minimum and maximum values
- LED brightness is updated dynamically without blocking the main loop

---

## Build & Upload (Arduino IDE)

### Recommended Settings

- Board: **LOLIN (WEMOS) D1 mini (clone)**
- Flash Size: **4MB (FS:2MB OTA:~1019KB)**
- Flash Frequency: **40 MHz**
- Flash Mode: **DOUT**
- CPU Frequency: **80 MHz**
- lwIP Variant: **v2 Higher Bandwidth**
- Debug Level: **None**

### Flash Erase Strategy

- First upload: **Erase All Flash Contents**
- Subsequent uploads: **Only Sketch**

This ensures WiFi credentials are preserved while still allowing clean initial provisioning.

---

## Known Limitations

- No battery-backed RTC  
  → After power loss, valid time requires WiFi and NTP.
- Daylight Saving Time rules are hardcoded (EU standard).  
  Political or regional changes would require a firmware update.
- GPIO0 (D3) is a boot-strap pin and therefore not ideal for WS2812 data output.  
  Recommended alternative: **D2 (GPIO4)**.

---

## Project Structure

- State machine based architecture:
  - `STATE_WIFI_CONFIG`
  - `STATE_WAIT_FOR_TIME`
  - `STATE_SHOW_CLOCK`
- Time handling is strictly separated from display logic
- Color configuration is centralized and independent of time logic
- Rendering code contains no hard-coded color values

---

## License

This project is provided **as-is** for personal and educational use.

You are free to:
- Fork the project
- Modify the source code
- Adapt it to your own hardware

No warranty is provided.

---

## Final Note

This project intentionally prioritizes:
- Stability over clever hacks
- Explicit logic over implicit behavior
- Debuggability over abstraction

If you can understand this code months later without rereading long issue threads,  
the design has achieved its goal.
