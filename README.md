# MeshCore for the M5Stack Cardputer ADV

A standalone [MeshCore](https://github.com/meshcore-dev/MeshCore) firmware for the M5Stack Cardputer ADV
fitted with the Cap LoRa-1262. Your Cardputer becomes a self-contained mesh messenger: you type and read
messages on its own keyboard and screen, check your position on the built-in GPS, find your contacts with
a compass, and browse offline maps stored on a microSD card — no phone required.

If you would rather use the official MeshCore app, the same image also contains the stock companion
firmware. Switching to "phone mode" is a single menu entry, and pressing **esc** three times brings you
back. Both modes share the same radio identity, so the rest of the mesh sees a single node.

> The user interface is currently in French.

## What you need

- an **M5Stack Cardputer ADV** (the original Cardputer is not supported yet);
- the **Cap LoRa-1262**, with its antenna screwed on *before* you power up — transmitting without an
  antenna can damage the radio;
- optionally, a **microSD card** (FAT32) for backups and offline maps.

No Cap at hand? The `cardputer_adv_demo` build runs the whole interface on its own, with simulated
contacts that chat back and a simulated GPS walk.

## Installing

Ready-to-flash images are attached to each [release](../../releases). Each file is a full image to be
written at address `0x0`:

| File | What you get |
| --- | --- |
| `cardputer_adv_dual` | **recommended** — keyboard messenger with offline maps, plus phone mode |
| `cardputer_adv_standalone` | keyboard messenger only |
| `cardputer_adv_standalone_map` | keyboard messenger with offline maps |
| `cardputer_adv_companion_ble` | phone mode only (official MeshCore companion) |
| `cardputer_adv_demo` | demo without the LoRa Cap |

The simplest route is a browser-based flasher such as
[ESPConnect](https://thelastoutpostworkshop.github.io/ESPConnect/). From a terminal:

```bash
esptool.py --chip esp32s3 write_flash 0x0 cardputer_adv_dual.bin
```

The standalone, map and demo images are also laid out for M5Launcher, which gives them all the free
flash for message history. A detailed, step-by-step
guide (in French) is in [`app/FLASH.md`](app/FLASH.md).

## Building from source

The project uses [PlatformIO](https://platformio.org/). MeshCore is pulled in as a git submodule, so
remember to clone recursively:

```bash
git clone --recursive <repository-url>
cd <repository>
pio run -e cardputer_adv_dual_standalone -e cardputer_adv_dual_companion
python3 tools/make_dual.py          # writes .pio/build/firmware-dual.bin
```

Flash it and open the serial monitor:

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 -b 921600 write_flash 0x0 .pio/build/firmware-dual.bin
pio device monitor -b 115200
```

The log is designed to make field debugging painless: the boot log is replayed as soon as a monitor
connects, a one-line status report is printed every ten seconds, and the standalone firmware has a small
console — type `help` to list its commands (raw keyboard events, raw GPS sentences, I2C scan, and so on).

If you forgot `--recursive`, run `git submodule update --init` before building.

## Testing without hardware

Most of the code can be exercised on a regular Linux machine:

```bash
make -C app/sim -j8 test run
```

`test` runs the keyboard, GPS and I/O-expander drivers against software models of the actual chips,
including the awkward cases: a GPS talking at an unexpected baud rate, a keyboard controller that loses
its configuration after a power glitch, a full event FIFO. `run` replays scripted user sessions through
the real interface and saves screenshots in `app/sim/build/`. Each session runs twice: once with keys
injected directly, once with every key press going through the emulated keyboard controller.

The radio, the audio codec and the battery are not emulated; those still need a real device.

## Project layout

```
meshcore/            official MeshCore, as a git submodule pinned to a release
meshcore_base.ini    MeshCore's shared build settings (generated, do not edit)
platformio.ini       the firmware variants we build
variant/             board support: display, serial logging, dual-boot switching, partition tables
app/                 the keyboard interface, the portable drivers and the PC simulator
boards/              PlatformIO board definition
tools/               dual-image builder, offline-map converter, MeshCore update scripts
```

None of our code lives inside MeshCore, and MeshCore itself is never modified. This keeps upgrades
cheap: moving to a new MeshCore release is a matter of pointing the submodule at another tag.

## Staying up to date with MeshCore

Official MeshCore releases are published as `companion-vX.Y.Z` tags; their `main` branch follows those
releases, while day-to-day development happens on `dev`. This project tracks releases only.

To upgrade by hand:

```bash
python3 tools/update_meshcore.py --dry-run   # which release, and which of our dependencies changed
python3 tools/update_meshcore.py             # move to the latest release (or name a specific tag)
```

Then rebuild every environment, run the simulator, test on a device and commit `meshcore` together with
`meshcore_base.ini`.

There is also a scheduled GitHub workflow, *MeshCore update*, that does all of this every Monday. When a
new release is out, it prepares the upgrade on a separate branch and runs the full set of checks: every
firmware must build, stay within the memory budget and fit its flash partition, and every driver test and
interface scenario must pass. If anything fails, `main` is left untouched. If everything passes, it opens
a pull request so you can flash the result and try it before merging — or, if you set the repository
variable `MESHCORE_AUTO_MERGE` to `true`, merges it straight away (never across a major version bump).

For the workflow to push branches and open pull requests, enable *Read and write permissions* and
*Allow GitHub Actions to create and approve pull requests* under *Settings → Actions → General*.

## Offline maps

Maps are stored on the microSD card as raw tiles the Cardputer can draw without decoding. The converter
in `tools/tiles/` builds them from an MBTiles file (for instance one exported from QGIS) or from a tile
server whose terms allow offline use. Note that the public OpenStreetMap tile servers do not. See
[`tools/tiles/README.md`](tools/tiles/README.md).

## Credits

Built on [MeshCore](https://github.com/meshcore-dev/MeshCore) by the meshcore-dev team, and on M5Stack's
M5Unified and M5GFX libraries. The keyboard layout follows M5Stack's M5Cardputer library.
