# Smart Mailbox

ESP8266 (NodeMCU) firmware for a **Wi-Fi controlled mailbox lock** with an optional HC-SR04 mail detector and a mobile-friendly local dashboard.

> **Demo disclaimer:** An SG90 and a cardboard/paper latch are for learning and demonstration only. This is **not** a security-rated mailbox or door lock.

## Features (current)

| Feature | Status |
|---------|--------|
| Local web dashboard on the ESP8266 | Stages 1–2 (ready) |
| LOCK / UNLOCK via website only (no physical buttons) | Ready |
| JSON status API | Ready |
| SG90 servo on D6 (0° lock / 90° unlock) | Ready |
| HC-SR04 mail detection | Code ready, **disabled until voltage divider verified** |
| MQTT / cloud dashboard | Documented for Stage 5 — not required |

## Requirements

- [PlatformIO](https://platformio.org/) in VS Code or Cursor
- NodeMCU-style ESP8266 (ESP-12E/12F)
- SG90 micro servo
- USB cable
- Phone or laptop on the **same 2.4 GHz Wi-Fi** network
- For Stage 3+: HC-SR04, breadboard, jumpers, and a verified **1 kΩ + 2 kΩ** (or equivalent) Echo voltage divider

## Project layout

```text
Mailbox/
├── platformio.ini
├── include/
│   ├── config.h              # pins, angles, sensor thresholds, feature flags
│   ├── secrets.h.example     # template (committed)
│   └── secrets.h             # your Wi-Fi password (gitignored)
├── src/
│   └── main.cpp
├── context.yaml
├── README.md
├── LICENSE
└── .gitignore
```

The dashboard HTML/CSS/JS is **embedded in firmware** — one PlatformIO upload, no filesystem image.

---

## Stage 1–2: Build, upload, and open the dashboard

### 1. Configure Wi-Fi secrets

```powershell
Copy-Item include\secrets.h.example include\secrets.h
```

Edit `include/secrets.h`:

```cpp
const char* WIFI_SSID = "YourNetwork";
const char* WIFI_PASSWORD = "YourPassword";
```

Never commit `secrets.h`. It is listed in `.gitignore`.

### 2. Build

**CLI:**

```powershell
pio run
```

**VS Code / Cursor:** PlatformIO sidebar → **Build** (checkmark).

### 3. Upload

Connect the ESP8266 by USB. Select the correct COM port if prompted.

```powershell
pio run -t upload
```

`upload_speed` is **115200** in `platformio.ini` for reliability. If uploads are solid, you may try `460800` or `921600` later.

### 4. Serial monitor

```powershell
pio device monitor
```

Or PlatformIO → **Monitor**. Baud: **115200**.

Expected lines include:

```text
=== Smart Mailbox ===
[servo] boot LOCKED @ 0
[us] disabled — set ENABLE_ULTRASONIC 1 after divider is ready
[wifi] IP: 192.168.x.x
[http] open http://192.168.x.x
```

Use the **printed IP** — do not hard-code an old address.

### 5. Phone test procedure

1. Join the same Wi-Fi as the ESP8266 (not guest isolation if possible).
2. Open `http://<printed-ip>/` in the phone browser.
3. Confirm lock banner shows **LOCKED**.
4. Tap **UNLOCK** — servo should move to 90°, UI updates only after the server responds.
5. Tap **LOCK** — servo returns to 0°.
6. Status panel should refresh every ~2 seconds (Wi-Fi RSSI, uptime-related fields).
7. Turn off Wi-Fi on the phone briefly — an error banner should appear; restore Wi-Fi and it should recover.

**Servo horn:** leave the mechanical horn/latch **unattached** until angles are confirmed.

---

## Hardware — Stage 1–2 (servo only)

| SG90 wire | ESP8266 |
|-----------|---------|
| Brown / black | **G / GND** |
| Red | **VIN** (USB 5 V) — **never 3V3** |
| Orange / yellow | **D6** (GPIO12) |

**Power notes**

- Servo ground and ESP8266 ground must be common.
- If the board resets, disconnects, or the servo jitters when moving, use a **separate regulated 5 V** supply for the servo, share GND with the ESP8266, and do **not** parallel supplies in a way that back-feeds USB.
- Distinguish pins: **3V3** = 3.3 V logic power; **VIN** ≈ 5 V from USB; **GND**; labelled **D#** GPIOs.

Angles are in `include/config.h`:

```cpp
static const int SERVO_PIN = D6;
static const int LOCK_ANGLE = 0;
static const int UNLOCK_ANGLE = 90;
```

---

## API schema

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/` | Dashboard HTML |
| `GET` | `/api/status` | JSON status (source of truth) |
| `POST` | `/api/lock` | Move servo to lock angle; returns status JSON |
| `POST` | `/api/unlock` | Move servo to unlock angle; returns status JSON |

Example `/api/status` (ultrasonic disabled):

```json
{
  "deviceId": "smart-mailbox",
  "lockState": "LOCKED",
  "mailStatus": "UNKNOWN",
  "distanceCm": null,
  "sensorStatus": "DISABLED",
  "wifiConnected": true,
  "wifiRssi": -55,
  "uptimeMs": 12345,
  "lastSensorUpdateMs": null
}
```

| Field | Values |
|-------|--------|
| `lockState` | `LOCKED`, `UNLOCKED` |
| `mailStatus` | `MAIL_DETECTED`, `EMPTY`, `UNKNOWN` |
| `sensorStatus` | `DISABLED`, `NOT_READY`, `OK`, `TIMEOUT`, `OUT_OF_RANGE` |
| `distanceCm` | number or `null` |

Invalid methods return `405` with `{"error":"method_not_allowed"}`. Unknown paths return `404`.

The UI **does not** mark success until the POST returns HTTP 2xx and JSON.

---

## Stage 3: HC-SR04 — resistor check before wiring Echo

**Never connect HC-SR04 ECHO directly to an ESP8266 GPIO.** Echo can be ~5 V; ESP8266 GPIOs are 3.3 V.

### Verify resistor values

1. **Unplug USB** from the ESP8266.
2. Take candidate resistors **out of circuit**.
3. Multimeter → resistance (Ω).
4. You need approximately:
   - **1 kΩ** (1000 Ω) and **2 kΩ** (2000 Ω), or another pair that divides 5 V to ~3.3 V (e.g. 2 kΩ / 4 kΩ).
5. ±5–10% tolerance is fine.
6. A resistor that *looked* like red-red-violet-gold is **not confirmed** until you measure it. Do not assume it is correct.
7. If you lack suitable resistors: obtain 1× 1 kΩ + 1× 2 kΩ (or a 5 V→3.3 V level shifter). **Leave ECHO disconnected** until then.

### Voltage divider

```text
HC-SR04 ECHO
      |
    [1 kΩ]
      |
      +--------------------> ESP8266 D2 (GPIO4)
      |
    [2 kΩ]
      |
     GND
```

`Vout ≈ 5 × 2 / (1 + 2) ≈ 3.33 V`

### Full ultrasonic wiring

| HC-SR04 | Connection |
|---------|------------|
| VCC | Suitable **5 V** (VIN or regulated) |
| GND | Common **GND** |
| TRIG | **D5** (GPIO14) |
| ECHO | Through divider → **D2** (GPIO4) |

All grounds (ESP, servo supply, sensor) must be common.

### Enable in firmware

1. Wire TRIG and the **divided** Echo only after resistors are verified.
2. In `include/config.h`, set:

```cpp
#define ENABLE_ULTRASONIC 1
```

3. Rebuild and upload: `pio run -t upload`
4. Watch Serial for `[us] cm=...` lines.

While disabled (`ENABLE_ULTRASONIC 0`), the dashboard correctly shows `sensorStatus: DISABLED` and `mailStatus: UNKNOWN` — it does **not** invent mail presence.

### Calibration

1. Mount the sensor pointing at the mailbox bottom / mail pile.
2. Leave the box empty; record several Serial distances; average → set `EMPTY_DISTANCE_CM`.
3. Insert representative mail; record distances.
4. Set `MAIL_THRESHOLD_CM` so mail is detected when  
   `distance <= (EMPTY_DISTANCE_CM - MAIL_THRESHOLD_CM)`.
5. Retest empty vs with mail several times.
6. Timeouts / no echo → `UNKNOWN` / `TIMEOUT`, never a false `EMPTY`.

The ultrasonic sensor **never** auto-unlocks. Only the website (or future MQTT command path) can unlock.

---

## Stage 4: Full mailbox integration checklist

- [ ] Ultrasonic enabled and calibrated
- [ ] Dashboard shows live distance and mail status
- [ ] Invalid readings stay UNKNOWN
- [ ] LOCK/UNLOCK still work while sampling
- [ ] Pull Echo (safely, power off first) → UNKNOWN; HTTP still responds

---

## Stage 5: Optional MQTT (later)

**MQTT** is a publish/subscribe messaging protocol. **HiveMQ Cloud** (if that is what “hivemind” referred to) is a **hosted MQTT broker service**, not a different protocol. You would create an account with a provider, get host/port/credentials, and put them in `secrets.h` — never invent credentials, and never put broker passwords in frontend JavaScript.

Local dashboard remains the primary interface. MQTT is **not** required for Stages 1–4.

### Planned topic contract (implement only after local system is stable)

| Topic | Direction | Payload |
|-------|-----------|---------|
| `smart-mailbox/<device-id>/status` | device → broker | same JSON as `/api/status` |
| `smart-mailbox/<device-id>/command` | broker → device | `{"action":"lock"}` or `{"action":"unlock"}` |
| `smart-mailbox/<device-id>/event` | device → broker | lock changes, sensor errors |

Commands must call the same `setLock()` path as the website.

When enabling later:

1. Set `ENABLE_MQTT 1` in `config.h`
2. Add `knolleary/PubSubClient` to `lib_deps` in `platformio.ini`
3. Fill MQTT fields in `secrets.h`
4. Prefer TLS with proper certificate validation for any cloud broker
5. **Do not** port-forward the ESP8266 to the public internet

Remote dashboards should talk to a **backend or broker**, not directly to a home ESP8266 on the open internet.

---

## Stage 6: Mechanical installation

1. Confirm LOCK/UNLOCK angles with the horn **off**.
2. Temporarily tape the servo to the cardboard flap.
3. Adjust `LOCK_ANGLE` / `UNLOCK_ANGLE` if the latch binds.
4. Secure permanently only when travel is reliable.

---

## Troubleshooting

| Symptom | What to try |
|---------|-------------|
| Upload fails | Correct COM port; close Serial Monitor; try holding FLASH/BOOT if your board needs it; keep `upload_speed = 115200` |
| Wi-Fi never connects | 2.4 GHz only; check SSID/password in `secrets.h`; some guest networks block device-to-device |
| Page loads, servo silent | Confirm signal on **D6**, common GND, red on **VIN**/5 V not 3V3 |
| Board resets when servo moves | Separate 5 V servo supply + shared GND |
| Echo connected, board dies / GPIO damage risk | Power off immediately; verify divider before reconnecting Echo |
| Distance always timeout | Wiring, 5 V to VCC, common GND, Echo through divider to D2 |
| UI says success without move | Should not happen with this firmware — hard-refresh; confirm you are on the device IP |

---

## Security

- Use only on a **trusted LAN**.
- No login on the dashboard yet — anyone on the network who knows the IP can operate the lock.
- Do not expose the ESP8266 to the public internet.
- Do not recommend router port forwarding for this prototype.
- Keep secrets out of git.

## License

MIT — see [LICENSE](LICENSE).
