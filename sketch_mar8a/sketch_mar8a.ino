#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <Servo.h>
#include <ArduinoJson.h>

// Wi-Fi credentials
const char *ssid = "ESP8266_Hotspot";
const char *password = "12345678";

// Web server and WebSocket server
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Define number of servos (4 in use)
#define NUM_SERVOS 4

// Servo control pins (D1, D2, D5, D6)
const int servoPins[NUM_SERVOS] = { 5, 4, 14, 12 };

// Servo objects and tracking variables
Servo servos[NUM_SERVOS];
bool servoStates[NUM_SERVOS] = { false };
int servoMin[NUM_SERVOS] = { 0, 0, 0, 0 };
int servoMax[NUM_SERVOS] = { 180, 180, 180, 180 };

// Default threshold values (used for initialization and reference)
int defaultServoMin = 0;
int defaultServoMax = 180;

unsigned long lastMoveTime[NUM_SERVOS] = { 0 };
int currentPos[NUM_SERVOS] = { 0 };
bool moving[NUM_SERVOS] = { false };
int targetPos[NUM_SERVOS] = { 0 };
int moveSpeed[NUM_SERVOS] = { 0 };  // 0 = instant, 1 = slow, 2 = medium, 3 = fast

// Delay timing in milliseconds for movement speeds
const int speedDelay[] = { 0, 30, 15, 5 };

int getSpeedIndex(const String &speed) {
  if (speed == "slow") return 1;
  if (speed == "medium") return 2;
  if (speed == "fast") return 3;
  return 0;
}

void sendSystemStatus() {
  StaticJsonDocument<200> json;
  json["status"] = "running";
  json["uptime"] = millis() / 1000;
  json["ip"] = WiFi.softAPIP().toString();

  String jsonStr;
  serializeJson(json, jsonStr);
  webSocket.broadcastTXT(jsonStr);
}

void handleWebSocketMessage(uint8_t num, uint8_t *payload, size_t length) {
  StaticJsonDocument<300> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    webSocket.broadcastTXT("{\"error\": \"Invalid JSON\"}");
    return;
  }

  if (!doc.containsKey("action")) {
    webSocket.broadcastTXT("{\"error\": \"Missing action\"}");
    return;
  }

  String action = doc["action"];

  if (action == "get_status") {
    sendSystemStatus();
  }

