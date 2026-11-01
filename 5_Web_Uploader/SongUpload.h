#pragma once

#include <stdint.h>

// "Add Songs": a small web server on the console. Open its address from a
// phone or laptop on the same WiFi and drop audio files onto the page;
// they're written straight to the SD card's root, where the music player
// finds them. Also reachable as http://jigglypuff.local on devices that
// support mDNS.
//
// Music playback must be stopped while this runs - the audio task reads
// the SD card from the other core, and the SD driver isn't safe to share.
namespace SongUpload {

bool start(); // needs WiFi connected; returns false otherwise
void stop();
void poll(); // call every loop() while running
bool running();

bool busy();                  // a file is being received right now
const char *currentName();    // file being received / last received
uint32_t bytesReceived();     // of the current file
uint32_t filesAdded();        // total completed this session
const char *lastError();      // "" if none

} // namespace SongUpload
