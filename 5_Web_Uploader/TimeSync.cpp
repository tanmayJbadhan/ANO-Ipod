#include "TimeSync.h"
#include "WifiConfig.h"
#include <WiFi.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool TimeSync::_hasCredentials = false;
bool TimeSync::_enabled = false;
bool TimeSync::_ntpStarted = false;

// Any time after 2024 means the clock has been set by NTP at some point;
// an unset ESP32 clock starts at 1970.
static const time_t CLOCK_VALID_AFTER = 1704067200;

void TimeSync::begin() {
  _hasCredentials = strlen(WIFI_SSID) > 0;
  // The timezone lives in the environment, which doesn't survive a reboot
  // or deep sleep, so set it every boot - the time itself does survive.
  setenv("TZ", WIFI_TZ, 1);
  tzset();
  WiFi.mode(WIFI_OFF);
}

void TimeSync::wifiOn() {
  if (!_hasCredentials || _enabled) return;
  _enabled = true;
  _ntpStarted = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // non-blocking
}

void TimeSync::wifiOff() {
  if (!_enabled) return;
  _enabled = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void TimeSync::poll() {
  if (!_enabled || _ntpStarted) return;
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(WIFI_TZ, "pool.ntp.org", "time.nist.gov");
    _ntpStarted = true;
  }
}

bool TimeSync::wifiConnected() { return _enabled && WiFi.status() == WL_CONNECTED; }

bool TimeSync::clockString(char *buf, size_t len) {
  time_t now = time(nullptr);
  if (now < CLOCK_VALID_AFTER) return false;
  struct tm t;
  localtime_r(&now, &t);
  snprintf(buf, len, "%02d:%02d", t.tm_hour, t.tm_min);
  return true;
}
