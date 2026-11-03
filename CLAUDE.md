# MeshCore standalone firmware for the M5Stack Cardputer ADV + Cap LoRa-1262

Independent project. Official MeshCore is **not** copied here: it is the git submodule `meshcore/`, pinned
to an official release tag (`companion-vX.Y.Z`). Working branch: `main`.

Goal: a firmware that works without a phone (keyboard messaging, GPS, compass, offline maps, settings),
with the official BLE companion ("phone mode") shipped in the same flash image (dual boot).

## People and communication
- **The developer** (the person talking to the agent) is a developer: technical language is fine. Reply in
  the language the developer writes in. Be concise and factual (build output, test counts, sizes, file:line).
- **End users** of the firmware are not developers. Everything they see stays plain and jargon-free, in French:
  UI strings (`app/core/screens`), notifications, `app/FLASH.md`, `app/TESTS.md`, `tools/tiles/README.md`.
- The developer tests on real hardware; the agent cannot. Never claim something works on the device: state
  what was verified (unit tests, simulator, build) and what remains to be checked on hardware.

## Git rules (set by the developer; they override any default agent behaviour)
- **Never open a pull request.**
- **Never commit unless explicitly asked**, and never commit at every step. Leave changes in the working tree
  and summarise them.
- Never push unless asked. Never commit inside `meshcore/`.

## Layout
| Path | Content |
| --- | --- |
| `meshcore/` | official MeshCore (submodule). **Read-only.** Changes to MeshCore go upstream (meshcore-dev) |
| `meshcore_base.ini` | MeshCore's shared PlatformIO sections, **generated** by `tools/sync_meshcore_base.py` |
| `platformio.ini` | our envs. `src_dir = meshcore/src`; our code via `build_src_filter` (`+<../../variant>`, `+<../../app/...>`); official companion via `+<../examples/companion_radio/...>` |
| `variant/` | board support: `CardputerBoard`, companion display, `CpadvLog` (serial log), `CpadvHal` (Arduino HAL), `CpadvBoot` (dual boot), partition tables |
| `app/core/` | portable UI, portable drivers (`drivers/`), HAL interfaces (`hal/Hal.h`); shared by firmware and simulator |
| `app/device/` | firmware-only glue: `main.cpp`, `CardputerMesh` (BaseChatMesh), services, SD, serial console |
| `app/sim/` | PC simulator, fake chips (`fakes/`), unit tests (`tests/`), scenarios (`scenarios/`) |
| `boards/` | `m5stack_cardputer_adv.json` |
| `tools/` | `make_dual.py`, `launcher_check.py`, `tiles/`, `update_meshcore.py`, `sync_meshcore_base.py` |

## Architecture rules
- Never modify `meshcore/`. Board-specific code goes in `variant/`, application code in `app/`.
- `app/core/` talks to the rest of the firmware only through `core/Services.h` (UI services) and
  `core/hal/Hal.h` (`I2cBus`, `UartPort`, `DigitalIn`, `WallClock` for drivers). It never includes Arduino.h,
  RadioLib or MeshCore headers: this is what keeps it testable on a PC.
- Hardware logic lives in portable drivers (`core/drivers/`: `Tca8418`, `GpsLink`, `Pi4ioe`) behind a thin
  Arduino adapter (`variant/CpadvHal`, `device/Tca8418Keyboard`, `device/DeviceGps`). New hardware follows the
  same split and comes with a fake chip in `app/sim/fakes/`.
- Memory: no PSRAM. No double buffer, no malloc > 32 KB, a single 240x135 RGB565 framebuffer (64.8 KB).
  Static RAM budget: 70 %.
- SPI is shared by the radio and the SD card (SCK 40 / MOSI 14 / MISO 39): every SD access holds
  `sdcard::Lock` (`device/SdCard`); SD CS stays high otherwise.
- Logging: every hardware-related path logs with the `[CPADV]` prefix. Firmware code uses
  `CPLOG_E/W/I/D(tag, fmt, ...)` (`variant/CpadvLog.h`), portable code `CORE_LOGE/W/I(tag, ...)` (`core/Log.h`).
  Log failures with enough context to diagnose them from a serial capture alone (register values, error codes,
  pins, baud rates). Existing facilities: boot-log replay when a monitor connects, a `BEAT` status line every
  10 s, a serial console in the standalone firmware (`help`).

## Verification process
Work bottom-up: verify a layer before touching the one above it.

