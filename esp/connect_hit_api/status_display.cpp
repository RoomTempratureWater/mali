#include "status_display.h"

#define CHECK_WIDTH 8
#define CHECK_HEIGHT 8
#define CROSS_WIDTH 8
#define CROSS_HEIGHT 8

static const unsigned char PROGMEM check_bitmap[] = {
  0b00000000,
  0b00000001,
  0b00000011,
  0b10000110,
  0b11001100,
  0b01111000,
  0b00110000,
  0b00000000
};

static const unsigned char PROGMEM cross_bitmap[] = {
  0b00000000,
  0b11000011,
  0b01100110,
  0b00111100,
  0b00111100,
  0b01100110,
  0b11000011,
  0b00000000
};

void HandleStatus(const char* ssid, bool wifiConnected, bool serverConnected, const char* email, bool sensorWorking) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    
    // Draw a border
    //display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
    
    // Display Title
    display.setTextSize(1.2);
    display.setCursor(25, 0);
    display.println("System Status");
    
    display.setTextSize(0.8);
    display.setCursor(0, 15);
    display.print("WiFi: ");
    display.drawBitmap(35, 15, wifiConnected ? check_bitmap : cross_bitmap, CHECK_WIDTH, CHECK_HEIGHT, WHITE);
    display.setCursor(50, 15);
    String newssid = String(" - ") + ssid;
    display.print(wifiConnected ? newssid : "");
    
    display.setCursor(0, 35);
    display.print("Server: ");
    display.drawBitmap(45, 35, serverConnected ? check_bitmap : cross_bitmap, CHECK_WIDTH, CHECK_HEIGHT, WHITE);
    String newemail = String(" - ") + email;
    if (serverConnected) {
      display.setCursor(55, 35);
      display.print(newemail);
    }
    
    display.setCursor(0, 55);
    display.print("Sensor: ");
    display.drawBitmap(50, 55, sensorWorking ? check_bitmap : cross_bitmap, CHECK_WIDTH, CHECK_HEIGHT, WHITE);
    
    display.display();
}
