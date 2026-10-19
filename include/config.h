#pragma once

// ---------------------------------------------------------------------------
// Smart Mailbox — central configuration
// Adjust pins, angles, and sensor thresholds here. Rebuild after changes.
// ---------------------------------------------------------------------------

// Device identity (also used in JSON status and future MQTT topics)
static const char* DEVICE_ID = "smart-mailbox";

// Feature flags
// Stage 3: set ENABLE_ULTRASONIC to true ONLY after the ECHO voltage divider
// is verified and wired. Leave false for Stages 1–2.
#ifndef ENABLE_ULTRASONIC
#define ENABLE_ULTRASONIC 1
#endif

// Stage 5: MQTT is not required for local operation.
#ifndef ENABLE_MQTT
#define ENABLE_MQTT 0
#endif

// --- Servo (SG90) ---
// Signal on D6 = GPIO12 on NodeMCU. Power from 5 V (VIN), never 3V3.
static const int SERVO_PIN = D6;
static const int LOCK_ANGLE = 0;
static const int UNLOCK_ANGLE = 90;

// --- HC-SR04 (Stage 3+) ---
// TRIG: D5 = GPIO14
// ECHO: D2 = GPIO4  — MUST come through a 5 V → ~3.3 V voltage divider
static const int TRIG_PIN = D5;
static const int ECHO_PIN = D2;

// Ultrasonic timing and filtering
static const unsigned long SENSOR_SAMPLE_INTERVAL_MS = 500;
static const unsigned long ECHO_TIMEOUT_US = 30000;  // ~5 m max theoretical
static const int SENSOR_SAMPLE_COUNT = 5;
static const float DISTANCE_MIN_CM = 2.0f;
static const float DISTANCE_MAX_CM = 400.0f;

// Mail detection — calibrate for YOUR box (see README)
// EMPTY_DISTANCE_CM: typical reading when mailbox is empty
// MAIL_THRESHOLD_CM: treat as mail if distance <= (EMPTY - THRESHOLD)
static const float EMPTY_DISTANCE_CM = 25.0f;
static const float MAIL_THRESHOLD_CM = 8.0f;

// --- Wi-Fi ---
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000;
static const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;

// --- Web UI ---
static const unsigned long STATUS_POLL_INTERVAL_MS = 2000;
