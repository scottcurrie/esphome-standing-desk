# WN17CM3 Hardware Testing Guide

This guide covers testing the WN17CM3/WN17CM2 standing desk controller with an ESP32,
both as a single standalone desk and as the 5-desk synchronized platform
(see `docs/prp/00002-synchronized-platform-lift.md`).

## Hardware Required

- ESP32 DevKit board (e.g., HiLetgo ESP-WROOM-32)
- Bi-directional 3.3V/5V logic level shifter (mandatory — see Wiring)
- RJ45 breakout board
- Breadboard and jumper wires
- Ethernet cable (to connect desk controller to breakout board)

## Wiring

### RJ45 Pinout (from desk controller)

| Pin | Color (per protocol doc) | Function |
|-----|-------------------------|----------|
| 2   | Blue                    | RX (controller receives) |
| 3   | Black                   | GND |
| 4   | Yellow                  | TX (controller sends) |
| 5   | Red                     | 5V |

### Connections to ESP32

```
┌──────────────────────────────────────────────────────────────────┐
│                        BREADBOARD SETUP                          │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   RJ45 Breakout          Level Shifter          ESP32 DevKit     │
│   (desk cable)           (HV side | LV side)                     │
│                                                                  │
│   Pin 2 (RX/Blue)   ◄──── HV1 ◄─┼─ LV1 ◄─────── GPIO17 (TX2)    │
│   Pin 3 (GND/Black) ──────GND───┼──GND─────────► GND             │
│   Pin 4 (TX/Yellow) ────► HV2 ──┼─► LV2 ───────► GPIO16 (RX2)   │
│   Pin 5 (5V/Red)    ─┬───► HV supply | LV supply ◄── 3V3         │
│                      └─────────────────────────► VIN (5V power)  │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

**Note**: The desk uses 5V logic levels. ESP32 GPIOs are 3.3V and are **NOT 5V-tolerant**
(per Espressif specifications). A bi-directional level shifter is **mandatory** on both
UART lines — desk TX → ESP RX and ESP TX → desk RX — with the desk's 5V feeding the
shifter's HV side and the ESP32's 3V3 feeding the LV side. This wiring (matching the
header comments in `configs/platform/platform_standalone.yaml`) is the only configuration
that has worked on real hardware. (Note: the December bring-up fixed the level shifting
and a height sanity-filter bug concurrently, so the shifter alone is not proven to have
fixed RX — but do not attempt direct 5V-to-GPIO wiring.)

## Quick Start (single desk, standalone)

The current single-desk configuration is `configs/platform/platform_standalone.yaml`,
which uses its own secrets file in `configs/platform/`.

### 1. Update secrets

Edit `configs/platform/secrets.yaml` with your WiFi credentials:

```yaml
wifi_ssid: "YourNetwork"
wifi_password: "YourPassword"
api_encryption_key: "generate-a-32-byte-base64-key"
ota_password: "something-secure"
```

To generate an API encryption key:
```bash
esphome wizard dummy.yaml  # generates a key you can copy
# Or use: openssl rand -base64 32
```

### 2. Compile and flash

```bash
cd configs/platform
esphome run platform_standalone.yaml
```

### 3. Monitor logs

Serial logging is disabled in the platform configs (`baud_rate: 0`, to avoid UART
conflicts), so logs arrive over the network:

```bash
esphome logs platform_standalone.yaml
```

For the first test on any board, raise the decoder's log level to VERBOSE so you can see
**rejected** height frames as well as accepted ones (out-of-range heights are dropped at
VERBOSE, which made them invisible at DEBUG during the December bring-up):

```yaml
logger:
  level: DEBUG
  baud_rate: 0
  logs:
    wn17cm3_decoder: VERBOSE
