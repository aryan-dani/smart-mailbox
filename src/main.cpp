#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <math.h>

#include "config.h"
#include "secrets.h"

ESP8266WebServer server(80);
Servo lockServo;

// --- Runtime state -----------------------------------------------------------

enum class LockState { LOCKED, UNLOCKED };
enum class MailStatus { UNKNOWN, EMPTY, MAIL_DETECTED };
enum class SensorStatus {
  DISABLED,
  NOT_READY,
  OK,
  TIMEOUT,
  OUT_OF_RANGE
};

LockState lockState = LockState::LOCKED;
MailStatus mailStatus = MailStatus::UNKNOWN;
SensorStatus sensorStatus =
#if ENABLE_ULTRASONIC
    SensorStatus::NOT_READY;
#else
    SensorStatus::DISABLED;
#endif

float lastDistanceCm = NAN;
unsigned long lastSensorUpdateMs = 0;
unsigned long lastWifiAttemptMs = 0;
bool wifiWasConnected = false;

#if ENABLE_ULTRASONIC
float distanceSamples[SENSOR_SAMPLE_COUNT];
int sampleIndex = 0;
int sampleFilled = 0;
unsigned long lastSensorSampleMs = 0;
#endif

// --- Helpers -----------------------------------------------------------------

const char* lockStateStr(LockState s) {
  return s == LockState::LOCKED ? "LOCKED" : "UNLOCKED";
}

const char* mailStatusStr(MailStatus s) {
  switch (s) {
    case MailStatus::EMPTY:
      return "EMPTY";
    case MailStatus::MAIL_DETECTED:
      return "MAIL_DETECTED";
    default:
      return "UNKNOWN";
  }
}

const char* sensorStatusStr(SensorStatus s) {
  switch (s) {
    case SensorStatus::DISABLED:
      return "DISABLED";
    case SensorStatus::OK:
      return "OK";
    case SensorStatus::TIMEOUT:
      return "TIMEOUT";
    case SensorStatus::OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    default:
      return "NOT_READY";
  }
}

String buildStatusJson() {
  String json = "{";
  json += "\"deviceId\":\"";
  json += DEVICE_ID;
  json += "\",";
  json += "\"lockState\":\"";
  json += lockStateStr(lockState);
  json += "\",";
  json += "\"mailStatus\":\"";
  json += mailStatusStr(mailStatus);
  json += "\",";

  if (isnan(lastDistanceCm)) {
    json += "\"distanceCm\":null,";
  } else {
    json += "\"distanceCm\":";
    json += String(lastDistanceCm, 1);
    json += ",";
  }

  json += "\"sensorStatus\":\"";
  json += sensorStatusStr(sensorStatus);
  json += "\",";

  const bool connected = WiFi.status() == WL_CONNECTED;
  json += "\"wifiConnected\":";
  json += connected ? "true" : "false";
  json += ",";

  if (connected) {
    json += "\"wifiRssi\":";
    json += String(WiFi.RSSI());
    json += ",";
  } else {
    json += "\"wifiRssi\":null,";
  }

  json += "\"uptimeMs\":";
  json += String(millis());
  json += ",";

  if (lastSensorUpdateMs == 0) {
    json += "\"lastSensorUpdateMs\":null";
  } else {
    json += "\"lastSensorUpdateMs\":";
    json += String(lastSensorUpdateMs);
  }

  json += "}";
  return json;
}

void sendJson(int code, const String& body) {
  server.send(code, "application/json; charset=UTF-8", body);
}

void sendStatus(int code) { sendJson(code, buildStatusJson()); }

// Only drive the servo when the commanded state changes.
bool setLock(bool locked) {
  const LockState target = locked ? LockState::LOCKED : LockState::UNLOCKED;
  if (target == lockState) {
    Serial.print(F("[lock] already "));
    Serial.println(lockStateStr(lockState));
    return true;
  }

  const int angle = locked ? LOCK_ANGLE : UNLOCK_ANGLE;
  lockServo.write(angle);
  lockState = target;

  Serial.print(F("[lock] "));
  Serial.print(lockStateStr(lockState));
  Serial.print(F(" angle="));
  Serial.println(angle);
  return true;
}

// --- Ultrasonic (gated) ------------------------------------------------------

#if ENABLE_ULTRASONIC

float readDistanceCmOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  const unsigned long duration =
      pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);

  if (duration == 0) {
    return NAN;
  }

  // Speed of sound ~343 m/s → cm = us / 58
  return duration / 58.0f;
}

float medianOfSamples() {
  float sorted[SENSOR_SAMPLE_COUNT];
  const int n = sampleFilled;
  for (int i = 0; i < n; i++) {
    sorted[i] = distanceSamples[i];
  }
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (sorted[j] < sorted[i]) {
        const float tmp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = tmp;
      }
    }
  }
  return sorted[n / 2];
}

