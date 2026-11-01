#pragma once

// Template for WifiConfig.h. Copy this file to WifiConfig.h and fill in your
// home network - WifiConfig.h holds the password in plain text, so it is
// kept out of git (.gitignore). Leave WIFI_SSID empty to skip WiFi entirely:
// the console still works, the clock just shows uptime and Add Songs is
// unavailable.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
// POSIX timezone rule, so summer/winter time switches on its own.
// Berlin: CET (UTC+1) in winter, CEST (UTC+2) from the last Sunday of
// March to the last Sunday of October.
#define WIFI_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