```

## What to Expect

When working correctly, you should see in the logs:

- Raw UART messages like `:D71.10B;` being received
- Parsed height values (e.g., `Desk Height: 71.1 cm`)
- Button press/release commands being sent when you use the controls

Example log output:
```
[D][standing_desk_height:xxx]: Received: :D71.10B;
[D][sensor:xxx]: 'Desk Height': Sending state 71.1 cm
```

Known WN17CM3 behaviors from real hardware:
- The desk only moves while move commands are re-sent continuously (~every 100ms).
- Height frames may only appear during "display session" activity; if you see height
  spam only at the travel limits, check the VERBOSE log for frames being rejected by
  the sanity filter.

## Testing Order (single desk)

1. **Read-only first** - Verify height readings work before testing movement commands
2. **Short presses** - The Up/Down buttons send 100ms pulses (safe for testing)
3. **Presets** - Test if your desk responds to preset commands (1-4)

## 5-Board Platform Test Procedure

Do these stages in order. Do not skip ahead — the synchronized system has never been
hardware-tested, and the platform must not be attached until stage 4 passes.

### Stage 1: Flash and verify each board standalone

Flash each board and bench-verify it against a real desk in standalone mode before any
multi-board testing. Per-board flash table:

| Board | Role | Config file | `desk_id` substitution | Command (from `configs/platform/`) |
|-------|------|-------------|------------------------|------------------------------------|
| 1 | Master (drives desk 1) | `platform_master.yaml` | n/a (master is desk 1) | `esphome run platform_master.yaml` |
| 2 | Slave | `platform_slave.yaml` | `"2"` | `esphome run platform_slave.yaml` |
| 3 | Slave | `platform_slave.yaml` | `"3"` | `esphome run platform_slave.yaml` |
| 4 | Slave | `platform_slave.yaml` | `"4"` | `esphome run platform_slave.yaml` |
| 5 | Slave | `platform_slave.yaml` | `"5"` | `esphome run platform_slave.yaml` |

For each slave, edit the `desk_id` substitution in `platform_slave.yaml` before
compiling (or keep one copy per desk). For standalone bench-testing a slave board, set
`standalone_mode: "true"`; set it back to `"false"` for platform operation.

Per-board checklist:
- [ ] Boots and joins WiFi; shows up in Home Assistant
- [ ] Height readings received during motion (with `wn17cm3_decoder: VERBOSE` logging)
- [ ] Hold-to-move up/down works and the desk stops on release
- [ ] Record the board's WiFi MAC address (needed for the ESP-NOW peer list)

### Stage 2: 2-board ESP-NOW bench test

Before involving all five boards, verify ESP-NOW between the master and ONE slave on
the bench (no desks needed for the comms half of this test):
- [ ] Master receives the slave's height reports (real, not simulated — the simulation
      interval in `platform_master.yaml` must be removed/disabled first)
- [ ] Slave receives and acts on pause/resume/stop commands from the master
- [ ] Both boards are associated to the same WiFi AP/channel (ESP-NOW silently fails
      across channels; pin the AP channel on multi-AP networks)
- [ ] Unplug the slave and confirm the master declares comm loss within the 250ms timeout

### Stage 3: All 5 boards, desks attached, NO platform

- [ ] All 5 desks report heights to the master
- [ ] Synchronized up/down moves keep spread within tolerance (watch
      `sensor.platform_max_spread`)
- [ ] Emergency stop verification: trigger e-stop mid-motion and confirm **every desk
      physically stops**; also power off one slave mid-motion and confirm the comm-loss
      e-stop stops the rest

### Stage 4: First loaded test

Only after stage 3 passes cleanly: attach the platform, re-run synchronized moves at
low travel first, and keep a hand on mains power as the manual e-stop of last resort.

## Troubleshooting

### No height readings

- Check wiring: TX from desk goes through the level shifter to RX (GPIO16) on ESP32
- Verify baud rate is 19200 (different from other desk controllers)
- Check GND is connected between desk, level shifter, and ESP32
- Enable `wn17cm3_decoder: VERBOSE` logging — frames may be arriving but getting
  rejected by the height sanity filter

### Height readings but no control

- Check wiring: RX on desk goes through the level shifter to TX (GPIO17) on ESP32
- Verify the desk remote still works (controller may need wake-up)
- Move commands must be re-sent continuously (~100ms interval) or the desk stops

### Garbage data in logs

- Wrong baud rate (must be 19200)
- Wiring issue or loose connection
- Try a different GPIO pair

## Protocol Reference

See `standing-desk-serial-protocol.md` in the project root for full protocol details including:

- Message format (`:command checksum;`)
- Checksum calculation
- Key codes for movement commands