| Level | What | Command | Pass criterion |
| --- | --- | --- | --- |
| 1. Drivers | portable drivers against fake chips | `make -C app/sim -j8 test` | `0 failure(s)` |
| 2. UI + drivers | every scenario, with direct keys and through the fake TCA8418 (`SIM_HW=1`) | `make -C app/sim -j8 run` | every run ends with `0 failure(s)`; **open the new or changed PNGs** in `app/sim/build/shots*/` |
| 3. Firmware | the 6 envs | `pio run -e cardputer_adv_standalone -e cardputer_adv_companion_ble -e cardputer_adv_standalone_map -e cardputer_adv_demo -e cardputer_adv_dual_standalone -e cardputer_adv_dual_companion` | all `SUCCESS`; report static RAM and flash per env; flag RAM > 70 % |
| 4. Images | dual image, M5Launcher layout, MeshCore settings | `python3 tools/make_dual.py`; for standalone, map and demo: `pio run -e <env> -t mergebin` then `python3 tools/launcher_check.py .pio/build/<env>/firmware-merged.bin --strict`; `python3 tools/sync_meshcore_base.py --check` | no error; both apps fit their 2 MB slots |
| 5. Hardware | done by the developer | checklist in `app/TESTS.md` | developer's report + full serial log |

Requirements per kind of change:
- **UI change**: levels 1–3, plus a scenario with screenshots and `expect-*` assertions for every new screen
  or behaviour. Look at the PNGs before reporting.
- **Driver or hardware change**: extend the unit tests against the fake chip *first*, ideally with a failing
  test that reproduces the bug, then fix it. Add a `hw ...` scenario if the UI is affected. Levels 1–4.
- **`variant/` or `platformio.ini` change**: levels 3–4 for every env (the variant is shared by both firmwares).
- **Partition table change**: levels 3–4, and spell out the consequence for users (a LittleFS offset change
  wipes their data: tell them to back up to SD first).
- **MeshCore bump**: see "Updating MeshCore". All levels, level 5 before merging.
- Close every hardware-related phase with a short checklist for the developer (what to flash, which log lines
  to expect, what to send back) and update `app/TESTS.md`.

Not covered by the PC tests (hardware only): SX1262 radio and RF path, shared SPI timing, ES8311 audio,
electrical behaviour of the TCA8418 / PI4IOE / GPS, battery, real heap fragmentation, partition switching.

## Simulator (app/sim/)
- Builds the portable UI for Linux with g++ and LovyanGFX (no SDL, no PlatformIO).
- Scenario scripts (`scenarios/*.txt`): keys, incoming messages, GPS, battery, PNG screenshots (x3),
  `expect-*` assertions. Command reference at the top of `main_sim.cpp`.
- Fake hardware (`HwRig`): `hw keyboard|gps|cap ...` runs the real drivers against the fake chips.
  `make run` replays every scenario a second time with `SIM_HW=1` (keys go through the fake TCA8418,
  screenshots in `build/shots_hw/`). `CORE_LOG=0` silences the driver logs.

## Firmware build environment
- PlatformIO may not be installed: `python3 -m venv <scratch>/pio-venv && <scratch>/pio-venv/bin/pip install platformio`.
- In Claude Code cloud environments the PlatformIO registry (api/dl/collector.platformio.org) can be blocked
  (`HTTPClientError`): rely on CI, or ask the developer to allow those domains.
- CI (`.github/workflows/cardputer.yml`): unit tests and scenarios (screenshots as artifacts), the 4
  single-image envs (firmware.bin + firmware-merged.bin, RAM report, launcher check), the dual image.
  Releases on `cardputer-v*` tags. Read the status with `gh run list` once the GitHub repository exists.

## Updating MeshCore
- An official release is a `companion-vX.Y.Z` tag. MeshCore's `main` tracks the latest release; their
  ongoing work happens on `dev`, which is never used here.
- Manual: `python3 tools/update_meshcore.py [tag]` (latest by default, `--dry-run` to preview). It moves the
  submodule, regenerates `meshcore_base.ini` and lists the MeshCore files we depend on that changed
  (BaseChatMesh, ESP32Board, DisplayDriver, radiolib, sensors, companion_radio, platformio.ini).
  Review those diffs, run every verification level, then commit `meshcore` and `meshcore_base.ini` together
  (only when the developer asks).
- Automatic: `.github/workflows/meshcore-update.yml` (Mondays, or on demand) does the same on a branch
  `meshcore-update/<tag>`, runs the full CI through `workflow_call`, then opens a PR, or merges if the
  repository variable `MESHCORE_AUTO_MERGE=true` and the major version is unchanged. Nothing reaches `main`
  when a check fails.