void updateMailFromDistance(float cm) {
  if (isnan(cm)) {
    mailStatus = MailStatus::UNKNOWN;
    return;
  }
  if (cm < DISTANCE_MIN_CM || cm > DISTANCE_MAX_CM) {
    sensorStatus = SensorStatus::OUT_OF_RANGE;
    mailStatus = MailStatus::UNKNOWN;
    return;
  }

  sensorStatus = SensorStatus::OK;
  // Closer than (empty - threshold) ⇒ mail present
  if (cm <= (EMPTY_DISTANCE_CM - MAIL_THRESHOLD_CM)) {
    mailStatus = MailStatus::MAIL_DETECTED;
  } else {
    mailStatus = MailStatus::EMPTY;
  }
}

void sampleUltrasonic() {
  const unsigned long now = millis();
  if (now - lastSensorSampleMs < SENSOR_SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSensorSampleMs = now;

  const float cm = readDistanceCmOnce();
  if (isnan(cm)) {
    sensorStatus = SensorStatus::TIMEOUT;
    mailStatus = MailStatus::UNKNOWN;
    lastDistanceCm = NAN;
    lastSensorUpdateMs = now;
    Serial.println(F("[us] timeout / no echo"));
    return;
  }

  distanceSamples[sampleIndex] = cm;
  sampleIndex = (sampleIndex + 1) % SENSOR_SAMPLE_COUNT;
  if (sampleFilled < SENSOR_SAMPLE_COUNT) {
    sampleFilled++;
  }

  if (sampleFilled < SENSOR_SAMPLE_COUNT) {
    sensorStatus = SensorStatus::NOT_READY;
    mailStatus = MailStatus::UNKNOWN;
    lastDistanceCm = cm;
    lastSensorUpdateMs = now;
    Serial.print(F("[us] warming sample="));
    Serial.println(cm, 1);
    return;
  }

  const float filtered = medianOfSamples();
  lastDistanceCm = filtered;
  lastSensorUpdateMs = now;
  updateMailFromDistance(filtered);

  Serial.print(F("[us] cm="));
  Serial.print(filtered, 1);
  Serial.print(F(" mail="));
  Serial.println(mailStatusStr(mailStatus));
}

void initUltrasonic() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  sampleIndex = 0;
  sampleFilled = 0;
  sensorStatus = SensorStatus::NOT_READY;
  mailStatus = MailStatus::UNKNOWN;
  Serial.println(F("[us] HC-SR04 enabled (divider required on ECHO)"));
}

#endif  // ENABLE_ULTRASONIC

