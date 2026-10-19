# Smart Mailbox

ESP8266 (NodeMCU) firmware for a Wi-Fi mailbox lock. The board runs a small web dashboard so you can lock and unlock a servo latch from any phone or browser on the same network.

## Features

- Servo lock/unlock on pin **D6**
- Web UI at the board’s local IP (`/` , `/lock`, `/unlock`)
- Starts locked on boot
- Placeholder UI fields for mail presence and distance (sensors not wired yet)

## Hardware

| Part | Notes |
|------|--------|
| ESP8266 NodeMCU v2 | Or Wemos D1 Mini (see `platformio.ini`) |
| Hobby servo | Signal → **D6**, power from a suitable supply |
| Wi-Fi | 2.4 GHz network |

Adjust `LOCK_ANGLE` / `UNLOCK_ANGLE` in `src/main.cpp` if your latch needs different positions.

## Setup

1. Install [PlatformIO](https://platformio.org/) (VS Code / Cursor extension or CLI).
2. Copy credentials template:

   ```bash
   cp include/secrets.h.example include/secrets.h
   ```

   On Windows PowerShell:

   ```powershell
   Copy-Item include\secrets.h.example include\secrets.h
   ```

3. Edit `include/secrets.h` with your Wi-Fi SSID and password (`secrets.h` is gitignored).
4. Build and upload:

   ```bash
   pio run -t upload
   pio device monitor
   ```

5. Open the **Mailbox IP** printed in the serial monitor in a browser.

## Project layout

```
platformio.ini     # board / framework config
src/main.cpp       # firmware + embedded dashboard
include/secrets.h.example
context.yaml       # project context for agents & contributors
```

## Security

This demo has **no login**. Anyone on your LAN who knows the IP can open the lock. Use only on a trusted network, and never commit real Wi-Fi passwords.

## License

MIT — see [LICENSE](LICENSE).
