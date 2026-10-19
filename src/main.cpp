#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include "secrets.h"

ESP8266WebServer server(80);

Servo lockServo;

const int servoPin = D6;

// Change these later if your physical latch needs different angles
const int LOCK_ANGLE = 0;
const int UNLOCK_ANGLE = 90;

bool locked = true;

void handleHome() {
  String lockText = locked ? "LOCKED" : "UNLOCKED";

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>My Mailbox</title>
<style>
body {
  font-family: Arial, sans-serif;
  text-align: center;
  background: #f2f2f2;
  padding: 30px;
}
.box {
  max-width: 400px;
  margin: auto;
  background: white;
  padding: 25px;
  border-radius: 20px;
  box-shadow: 0 5px 20px rgba(0,0,0,0.1);
}
h1 { margin-bottom: 25px; }
.status { font-size: 22px; margin: 18px; }
button {
  width: 90%;
  padding: 18px;
  margin: 8px;
  border: none;
  border-radius: 12px;
  font-size: 18px;
  cursor: pointer;
}
.unlock { background: #4CAF50; color: white; }
.lock { background: #333; color: white; }
</style>
</head>
<body>
<div class="box">
<h1>📬 MY MAILBOX</h1>
<div class="status">
  <span id="lockEmoji">LOCKED</span>
  Status: <b id="lockStatus">LOCKED</b>
</div>
<div class="status">📦 Mail: <b>EMPTY</b></div>
<div class="status">📏 Distance: <b>-- cm</b></div>
<button class="unlock" onclick="unlockBox()">🔓 UNLOCK</button>
<button class="lock" onclick="lockBox()">🔒 LOCK</button>
</div>
<script>
function unlockBox() {
  fetch('/unlock')
    .then(response => response.text())
    .then(data => {
      document.getElementById("lockStatus").innerText = "UNLOCKED";
      document.getElementById("lockEmoji").innerText = "🔓";
    });
}
function lockBox() {
  fetch('/lock')
    .then(response => response.text())
    .then(data => {
      document.getElementById("lockStatus").innerText = "LOCKED";
      document.getElementById("lockEmoji").innerText = "🔒";
    });
}
</script>
</body>
</html>
)rawliteral";

  page.replace("LOCKED", lockText);

  server.send(200, "text/html; charset=UTF-8", page);
}

void handleUnlock() {
  locked = false;
  lockServo.write(UNLOCK_ANGLE);
  Serial.println("🔓 MAILBOX UNLOCKED");
  server.send(200, "text/plain", "UNLOCKED");
}

void handleLock() {
  locked = true;
  lockServo.write(LOCK_ANGLE);
  Serial.println("🔒 MAILBOX LOCKED");
  server.send(200, "text/plain", "LOCKED");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Starting Smart Mailbox...");

  lockServo.attach(servoPin);
  lockServo.write(LOCK_ANGLE);
  delay(500);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected!");
  Serial.print("Mailbox IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleHome);
  server.on("/unlock", handleUnlock);
  server.on("/lock", handleLock);
  server.begin();

  Serial.println("Mailbox dashboard started!");
}

void loop() {
  server.handleClient();
}
