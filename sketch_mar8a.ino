#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Servo.h>
#include <ArduinoJson.h>

// Wi-Fi credentials
const char *ssid = "ESP8266_Hotspot";
const char *password = "12345678";

// Create an instance of AsyncWebServer
AsyncWebServer server(80);

// Define the number of servos
#define NUM_SERVOS 5

// Servo control pins (Adjusted for ESP8266)
const int servoPins[NUM_SERVOS] = { 5, 4, 14, 12, 13 };  // D1, D2, D5, D6, D7

// Servo objects
Servo servos[NUM_SERVOS];

// Servo positions
const int basePosition = 0;   // Base position (angle)
const int fullPosition = 180; // Full position (angle)
bool servoStates[NUM_SERVOS] = { false }; // Keeps track of servo states

// Store constant messages in Flash to save RAM
const char successMessage[] PROGMEM = "ESP8266 API running!";
const char errorMessage[] PROGMEM = "{\"error\": \"Invalid servo ID\"}";

// Function to handle system info API
void handleSystemInfo(AsyncWebServerRequest *request) {
    StaticJsonDocument<100> jsonResponse;
    jsonResponse["status"] = "success";

    char buffer[50]; // Buffer to hold flash string
    strcpy_P(buffer, successMessage);
    jsonResponse["message"] = buffer;
    jsonResponse["uptime"] = millis() / 1000;

    String jsonString;
    serializeJson(jsonResponse, jsonString);
    request->send(200, "application/json", jsonString);
}

// Function to handle servo toggling using JSON POST body
void handleToggleServo(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    String jsonData = String((char*)data).substring(0, len);
    Serial.println("Received JSON: " + jsonData);

    StaticJsonDocument<200> jsonRequest;
    DeserializationError error = deserializeJson(jsonRequest, jsonData);

    if (error) {
        Serial.println("JSON Parsing Failed!");
        request->send(400, "application/json", "{\"error\": \"Invalid JSON format\"}");
        return;
    }

    if (!jsonRequest.containsKey("servoId")) {
        Serial.println("Missing servoId key!");
        request->send(400, "application/json", "{\"error\": \"Missing servoId in request\"}");
        return;
    }

    int servoId = jsonRequest["servoId"];

    if (servoId < 0 || servoId >= NUM_SERVOS) {
        Serial.println("Invalid servo ID received!");
        request->send(400, "application/json", "{\"error\": \"Invalid servo ID\"}");
        return;
    }

    // Toggle servo position
    servoStates[servoId] = !servoStates[servoId];
    int newPosition = servoStates[servoId] ? fullPosition : basePosition;
    servos[servoId].write(newPosition);

    Serial.printf("Toggled Servo %d to position %d\n", servoId, newPosition);

    // Send success response
    StaticJsonDocument<100> jsonResponse;
    jsonResponse["servoId"] = servoId;
    jsonResponse["newPosition"] = newPosition;

    String responseJson;
    serializeJson(jsonResponse, responseJson);
    request->send(200, "application/json", responseJson);
}

void setup() {
    Serial.begin(115200);

    // Setup Access Point mode
    WiFi.softAP(ssid, password);
    Serial.println("Access Point Started!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    // Attach servos to their respective pins with optimized PWM settings
    for (int i = 0; i < NUM_SERVOS; i++) {
        servos[i].attach(servoPins[i], 1000, 2000);
        servos[i].write(fullPosition);
        delay(500);
        servos[i].write(basePosition);
        delay(500);
    }

    // Register API Endpoints
    server.on("/api/data", HTTP_GET, handleSystemInfo);

    // Handle POST JSON request for toggling servos correctly
    server.on("/api/servo/toggle", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleToggleServo);

    server.begin();
    Serial.println("Web server started!");
}

void loop() {
    // Nothing needed here; ESPAsyncWebServer handles requests asynchronously
}