//used for safe live controls on the monitor application
else if (action == "get_thresholds") {
    if (doc.containsKey("servoId")) {
      int id = doc["servoId"];
      if (id >= 0 && id < NUM_SERVOS) {
        StaticJsonDocument<100> res;
        res["servoId"] = id;
        res["min"] = servoMin[id];
        res["max"] = servoMax[id];
        String out;
        serializeJson(res, out);
        webSocket.broadcastTXT(out);
      } else {
        webSocket.broadcastTXT("{\"error\": \"Invalid servo ID\"}");
      }
    } else {
      StaticJsonDocument<400> res;
      JsonArray arr = res.createNestedArray("thresholds");
      for (int i = 0; i < NUM_SERVOS; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["servoId"] = i;
        obj["min"] = servoMin[i];
        obj["max"] = servoMax[i];
      }
      String out;
      serializeJson(res, out);
      webSocket.broadcastTXT(out);
    }
  }


  //used for defining min and max thresholds per servo 
  else if (action == "set_thresholds") {
  int id = doc["servoId"];

  if (id < 0 || id >= NUM_SERVOS) {
    webSocket.broadcastTXT("{\"error\": \"Invalid servo ID\"}");
    return;
  }

  bool hasMin = doc.containsKey("min");
  bool hasMax = doc.containsKey("max");

  if (!hasMin && !hasMax) {
    webSocket.broadcastTXT("{\"error\": \"No threshold provided\"}");
    return;
  }

  int newMin = servoMin[id];
  int newMax = servoMax[id];

  if (hasMin) newMin = doc["min"];
  if (hasMax) newMax = doc["max"];

  if (newMin >= newMax) {
    webSocket.broadcastTXT("{\"error\": \"min must be less than max\"}");
    return;
  }

  if (hasMin) servoMin[id] = newMin;
  if (hasMax) servoMax[id] = newMax;

  StaticJsonDocument<100> res;
  res["status"] = "thresholds updated";
  res["servoId"] = id;
  if (hasMin) res["min"] = newMin;
  if (hasMax) res["max"] = newMax;

  String out;
  serializeJson(res, out);
  webSocket.broadcastTXT(out);
}


  //moves to given position by given speed, used for testing and debugging through monitoring app
  else if (action == "move_to_position") {
    int id = doc["servoId"];
    int position = doc["position"];
    String speed = doc["speed"] | "medium";

    if (id >= 0 && id < NUM_SERVOS) {
      targetPos[id] = position;
      moveSpeed[id] = getSpeedIndex(speed);

      if (moveSpeed[id] == 0) {
        servos[id].write(position);
        currentPos[id] = position;
        moving[id] = false;
        webSocket.broadcastTXT("{\"status\": \"moved instantly\"}");
      } else {
        moving[id] = true;
        StaticJsonDocument<100> res;
        res["status"] = "moving to position at " +speed + " speed";
        res["servoId"] = id;
        res["target"] = position;
        String out;
        serializeJson(res, out);
        webSocket.broadcastTXT(out);
      }
    } else {
      webSocket.broadcastTXT("{\"error\": \"Invalid servo ID\"}");
    }
  }

  else if (action == "toggle_servo") {
    int id = doc["servoId"];
    String speed = doc["speed"] | "medium";
    String direction = doc["direction"] | "";

    if (id >= 0 && id < NUM_SERVOS) {
      int desiredTarget;

      // If a direction is specified explicitly ("min" or "max") move to that threshold value
      if (direction == "min") {
        desiredTarget = servoMin[id];
      } else if (direction == "max") {
        desiredTarget = servoMax[id];
      } else {
        // Otherwise toggle to the opposite of current state
        servoStates[id] = !servoStates[id];
        desiredTarget = servoStates[id] ? servoMax[id] : servoMin[id];
      }

      // If the servo is already moving to that target, treat it as a cancel command (allows for finite control through eeg)
      if (moving[id] && targetPos[id] == desiredTarget) {
        moving[id] = false;
        targetPos[id] = currentPos[id];
        webSocket.broadcastTXT("{\"status\": \"movement stopped\"}");
        return;
      }

      // Begin movement toward the new target
      targetPos[id] = desiredTarget;
      moving[id] = true;
      moveSpeed[id] = getSpeedIndex(speed);

      // If speed is 0 (instant), move immediately
      if (moveSpeed[id] == 0) {
        servos[id].write(desiredTarget);
        currentPos[id] = desiredTarget;
        moving[id] = false;
        webSocket.broadcastTXT("{\"status\": \"moved instantly\"}");
      } else {
        // Otherwise let the main loop handle smooth movement
        StaticJsonDocument<100> res;
        res["status"] = "toggle started";
        res["target"] = desiredTarget;
        String out;
        serializeJson(res, out);
        webSocket.broadcastTXT(out);
      }
    } else {
      webSocket.broadcastTXT("{\"error\": \"Invalid servo ID\"}");
    }
  }
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    handleWebSocketMessage(num, payload, length);
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started");
  Serial.print("ESP8266 is running at IP: ");
  Serial.println(WiFi.softAPIP());

  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].attach(servoPins[i], 500, 2500);
    servos[i].write(servoMin[i]);
    currentPos[i] = servoMin[i];
    delay(500);
  }

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  webSocket.loop();

  unsigned long now = millis();
  for (int i = 0; i < NUM_SERVOS; i++) {
    if (moving[i] && moveSpeed[i] > 0 && now - lastMoveTime[i] > speedDelay[moveSpeed[i]]) {
      lastMoveTime[i] = now;

      if (currentPos[i] < targetPos[i]) currentPos[i]++;
      else if (currentPos[i] > targetPos[i]) currentPos[i]--;

      servos[i].write(currentPos[i]);

      if (currentPos[i] == targetPos[i]) {
        moving[i] = false;
      }
    }
  }
}