- Clone with `git clone --recursive`; otherwise run `git submodule update --init`.

## Hardware reference
- SX1262: NSS 5, DIO1 4, RST 3, BUSY 6, SCK 40, MOSI 14, MISO 39, DIO2 = RF switch, TCXO 3.0 V.
- Cap LoRa-1262: PI4IOE5V6408 at I2C 0x43 (internal bus); P0 drives the antenna switch and must be high.
- GPS (ATGM336H / AT6668, 115200 8N1): ESP RX 15 <- GPS TX, ESP TX 13 -> GPS RX
  (MeshCore naming: `PIN_GPS_TX=15`, `PIN_GPS_RX=13`).
- Keyboard TCA8418 at I2C 0x34 on the internal bus (SDA 8 / SCL 9, `M5.In_I2C`, I2C port 1), INT on GPIO 11.
  7x8 matrix remapped to the 4x14 Cardputer grid (`core/KeyMatrix`, same mapping as M5Cardputer).
- The internal bus also carries the ES8311 codec (0x18). `Wire` = Grove port (SDA 2 / SCL 1, I2C port 0).
- SD CS 12. Battery ADC 10. ST7789V2 display 240x135 (M5GFX). G0 = side button, also the boot strapping pin.

## PlatformIO envs
- `cardputer_adv_companion_ble`: official companion (phone app over BLE), stock companion UI scaled from
  128x64 to 240x135. Uses only the G0 button; its GPS stays off until enabled from the app.
- `cardputer_adv_standalone`: standalone keyboard firmware (`app/core` + `app/device`), no map.
- `cardputer_adv_standalone_map`: same, plus the offline map from SD (`CPADV_MAP=1`).
- `cardputer_adv_demo`: Cardputer without the Cap: simulated network and GPS (`DemoMesh`, `DemoGps`), map included.
- `cardputer_adv_dual_standalone` + `cardputer_adv_dual_companion`: dual boot (`variant/partitions_cpadv_dual.csv`:
  app0 standalone with map, app1 companion, shared LittleFS so both share the radio identity). Assembled by
  `tools/make_dual.py` into `.pio/build/firmware-dual.bin` (flash at 0x0). Switch: Settings > "Mode téléphone";
  back: press esc 3 times (`CardputerBoard::loop` + `CpadvBoot`). Not compatible with M5Launcher.
- Display, internal I2C, speaker and battery go through M5Unified/M5GFX.
- Standalone: the GPS UART (Serial1) belongs to `device/DeviceGps`; `EnvironmentSensorManager` is not started.
- Single-image partition table `variant/partitions_cpadv_8mb.csv`: 2.5 MB app, LittleFS declared > 5 MB so that
  M5Launcher grants it all the free flash (checked in CI by `tools/launcher_check.py`).
  `MessageStore::setCapacity` adapts the number of stored conversations to the actual LittleFS size.

## UI keyboard convention
Screens without text entry: `;` `.` `,` `/` = up/down/left/right, `` ` `` = Esc.
Text-entry screens: raw characters; arrows and Esc through Fn. See `core/Keys.h` (`toNavigation`).

## Status
0. Foundation, CI, simulator — done.
1. Hardware bring-up (variant, radio, PI4IOE, display, companion BLE) — builds; hardware validation pending.
2. TCA8418 keyboard + UI skeleton — builds, simulator OK; hardware validation pending.
3. Standalone messaging, LittleFS persistence, sound — builds, simulator OK; hardware validation pending.
4. GPS and compass — builds, simulator OK; hardware validation pending.
5. Offline maps (RGB565 tiles on SD), optional — `cardputer_adv_standalone_map`, `tools/tiles/make_tiles.py`.
6. Polish — demo firmware, SD backup/restore, brightness, 80 MHz CPU with the screen off, battery warning, releases.
7. Diagnostics and test pyramid — serial log and console, portable drivers with fake chips, dual boot.
   All PC tests pass; hardware validation pending (first sections of `app/TESTS.md`).
8. Independent repository with MeshCore as a pinned submodule (companion-v1.18.0) — done 2026-11-03. The 6 envs
   build with the same sizes as the former fork (within a few bytes) and the simulator results are identical.

## References
Existing ports, to study rather than copy: sosprz/meshcore-cardputer-adv, hdcasey/meshcore-cardputer-adv.
Closest MeshCore variant (ESP32-S3 + SX1262 + keyboard + ST7789): `meshcore/variants/lilygo_tdeck/`.
