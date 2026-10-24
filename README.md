# urec-live-firmware

Automatic rep counting for selectorized gym machines, reproducing the beacon/gateway system of Bian, Rupp & Magno, [*Fully Automatic Gym Exercises Recording: An IoT Solution*](https://arxiv.org/abs/2305.17594). It feeds machine status into UREC Live.

```
LIS3DH ─INT1─► Feather nRF52840 ─BLE iBeacon─► ESP32-C3 gateway ─► Firebase RTDB
(on the stack)  counts reps,        (minor = reps)  scans, tracks sets    + UREC Live machine status
                sleeps between
```

| Folder | What |
|---|---|
| `src/beacon/` | Feather nRF52840 Express firmware: two-phase LIS3DH interrupt rep counter, iBeacon bursts, USB tuning console |
| `src/gateway/` | ESP32-C3-DevKitM-1 firmware: continuous BLE scan, set tracking, Firebase REST, UREC Live status |
| `lib/gym_core/` | Board-independent C++: packet format, rep counter, machine tracker |
| `test/test_gym_core/` | Unity tests for `lib/gym_core` |
| `docs/` | [Walkthrough](docs/rep-counting-walkthrough.md) and Firebase security rules |
| `beacon-breadboard-schematic.md` | Breadboard layout and wiring for the beacon |

## Quick start

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -e beacon -t upload && pio device monitor -e beacon     # Feather nRF52840
pio run -e gateway -t upload && pio device monitor -e gateway   # ESP32-C3
pio test -e gateway                                             # unit tests on a connected board
```

The gateway runs serial-only until you copy `src/gateway/secrets.example.h` to `src/gateway/secrets.h` (git-ignored) and fill in Wi-Fi, Firebase and the UREC Live URL. Everything else, from wiring to tuning and Firebase setup, is in [docs/rep-counting-walkthrough.md](docs/rep-counting-walkthrough.md).
