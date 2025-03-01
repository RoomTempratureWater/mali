#ifndef STATUS_DISPLAY_H
#define STATUS_DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 display;  // Declare display as external

void HandleStatus(const char* ssid, bool wifiConnected, bool serverConnected, const char* email, bool sensorWorking);

#endif
