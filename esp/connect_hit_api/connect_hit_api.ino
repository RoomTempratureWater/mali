#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>  // Include Preferences library
#include "DHT.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "status_display.h"
#include <Arduino.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Variables to store user input
//char ssid[32] = "DefaultSSID";
//char email[32] = "test123@gmail.com";
bool wifiConnected = false;
bool serverConnected = false;
bool sensorWorking = false;

#define DHTPIN 12
#define PUMP_PIN 14  // LED pin
#define DHTTYPE DHT11
Preferences preferences;  // Create Preferences instance
DHT dht(DHTPIN, DHTTYPE);
bool deviceRegistered = false;
unsigned long lastSensorSend = 0;
const long sensorInterval = 10000; // 10 seconds
bool sseActive = false;
bool temphum = true;
unsigned long lastCheck = 0;
const long checkInterval = 5000; // Check every 5 seconds

const char* default_ssid = "ESP32-CAM-Setup";
const char* default_password = "12345678";
const char* device_id = "5aafc0ef-dce4-48a8-b232-3e586b0d7868";
const char* serverURL = "https://solid-primate-strangely.ngrok-free.app";
const char* serverUrl = "https://solid-primate-strangely.ngrok-free.app/esp32/updates/{device_id}";
const char* pingurl = "https://solid-primate-strangely.ngrok-free.app/ping";
WiFiClientSecure client;
WebServer server(80);
String ssid, password, user_email;

struct OptionType {
    bool hasError;  // True if there's an error, false otherwise
    float temprature;
    float humidity;
};

// Webhook URL (Replace with actual URL)
const char* webhook_url = "https://solid-primate-strangely.ngrok-free.app/register_webhook";  

// HTML Form for Wi-Fi Input
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>Wi-Fi Setup</title></head>
<body>
    <h2>Enter Wi-Fi Credentials</h2>
    <form action="/connect" method="post">
        SSID: <input type="text" name="ssid"><br>
        Password: <input type="password" name="password"><br>
        User Email: <input type="text" name="user_email"><br>
        <input type="submit" value="Connect">
    </form>
</body>
</html>
)rawliteral";