// --- Wi-Fi -------------------------------------------------------------------

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.println();
      Serial.println(F("[wifi] connected"));
      Serial.print(F("[wifi] IP: "));
      Serial.println(WiFi.localIP());
      Serial.print(F("[wifi] RSSI: "));
      Serial.println(WiFi.RSSI());
    }
    return;
  }

  wifiWasConnected = false;
  const unsigned long now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_INTERVAL_MS &&
      lastWifiAttemptMs != 0) {
    return;
  }
  lastWifiAttemptMs = now;

  Serial.println(F("[wifi] connecting..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool waitForWifiInitial() {
  Serial.print(F("[wifi] Connecting to "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptMs = millis();

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print('.');
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println();
      Serial.println(F("[wifi] initial connect timed out — will retry in loop"));
      return false;
    }
    yield();
  }

  wifiWasConnected = true;
  Serial.println();
  Serial.print(F("[wifi] IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

// --- HTTP handlers -----------------------------------------------------------

void handleRoot() {
  // STATUS_POLL_INTERVAL_MS is injected into the page script.
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#1a2332">
<title>Smart Mailbox</title>
<style>
:root {
  --bg: #1a2332;
  --bg-elev: #243044;
  --text: #f0f4f8;
  --muted: #9aabbc;
  --accent-lock: #c45c26;
  --accent-unlock: #2f6f4e;
  --danger: #c0392b;
  --ok: #3d8b6e;
  --radius: 16px;
  --touch: 52px;
}
* { box-sizing: border-box; }
html, body {
  margin: 0;
  min-height: 100%;
  font-family: "Segoe UI", system-ui, -apple-system, sans-serif;
  background:
    radial-gradient(ellipse at 20% 0%, #2a3a52 0%, transparent 50%),
    radial-gradient(ellipse at 100% 100%, #1e2d22 0%, transparent 45%),
    var(--bg);
  color: var(--text);
}
.wrap {
  max-width: 420px;
  margin: 0 auto;
  padding: 24px 18px 40px;
}
header h1 {
  margin: 0 0 4px;
  font-size: 1.65rem;
  font-weight: 700;
  letter-spacing: -0.02em;
}
header p {
  margin: 0 0 20px;
  color: var(--muted);
  font-size: 0.95rem;
}
.panel {
  background: var(--bg-elev);
  border-radius: var(--radius);
  padding: 18px 16px;
  margin-bottom: 14px;
}
.lock-banner {
  text-align: center;
  padding: 22px 12px;
  border-radius: var(--radius);
  margin-bottom: 14px;
  transition: background 0.25s ease, color 0.25s ease;
}
.lock-banner.LOCKED {
  background: linear-gradient(145deg, #3a2818, #2a1f14);
  border: 1px solid #6a3e22;
}
.lock-banner.UNLOCKED {
  background: linear-gradient(145deg, #1a3228, #14241c);
  border: 1px solid #2f6f4e;
}
.lock-banner .label {
  font-size: 0.8rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--muted);
  margin-bottom: 6px;
}
.lock-banner .value {
  font-size: 1.75rem;
  font-weight: 700;
}
.row {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  gap: 12px;
  padding: 10px 0;
  border-bottom: 1px solid rgba(255,255,255,0.06);
  font-size: 0.95rem;
}
.row:last-child { border-bottom: none; }
.row .k { color: var(--muted); }
.row .v { font-weight: 600; text-align: right; }
.actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
  margin: 18px 0 12px;
}
button {
  min-height: var(--touch);
  border: none;
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 700;
  letter-spacing: 0.04em;
  cursor: pointer;
  color: #fff;
  -webkit-tap-highlight-color: transparent;
}
button:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
button.unlock { background: var(--accent-unlock); }
button.lock { background: var(--accent-lock); }
button:active:not(:disabled) { transform: scale(0.98); }
.err {
  display: none;
  background: rgba(192, 57, 43, 0.2);
  border: 1px solid var(--danger);
  color: #ffb4ae;
  border-radius: 10px;
  padding: 12px 14px;
  margin-bottom: 14px;
  font-size: 0.9rem;
}
.err.show { display: block; }
.note {
  font-size: 0.78rem;
  color: var(--muted);
  line-height: 1.45;
  margin-top: 8px;
}
.dot {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-right: 6px;
  vertical-align: middle;
}
.dot.on { background: var(--ok); }
.dot.off { background: var(--danger); }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <h1>Smart Mailbox</h1>
    <p>Wi-Fi lock &amp; mail monitor</p>
  </header>

  <div id="err" class="err" role="alert"></div>

  <div id="lockBanner" class="lock-banner LOCKED">
    <div class="label">Lock state</div>
    <div class="value" id="lockState">—</div>
  </div>

  <div class="panel">
    <div class="row">
      <span class="k">Mail</span>
      <span class="v" id="mailStatus">—</span>
    </div>
    <div class="row">
      <span class="k">Distance</span>
      <span class="v" id="distance">—</span>
    </div>
    <div class="row">
      <span class="k">Sensor</span>
      <span class="v" id="sensorStatus">—</span>
    </div>
    <div class="row">
      <span class="k">Wi-Fi</span>
      <span class="v" id="wifi">—</span>
    </div>
    <div class="row">
      <span class="k">Last sensor update</span>
      <span class="v" id="lastUpdate">—</span>
    </div>
  </div>

  <div class="actions">
    <button type="button" class="unlock" id="btnUnlock">UNLOCK</button>
    <button type="button" class="lock" id="btnLock">LOCK</button>
  </div>

  <p class="note">
    Demonstration only: an SG90 and cardboard latch are not a security-rated lock.
    Use only on a trusted local network. Do not expose this device to the internet.
  </p>
</div>
<script>
(function () {
  var POLL_MS = )rawliteral";

  page += String(STATUS_POLL_INTERVAL_MS);

  page += R"rawliteral(;
  var busy = false;
  var errEl = document.getElementById("err");

  function showError(msg) {
    errEl.textContent = msg || "Request failed. Check that the mailbox is online.";
    errEl.classList.add("show");
  }
  function clearError() {
    errEl.classList.remove("show");
    errEl.textContent = "";
  }

  function applyStatus(s) {
    var lock = s.lockState || "UNKNOWN";
    document.getElementById("lockState").textContent = lock;
    var banner = document.getElementById("lockBanner");
    banner.className = "lock-banner " + (lock === "UNLOCKED" ? "UNLOCKED" : "LOCKED");

    document.getElementById("mailStatus").textContent =
      (s.mailStatus || "UNKNOWN").replace("_", " ");

    if (s.distanceCm === null || typeof s.distanceCm === "undefined") {
      document.getElementById("distance").textContent = "— cm";
    } else {
      document.getElementById("distance").textContent =
        Number(s.distanceCm).toFixed(1) + " cm";
    }

    document.getElementById("sensorStatus").textContent = s.sensorStatus || "—";

    var wifiHtml;
    if (s.wifiConnected) {
      wifiHtml = '<span class="dot on"></span>Online';
      if (s.wifiRssi !== null && typeof s.wifiRssi !== "undefined") {
        wifiHtml += " (" + s.wifiRssi + " dBm)";
      }
    } else {
      wifiHtml = '<span class="dot off"></span>Offline';
    }
    document.getElementById("wifi").innerHTML = wifiHtml;

    if (s.lastSensorUpdateMs === null || typeof s.lastSensorUpdateMs === "undefined") {
      document.getElementById("lastUpdate").textContent = "n/a";
    } else {
      var age = Math.max(0, Math.round((s.uptimeMs - s.lastSensorUpdateMs) / 1000));
      document.getElementById("lastUpdate").textContent = age + "s ago";
    }
  }

  function setBusy(v) {
    busy = v;
    document.getElementById("btnLock").disabled = v;
    document.getElementById("btnUnlock").disabled = v;
  }

  function refreshStatus() {
    return fetch("/api/status", { cache: "no-store" })
      .then(function (r) {
        if (!r.ok) throw new Error("Status HTTP " + r.status);
        return r.json();
      })
      .then(function (s) {
        clearError();
        applyStatus(s);
      })
      .catch(function () {
        showError("Cannot reach mailbox. Is it powered and on Wi-Fi?");
      });
  }

  function command(path) {
    if (busy) return;
    setBusy(true);
    clearError();
    fetch(path, { method: "POST", cache: "no-store" })
      .then(function (r) {
        if (!r.ok) throw new Error("Command HTTP " + r.status);
        return r.json();
      })
      .then(function (s) {
        applyStatus(s);
      })
      .catch(function () {
        showError("Lock command failed. Servo state may be unchanged.");
      })
      .then(function () {
        setBusy(false);
      });
  }

  document.getElementById("btnLock").addEventListener("click", function () {
    command("/api/lock");
  });
  document.getElementById("btnUnlock").addEventListener("click", function () {
    command("/api/unlock");
  });

  refreshStatus();
  setInterval(refreshStatus, POLL_MS);
})();
</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=UTF-8", page);
}

void handleApiStatus() {
  if (server.method() != HTTP_GET) {
    sendJson(405, "{\"error\":\"method_not_allowed\"}");
    return;
  }
  sendStatus(200);
}

void handleApiLock() {
  if (server.method() != HTTP_POST) {
    sendJson(405, "{\"error\":\"method_not_allowed\"}");
    return;
  }
  if (!setLock(true)) {
    sendJson(500, "{\"error\":\"lock_failed\"}");
    return;
  }
  sendStatus(200);
}

void handleApiUnlock() {
  if (server.method() != HTTP_POST) {
    sendJson(405, "{\"error\":\"method_not_allowed\"}");
    return;
  }
  if (!setLock(false)) {
    sendJson(500, "{\"error\":\"unlock_failed\"}");
    return;
  }
  sendStatus(200);
}

void handleNotFound() {
  sendJson(404, "{\"error\":\"not_found\"}");
}

// --- Setup / loop ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.println(F("=== Smart Mailbox ==="));
  Serial.print(F("deviceId="));
  Serial.println(DEVICE_ID);
  Serial.print(F("ultrasonic="));
  Serial.println(ENABLE_ULTRASONIC ? F("ON") : F("OFF"));
  Serial.print(F("mqtt="));
  Serial.println(ENABLE_MQTT ? F("ON") : F("OFF"));

  lockServo.attach(SERVO_PIN);
  lockServo.write(LOCK_ANGLE);
  lockState = LockState::LOCKED;
  delay(400);
  Serial.print(F("[servo] boot LOCKED @ "));
  Serial.println(LOCK_ANGLE);

#if ENABLE_ULTRASONIC
  initUltrasonic();
#else
  sensorStatus = SensorStatus::DISABLED;
  mailStatus = MailStatus::UNKNOWN;
  lastDistanceCm = NAN;
  Serial.println(F("[us] disabled — set ENABLE_ULTRASONIC 1 after divider is ready"));
#endif

  waitForWifiInitial();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/lock", HTTP_POST, handleApiLock);
  server.on("/api/unlock", HTTP_POST, handleApiUnlock);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println(F("[http] dashboard on port 80"));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[http] open http://"));
    Serial.println(WiFi.localIP());
  }
}

void loop() {
  server.handleClient();
  ensureWifi();

#if ENABLE_ULTRASONIC
  sampleUltrasonic();
#endif

  yield();
}
