#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>  // Include Preferences library

#define LED_PIN 14  // LED pin

Preferences preferences;  // Create Preferences instance

bool deviceRegistered = false;
unsigned long lastSensorSend = 0;
const long sensorInterval = 10000; // 10 seconds
bool sseActive = false;

unsigned long lastCheck = 0;
const long checkInterval = 5000; // Check every 5 seconds

const char* default_ssid = "ESP32-CAM-Setup";
const char* default_password = "12345678";
const char* device_id = "5aafc0ef-dce4-48a8-b232-3e586b0d7868";
const char* serverURL = "https://solid-primate-strangely.ngrok-free.app";
const char* serverUrl = "https://solid-primate-strangely.ngrok-free.app/esp32/updates/{device_id}";

WiFiClientSecure client;
WebServer server(80);
String ssid, password, user_email;

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
        delay(1000);
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

        delay(1000);
        WiFi.disconnect();
        if (connectToWiFi()) {
            registerDevice();
        }
    } else {
        server.send(400, "text/plain", "Missing SSID, Password, or User Email");
    }
}

bool registerDevice() {
    HTTPClient http;
    String url = String(serverURL) + "/esp32/register";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{\"user_email\": \"" + user_email + "\", \"device_id\": \"" + device_id + "\"}";
    int httpResponseCode = http.POST(jsonData);

    if (httpResponseCode == HTTP_CODE_OK || httpResponseCode == HTTP_CODE_CREATED) {
        Serial.println("ESP32 Registered Successfully!");
        deviceRegistered = true;
        http.end();
        return true;
    } else {
        Serial.printf("Registration failed. HTTP Response Code: %d\n", httpResponseCode);
        http.end();
        return false;
    }
}



void listenForCommands() {
    if (!WiFi.isConnected()) {
        Serial.println("WiFi not connected, skipping SSE.");
        return;
    }

    HTTPClient http;
    String url = String(serverUrl);
    url.replace("{device_id}", device_id);

    Serial.println("Starting SSE connection...");
    http.begin(url);

    int httpResponseCode = http.GET();
    if (httpResponseCode != HTTP_CODE_OK) {
        Serial.printf("SSE request failed: %s\n", http.errorToString(httpResponseCode).c_str());
        http.end();
        sseActive = false;
        deviceRegistered = false;
        return;
    }

    sseActive = true;  // Mark SSE as active only on success
    WiFiClient* stream = http.getStreamPtr();
    unsigned long sseStartTime = millis();

    while (WiFi.isConnected() && stream->connected()) {
        if (stream->available()) {
            String line = stream->readStringUntil('\n');
            Serial.println(line);
            handleSSEEvent(line);
        }

        // **Break out every 10 sec to allow other tasks**
        if (millis() - sseStartTime > 10000) {
            Serial.println("Breaking SSE loop to allow other tasks...");
            break;
        }
    }

    Serial.println("SSE connection temporarily closed.");
    sseActive = false;  
    http.end();
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
      digitalWrite(LED_PIN, HIGH);
      Serial.println("LED turned ON");
    } else if (command == "led_off") {
      digitalWrite(LED_PIN, LOW);
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


bool sendSensorData() {
    HTTPClient http;
    String url = String(serverURL) + "/esp32/send_data";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    float temperature = 24.5;  // Simulated value
    float humidity = 60.0;  // Simulated value

    String jsonData = "{\"user_email\": \"" + String(user_email) + "\", \"device_id\": \"" + String(device_id) + "\", \"temperature\": " + String(temperature) + ", \"humidity\": " + String(humidity) + "}";

    int httpResponseCode = http.POST(jsonData);

    if (httpResponseCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Sensor data sent successfully. Response: " + response);
        http.end();
        return true;  // Success
    } else {
        Serial.printf("Failed to send sensor data. Error: %s\n", http.errorToString(httpResponseCode).c_str());
        http.end();
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

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    startAPMode();
    loadCredentials();  // Load saved Wi-Fi credentials

    if (ssid != "" && password != "") {  
        Serial.println("Attempting to connect to saved Wi-Fi...");
        if (connectToWiFi()) {  
            if (registerDevice()) {  
                Serial.println("Device registered, starting SSE...");
                listenForCommands();  
            }
        } else {
            Serial.println("Wi-Fi connection failed, starting AP Mode...");
            startAPMode();
        }
    } else {
        Serial.println("No saved credentials, starting AP Mode...");
        startAPMode();
    }
}

void loop() {
    server.handleClient();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi Disconnected, retrying...");
        WiFi.begin(ssid.c_str(), password.c_str());
        delay(5000);
        return;
    }

    if (!deviceRegistered) {
        Serial.println("Device not registered. Registering...");
        if (registerDevice()) {
            Serial.println("Device successfully registered.");
            deviceRegistered = true;
        } else {
            Serial.println("Device registration failed. Retrying...");
            delay(5000);
            return;
        }
    }

    if (!sseActive) {
        Serial.println("SSE is inactive. Restarting...");
        listenForCommands();
    }

    if (millis() - lastSensorSend >= sensorInterval) {
        lastSensorSend = millis();

        if (!sendSensorData()) {
            Serial.println("Failed to send sensor data. Resetting registration...");
            deviceRegistered = false;
        }
    }
}
