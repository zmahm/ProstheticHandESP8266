# ESP8266 Robotic Arm Controller — Wi-Fi AP + WebSocket Servo Control

Firmware for an ESP8266-based robotic arm controller. The board hosts its own Wi-Fi access point and a WebSocket server to receive JSON commands for smooth servo motion. Designed to integrate with a Python/Flask EEG pipeline, but usable standalone.

## Features

- ESP8266 soft-AP with configurable SSID/password.
- WebSocket server on port 81 for low-latency control.
- Four servo channels with smooth, speed-controlled motion.
- Per-servo thresholds (min/max angles) that can be queried or updated at runtime.
- Simple JSON control API: get_status, get_thresholds, set_thresholds, move_to_position, toggle_servo.
- Broadcast responses for easy monitoring from multiple clients.

## Hardware

- MCU: ESP8266 (e.g., NodeMCU, Wemos D1 mini).
- Servos: up to 4 × hobby servos.
- Pins (GPIO):
    - Servo 0 → D1 (GPIO5)
    - Servo 1 → D2 (GPIO4)
    - Servo 2 → D5 (GPIO14)
    - Servo 3 → D6 (GPIO12)
- Signal ranges: servos attached with pulse range 500–2500 µs. Defaults: min=0, max=180.

Note: Power your servos from a suitable external supply. Tie grounds together (servo GND ↔ ESP8266 GND).

## Network Configuration

- SSID: ESP8266_Hotspot  
- Password: 12345678  
- AP IP: printed on serial at boot (typically 192.168.4.1)  
- WebSocket endpoint: ws://<AP_IP>:81

To change credentials, edit in the sketch:

    const char *ssid = "ESP8266_Hotspot";
    const char *password = "12345678";

## Software Dependencies (Arduino IDE / PlatformIO)

Required libraries:
- ESP8266 core for Arduino
- ESPAsyncWebServer (ESP8266)
- ESPAsyncTCP (ESP8266; required by ESPAsyncWebServer)
- WebSockets (Links2004/WebSockets)
- Servo (Arduino Servo library for ESP8266)
- ArduinoJson (>= 6.x)

Install via Arduino Library Manager where available. For ESPAsyncWebServer/ESPAsyncTCP, install from their GitHub repos if not present in the manager.

## Build and Upload

1. Select your ESP8266 board in Arduino IDE (e.g., Tools → Board → NodeMCU 1.0).
2. Install the dependencies above.
3. Open the sketch and compile/upload.
4. Open Serial Monitor at 115200 baud to read AP IP and status logs.

## Code Overview

- Access Point startup: WiFi.softAP(ssid, password);
- WebSocket server: WebSocketsServer webSocket(81);
- Servo array: four Servo objects, attached on setup.
- Motion control: target position, current position, and moveSpeed per servo. Smooth stepping is timed in loop() using speedDelay[].

Speed index mapping:
- 0 → instant (jump to target)
- 1 → slow (delay 30 ms per step)
- 2 → medium (15 ms per step)
- 3 → fast (5 ms per step)

## JSON WebSocket API

All API messages are JSON text frames over WebSocket port 81. Responses are broadcast to all connected clients.

### 1) get_status

Request:

    { "action": "get_status" }

Broadcast response:

    { "status": "running", "uptime": 1234, "ip": "192.168.4.1" }

### 2) get_thresholds

Omit servoId to get all; include servoId (0–3) to query one.

Request (all):

    { "action": "get_thresholds" }

Response (all):

    {
      "thresholds": [
        { "servoId": 0, "min": 0, "max": 180 },
        { "servoId": 1, "min": 0, "max": 180 },
        { "servoId": 2, "min": 0, "max": 180 },
        { "servoId": 3, "min": 0, "max": 180 }
      ]
    }

Request (single):

    { "action": "get_thresholds", "servoId": 2 }

Response (single):

    { "servoId": 2, "min": 0, "max": 180 }

### 3) set_thresholds

Sets min and/or max for a specific servo (must satisfy min < max).

Request:

    { "action": "set_thresholds", "servoId": 1, "min": 10, "max": 160 }

Response:

    { "status": "thresholds updated", "servoId": 1, "min": 10, "max": 160 }

### 4) move_to_position

Moves a servo to an exact angle with a chosen speed.

Request:

    { "action": "move_to_position", "servoId": 0, "position": 90, "speed": "medium" }

Responses:

    { "status": "moved instantly" }

or (when using smooth speed 1–3):

    { "status": "moving to position at medium speed", "servoId": 0, "target": 90 }

### 5) toggle_servo

Toggles a servo between its threshold endpoints or forces motion to a specified endpoint.

Request (toggle):

    { "action": "toggle_servo", "servoId": 3, "speed": "fast" }

Request (force direction):

    { "action": "toggle_servo", "servoId": 3, "speed": "slow", "direction": "max" }

Responses:

    { "status": "moved instantly" }

or

    { "status": "toggle started", "target": 180 }

Cancel movement (if already heading to the same target):

    { "status": "movement stopped" }

### Error responses

Examples:

    { "error": "Invalid JSON" }
    { "error": "Missing action" }
    { "error": "Invalid servo ID" }
    { "error": "No threshold provided" }
    { "error": "min must be less than max" }

## Example JavaScript Client (Browser)

    <script>
      const ws = new WebSocket("ws://192.168.4.1:81");
      ws.onopen = () => {
        ws.send(JSON.stringify({ action: "get_status" }));
        ws.send(JSON.stringify({ action: "get_thresholds" }));
        // Example toggle
        ws.send(JSON.stringify({ action: "toggle_servo", servoId: 1, speed: "medium" }));
      };
      ws.onmessage = (evt) => console.log("WS:", evt.data);
      ws.onerror = (e) => console.error("WS error:", e);
    </script>

## Calibration Workflow

1. Home all servos to safe angles (defaults write min at setup).
2. Query thresholds:

       { "action": "get_thresholds" }

3. Set safe motion limits per axis:

       { "action": "set_thresholds", "servoId": 0, "min": 5, "max": 170 }

4. Test smooth moves:

       { "action": "move_to_position", "servoId": 0, "position": 90, "speed": "slow" }

5. Integrate with client logic (e.g., EEG classifier → mapped toggle_servo actions).

## Safety Notes

- Always validate servo travel limits mechanically before applying power.
- Use an external 5–6 V supply rated for peak servo current; do not power servos from the ESP8266 5V pin.
- Common ground between ESP8266 and servo power is required.
- Consider adding brown-out protection and flyback/EMI considerations in wiring.

## Troubleshooting

- Cannot connect to WebSocket: confirm you joined the AP, verify IP via Serial log, ensure port 81 is used.
- Jerky motion or resets: power supply insufficient; add bulk capacitance near servos, use thicker wires.
- Threshold errors: ensure min < max. Query current thresholds to confirm state.
- Multiple clients: server broadcasts responses; if you see mixed logs, filter by servoId on the client side.

## Acknowledgements

Built with ESP8266 Arduino core, ESPAsyncWebServer, ESPAsyncTCP, Links2004/WebSockets, ArduinoJson, and Servo.
