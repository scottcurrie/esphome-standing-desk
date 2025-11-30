# WN17CM3 Hardware Testing Guide

This guide covers testing the WN17CM3/WN17CM2 standing desk controller with an ESP32.

## Hardware Required

- ESP32 DevKit board (e.g., HiLetgo ESP-WROOM-32)
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
┌─────────────────────────────────────────────────────────────┐
│                     BREADBOARD SETUP                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   RJ45 Breakout                      ESP32 DevKit           │
│   (desk cable)                                              │
│                                                             │
│   Pin 2 (RX/Blue)   ───────────────► GPIO17 (TX2)          │
│   Pin 3 (GND/Black) ───────────────► GND                   │
│   Pin 4 (TX/Yellow) ───────────────► GPIO16 (RX2)          │
│   Pin 5 (5V/Red)    ───────(optional)─► VIN                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**Note**: The desk uses 5V logic levels. ESP32 GPIOs are 3.3V but generally tolerate 5V input. For production use, consider a level shifter on the RX line.

## Quick Start

### 1. Update secrets

Edit `configs/secrets.yaml` with your WiFi credentials:

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
cd configs
esphome run test_esp32_wn17cm3.yaml
```

### 3. Monitor logs

After flashing, keep USB connected to watch debug output:

```bash
esphome logs test_esp32_wn17cm3.yaml
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

## Testing Order

1. **Read-only first** - Verify height readings work before testing movement commands
2. **Short presses** - The Up/Down buttons send 100ms pulses (safe for testing)
3. **Presets** - Test if your desk responds to preset commands (1-4)

## Troubleshooting

### No height readings

- Check wiring: TX from desk goes to RX (GPIO16) on ESP32
- Verify baud rate is 19200 (different from other desk controllers)
- Check GND is connected between desk and ESP32

### Height readings but no control

- Check wiring: RX on desk goes to TX (GPIO17) on ESP32
- Verify the desk remote still works (controller may need wake-up)

### Garbage data in logs

- Wrong baud rate (must be 19200)
- Wiring issue or loose connection
- Try a different GPIO pair

## Protocol Reference

See `standing-desk-serial-protocol.md` in the project root for full protocol details including:

- Message format (`:command checksum;`)
- Checksum calculation
- Key codes for movement commands