int lastupdate = 0;
int freq = 1000;
SemaphoreHandle_t mutex;
TaskHandle_t SSE_taskHandler = NULL;
void setup() {
  mutex = xSemaphoreCreateMutex();
  Serial.begin(115200);
  while (!Serial) {
    // Wait for Serial to be ready (only needed for native USB ports)
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
    display.clearDisplay();

    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW);
    startAPMode();
    loadCredentials();  // Load saved Wi-Fi credentials
    dht.begin();
    if (ssid != "" && password != "") {  
        Serial.println("Attempting to connect to saved Wi-Fi...");
        if (connectToWiFi()) {
            wifiConnected = true;  
            if (registerDevice()) {  
                serverConnected = true;
                Serial.println("Device registered, starting SSE...");
                deviceRegistered = true;
                //serverConnected = true;
                //creating a task for SSE on core 0
                xTaskCreatePinnedToCore(
                    SSE_bigboy,   /* Task function. */
                    "Handle SSE connection",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task *///setting lower than main loop which has a priotiy of 1
                    &SSE_taskHandler,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */
                  vTaskDelay(pdMS_TO_TICKS(2000));
            }
        } else {
            Serial.println("Wi-Fi connection failed, starting AP Mode...");
            startAPMode();
        }
    } else {
        Serial.println("No saved credentials, starting AP Mode...");
        startAPMode();
    }
//   Serial.print("setup() running on core ");
//   Serial.println(xPortGetCoreID());
}



void loop() {
while (1) {
//   Serial.print("loop() running on core ");
//   xTaskNotifyGive(SEE_taskHandler);
//   Serial.println(xPortGetCoreID());
//     if (xSemaphoreTake(mutex, portMAX_DELAY)) {
//       freq = freq + 1000;
//       xSemaphoreGive(mutex);
//   }
//   vTaskDelay(pdMS_TO_TICKS(5000));

  
  server.handleClient();

  String ssidCopy = ssid; 
  String emailCopy = user_email; 

  char ssidBuffer[32];  
  char emailBuffer[32];

  ssidCopy.toCharArray(ssidBuffer, sizeof(ssidBuffer));
  emailCopy.toCharArray(emailBuffer, sizeof(emailBuffer));

  HandleStatus(ssidBuffer, wifiConnected, serverConnected, emailBuffer, sensorWorking);
  if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi Disconnected, retrying...");
      wifiConnected = false;
      WiFi.begin(ssid.c_str(), password.c_str());
      vTaskDelay(pdMS_TO_TICKS(2000));
  } else {
    wifiConnected = true;
  }

  if ((xSemaphoreTake(mutex, portMAX_DELAY)) && wifiConnected) {
    if (!deviceRegistered) {
        Serial.println("Device not registered. Registering...");
        if (registerDevice()) {
            Serial.println("Device successfully registered.");
            deviceRegistered = true;
            serverConnected = true;
            //creating a task for SSE on core 0
            xTaskCreatePinnedToCore(
                SSE_bigboy,   /* Task function. */
                "Handle SSE connection",     /* name of task. */
                10000,       /* Stack size of task */
                NULL,        /* parameter of the task */
                1,           /* priority of the task */
                &SSE_taskHandler,      /* Task handle to keep track of created task */
                0);          /* pin task to core 0 */
            vTaskDelay(pdMS_TO_TICKS(500));
            delay(2000);
        } else {
            Serial.println("Device registration failed. Retrying...");
            serverConnected = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
          xSemaphoreGive(mutex);
    }



  if ((millis() - lastSensorSend >= sensorInterval) && wifiConnected && deviceRegistered) {
      lastSensorSend = millis();

      if (!sendSensorData()) {
          Serial.println("Failed to send sensor data.");
      }else{
          sensorWorking = true;
      }
      if (!pingServer()) {
          Serial.println("Ping failed. Resetting registration...");
          vTaskDelete(SSE_taskHandler);
          if (xSemaphoreTake(mutex, portMAX_DELAY)) {
          deviceRegistered = false;
          xSemaphoreGive(mutex);
        }
          serverConnected = false;
      }
      
  }
}
}



void SSE_bigboy(void *pvParameters){
    HTTPClient http_sse;
    String url;
    if (xSemaphoreTake(mutex, portMAX_DELAY)) {
        url = String(serverUrl);
        url.replace("{device_id}", device_id);
        xSemaphoreGive(mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    Serial.println("Starting SSE connection...");
    http_sse.begin(url);

    int httpResponseCode = http_sse.GET();
    if (httpResponseCode != HTTP_CODE_OK) {
        Serial.printf("SSE request failed: %s\n", http_sse.errorToString(httpResponseCode).c_str());
        http_sse.end();

        if (xSemaphoreTake(mutex, portMAX_DELAY)) {
            sseActive = false;
            deviceRegistered = false;
            xSemaphoreGive(mutex);
        }
        vTaskDelete(NULL);
    }
    if (xSemaphoreTake(mutex, portMAX_DELAY)) {
        sseActive = true;
        serverConnected = true;
        xSemaphoreGive(mutex);
    }
    WiFiClient* stream = http_sse.getStreamPtr();
    unsigned long sseStartTime = millis();

    while (WiFi.isConnected() && stream->connected()) {
        if (stream->available()) {
            String line = stream->readStringUntil('\n');
            Serial.println(line);
            handleSSEEvent(line);
        }

        // **Checking variable every 1.5 seconds (dont ask me why its 150 i just think thats nice), if isRegistered is false, that means this connection is useless and maybe server is offline**
        if (millis() - sseStartTime > 1500) {
            if (xSemaphoreTake(mutex, portMAX_DELAY)) {
                if (!deviceRegistered) {
                    Serial.println("killing task");
                    serverConnected = false;
                    http_sse.end();
                    vTaskDelete(NULL); //killing task because maybe pingy wingy failed or we just want to restart a connection
                }
                xSemaphoreGive(mutex);
            }

        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool pingServer() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected!");
        return false;
    }

    HTTPClient http_ping;
    http_ping.begin(pingurl);
    int httpResponseCode = http_ping.GET();
    
    if (httpResponseCode == 200) {
        http_ping.end();
        return true;  // Server is alive
    } else {
        Serial.print("Ping failed, HTTP Response: ");
        Serial.println(httpResponseCode);
        http_ping.end();
        return false; // Server is down
    }
}

// Save Wi-Fi credentials after receiving from the web form
void saveCredentials(String new_ssid, String new_password, String new_user_email) {
    preferences.begin("wifi", false);
    preferences.putString("ssid", new_ssid);
    preferences.putString("password", new_password);
    preferences.putString("user_email", new_user_email);
    preferences.end();
    Serial.println("Wi-Fi credentials saved!");
}

// Retrieve Wi-Fi credentials from storage
void loadCredentials() {
    preferences.begin("wifi", true);
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    user_email = preferences.getString("user_email", "");
    preferences.end();
}

// Function to Connect to Wi-Fi
bool connectToWiFi() {
    Serial.println("Connecting to Wi-Fi...");
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWi-Fi Connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        return true;
    } else {
        Serial.println("\nFailed to connect!");
        return false;
    }
}

// Handle Root Request (Serves Web Form)
void handleRoot() {
    server.send(200, "text/html", htmlPage);
}

// Handle Wi-Fi Connection Request
void handleConnect() {
    if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("user_email")) {
        ssid = server.arg("ssid");
        password = server.arg("password");
        user_email = server.arg("user_email");
        Serial.println("Received SSID: " + ssid);
        Serial.println("Received Password: " + password);
        Serial.println("Received User Email: " + user_email);
        
        saveCredentials(ssid, password, user_email);  // Save credentials
        server.send(200, "text/plain", "Connecting to Wi-Fi... Check Serial Monitor.");

        vTaskDelay(pdMS_TO_TICKS(1000));
        WiFi.disconnect();
        if (connectToWiFi()) {
            registerDevice();
        }
    } else {
        server.send(400, "text/plain", "Missing SSID, Password, or User Email");
    }
}

bool registerDevice() {
    HTTPClient http_regs;
    String url = String(serverURL) + "/esp32/register";

    http_regs.begin(url);
    http_regs.addHeader("Content-Type", "application/json");

    String jsonData = "{\"user_email\": \"" + user_email + "\", \"device_id\": \"" + device_id + "\"}";
    int httpResponseCode;
    int attempts = 0;
    while (attempts < 3) {
        httpResponseCode = http_regs.POST(jsonData);
        if (httpResponseCode > 0) {
            Serial.println("Registration successful!");
            break;
        } else {
            Serial.println("Failed to register. Retrying...");
            delay(2000);
            attempts++;
        }
    }


    if (httpResponseCode == HTTP_CODE_OK || httpResponseCode == HTTP_CODE_CREATED) {
        Serial.println("ESP32 Registered Successfully!");
        if (xSemaphoreTake(mutex, portMAX_DELAY)) {
          deviceRegistered = true;
          xSemaphoreGive(mutex);
          }

          http_regs.end();
        return true;
    } else {
        Serial.printf("Registration failed. HTTP Response Code: %d\n", httpResponseCode);
        http_regs.end();
        return false;
    }
}




void handleSSEEvent(String eventData) {
  // Check if the line starts with "data: "
  if (eventData.startsWith("data: ")) {
    // Extract the JSON part by removing the "data: " prefix
    String jsonData = eventData.substring(6); // Remove the first 6 characters ("data: ")

    // Parse the JSON data
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
      Serial.println("Failed to parse JSON: " + String(error.c_str()));
      return;
    }

    // Extract the command from the JSON
    String command = doc["command"].as<String>();

    // Handle the command
    if (command == "led_on") {
      digitalWrite(PUMP_PIN, HIGH);
      Serial.println("LED turned ON");
    } else if (command == "led_off") {
      digitalWrite(PUMP_PIN, LOW);
      Serial.println("LED turned OFF");
    } else if (command == "send_data") {
      sendSensorData();
    } else {
      Serial.println("Unknown command: " + command);
    }
  } else {
    Serial.println("Ignoring non-data line: " + eventData);
  }
}


OptionType gettempandhum() {
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t) || isnan(f)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return {true, 0.0, 0.0};
  } else {
    return {false, t, h};
  }
}

