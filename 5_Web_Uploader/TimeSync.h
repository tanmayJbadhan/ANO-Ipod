#pragma once

#include <stddef.h>

// WiFi on demand, plus real time-of-day from NTP.
//
// WiFi is off unless something asks for it (Add Songs), to save battery.
// Whenever it is on, the clock is synced over NTP; after that the ESP32
// keeps time by itself - including through WiFi going off and through
// deep sleep - so the clock stays right without WiFi. Until the first sync
// it falls back to showing uptime.
class TimeSync {
public:
  static void begin();     // once in setup(): applies the timezone, WiFi stays off
  static void wifiOn();    // starts connecting in the background
  static void wifiOff();   // disconnects and powers the radio down
  static void poll();      // every loop(); starts NTP once connected
  static bool wifiConnected();
  static bool wifiEnabled() { return _enabled; }

  // "HH:MM" if the clock has ever been synced; false otherwise.
  static bool clockString(char *buf, size_t len);

private:
  static bool _hasCredentials;
  static bool _enabled;
  static bool _ntpStarted;
};