bool sendSensorData() {
    HTTPClient http_sensor;
    String url = String(serverURL) + "/esp32/send_data";

    http_sensor.begin(url);
    http_sensor.addHeader("Content-Type", "application/json");

    OptionType result = gettempandhum();
    if (result.hasError) {
      Serial.printf("Failed to read from sensor!");
      temphum = false;
      http_sensor.end();
      return false;
    }
    String jsonData = "{\"user_email\": \"" + String(user_email) + "\", \"device_id\": \"" + String(device_id) + "\", \"temperature\": " + String(result.temprature) + ", \"humidity\": " + String(result.humidity) + "}";

    int httpResponseCode = http_sensor.POST(jsonData);

    if (httpResponseCode == HTTP_CODE_OK) {
        String response = http_sensor.getString();
        Serial.println("Sensor data sent successfully. Response: " + response);
        http_sensor.end();
        return true;  // Success
    } else {
        Serial.printf("Failed to send sensor data. Error: %s\n", http_sensor.errorToString(httpResponseCode).c_str());
        http_sensor.end();
        return false;  // Failure
    }
}

void startAPMode() {
    WiFi.softAP(default_ssid, default_password);
    Serial.println("ESP32 Access Point Started!");

    server.on("/", handleRoot);
    server.on("/connect", HTTP_POST, handleConnect);
    server.begin();
    Serial.println("Webserver Running...");
}

