# PRP-00002: Synchronized Lift Platform with 5 WN17CM3 Standing Desks

## Status

| Phase | Status |
|-------|--------|
| Phase 1: Slave Component & Configuration | ✅ Complete |
| Phase 2: Sync Controller Component | ⚠️ Partial — control-loop logic written, but the ESP-NOW transport is a log-only stub (`send_command_to_desk()`/`broadcast_command()` in `platform_sync_controller.cpp`); the 2-ESP32 comm test has never run |
| Phase 3: Master Configuration | ⚠️ Partial — master YAML exists and manual hold-to-move switches drive desk 1 directly, but platform controller commands reach no desk (stub transport), slave heights are simulated by an interval in `platform_master.yaml`, and the commented-out `espnow:` block is invalid for ESPHome 2025.11 |
| Phase 4: Safety Features | ⚠️ Partial — e-stop/timeout logic exists in C++ but emergency stop physically stops nothing (it goes through the stub broadcast); unverified |
| Phase 5: Hardware Setup & Testing | ⏳ Pending |

### Known Gaps Before Phase 5

- [ ] Implement ESP-NOW send/receive (native `espnow:` component, available since ESPHome 2025.8; register slave MACs in a `peers:` list; do NOT set a channel option — it is rejected when a `wifi:` block exists, and ESP-NOW follows the WiFi channel)
- [ ] Wire the sync controller to desk 1's local UART command path — including on emergency stop, which currently reaches no desk
- [ ] Remove the simulated slave-height interval from `platform_master.yaml`
- [ ] Honor the 100ms command re-send requirement in the control loop (WN17CM3 desks stop unless move commands are re-sent continuously)
- [ ] Hardware-verify that mid-motion height frames are received after the 20-150cm sanity-filter widening (run `wn17cm3_decoder` logging at VERBOSE to see accepted AND rejected frames)

## Overview

5 standing desks supporting a single heavy platform (200+ lbs). All desks must remain synchronized within **0.5cm tolerance** during movement to prevent platform tilt.

Uses 5 ESP32 dev boards (one per desk) coordinated via ESP-NOW. The master runs a **closed-loop synchronization controller** that actively throttles faster desks to match the slowest.

### Why This Approach

- **Real-time control**: Master continuously monitors all heights and sends stop/resume commands
- **Minimal wiring**: Each ESP32 sits next to its desk with 3 short wires
- **Low latency**: ESP-NOW delivers commands in <5ms
- **Fault tolerance**: Emergency stop if any desk diverges >1cm or stops responding

---

## Synchronization Control System

### Control Loop Algorithm (runs on Master at ~20Hz)

```
CONSTANTS (configurable in YAML):
  PAUSE_THRESHOLD = 0.3cm    # Pause desk if ahead by this much
  RESUME_THRESHOLD = 0.2cm   # Resume when within this of slowest
  EMERGENCY_THRESHOLD = 1.0cm # Stop all if spread exceeds this (default 1cm)
  COMM_TIMEOUT = 250ms       # Stop all if any desk stops reporting

STATE:
  heights[1..5]              # Last known height of each desk
  last_update[1..5]          # Timestamp of last height report
  desk_state[1..5]           # MOVING, PAUSED, or STOPPED
  target_height              # Where we're trying to go
  direction                  # UP or DOWN

CONTROL_LOOP:
  1. Receive height updates from slaves (continuously via ESP-NOW)

  2. Check for communication failures:
     FOR each desk:
       IF now() - last_update[desk] > COMM_TIMEOUT:
         EMERGENCY_STOP_ALL("Desk {desk} communication lost")

  3. Calculate spread:
     min_height = MIN(heights[1..5])
     max_height = MAX(heights[1..5])
     spread = max_height - min_height

  4. Emergency check:
     IF spread > EMERGENCY_THRESHOLD:  # Default 1.0cm
       EMERGENCY_STOP_ALL("Platform tilt detected: {spread}cm")

  5. Throttle fast desks:
     FOR each desk WHERE desk_state == MOVING:
       IF heights[desk] > min_height + PAUSE_THRESHOLD:
         SEND_STOP(desk)
         desk_state[desk] = PAUSED

  6. Resume paused desks:
     FOR each desk WHERE desk_state == PAUSED:
       IF heights[desk] <= min_height + RESUME_THRESHOLD:
         SEND_RESUME(desk, direction)
         desk_state[desk] = MOVING

  7. Check completion:
     IF ALL desks within 0.3cm of target_height:
       STOP_ALL()
       state = COMPLETE
```

### ESP-NOW Message Protocol

**Height Reports (Slave → Master, ~10Hz per slave):**
```
"H<id>:<height>"     e.g., "H2:72.35"
```

**Control Commands (Master → Individual Slave):**
```
"U"   = Start moving UP
"D"   = Start moving DOWN
"S"   = STOP immediately
"P1"  = Go to Preset 1 (etc.)
```

**Broadcast Commands (Master → All Slaves):**
```
"*U"  = All desks move UP
"*D"  = All desks move DOWN
"*S"  = EMERGENCY STOP ALL
"*P1" = All desks go to Preset 1
```

### Safety Features

1. **Heartbeat monitoring**: If any slave stops reporting height for 250ms, all desks stop
2. **Emergency threshold**: If spread exceeds 1cm (configurable), immediate stop (platform tilting)
3. **Pause and retry**: If tolerance exceeded, pause until re-synced
4. **Startup check**: Before any movement, verify all 5 slaves are responding

### Standalone Testing Mode

Each slave can run independently for testing without the master:

```yaml
# In platform_slave.yaml, set:
substitutions:
  standalone_mode: "true"  # Disables ESP-NOW, enables local HA control
```

When `standalone_mode: true`:
- ESP-NOW is disabled
- WiFi connects directly to Home Assistant
- Local buttons exposed: up/down/stop/presets
- Useful for testing individual desks before platform integration

---

## Hardware

### Shopping List

| Item | Qty | Purpose | Notes |
|------|-----|---------|-------|
| ESP32 DevKit (ESP-WROOM-32) | 5 | Controllers | ~$6-8 each |
| RJ45 Breakout Board | 5 | Connect to desk ports | ~$2 each |
| Bi-directional logic level shifter (3.3V/5V) | 5 | UART level shifting | ~$1-2 each; mandatory, desk uses 5V logic |
| Dupont jumper wires | 1 pack | Short connections | ~$5 |

**Total: ~$50-70**

No separate power supplies needed - desks provide 5V via RJ45 pin 5.

### Wiring (Per Desk)

The desk controller uses 5V logic; ESP32 GPIOs are 3.3V and NOT 5V-tolerant. Both UART
lines must go through a bi-directional level shifter (this matches the only configuration
verified on real hardware — see `configs/platform/platform_standalone.yaml`):

```
Desk RJ45 Port                                ESP32
─────────────                                 ─────
Pin 2 (Blue)   ◄── level shifter (HV◄─LV) ─── GPIO17 (TX2)
Pin 3 (Black)  ────────────────────────────►  GND
Pin 4 (Yellow) ─── level shifter (HV─►LV) ──► GPIO16 (RX2)
Pin 5 (Red)    ─┬──────────────────────────►  VIN (5V power)
                └─► level shifter HV supply (LV supply from ESP32 3V3)
```

### Architecture

```
                    Home Assistant
                          │
                        WiFi
                          │
                   ┌──────┴──────┐
                   │ Master ESP32│ ◄──UART──► Desk 1
                   │  (platform-master)
                   └──────┬──────┘
                          │
              ESP-NOW Broadcast (<5ms)
                          │
        ┌─────────┬───────┼───────┬─────────┐
        │         │       │       │         │
   ┌────┴────┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐
   │ Slave 2 │ │Slave│ │Slave│ │Slave│
   └────┬────┘ └──┬──┘ └──┬──┘ └──┬──┘
        │         │       │       │
      Desk 2   Desk 3   Desk 4   Desk 5
```

---

## Files to Create

### 1. `components/platform_sync_controller/`

New ESPHome external component for the master sync controller:

**`__init__.py`** - ESPHome config schema:
```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CONF_PAUSE_THRESHOLD = "pause_threshold"
CONF_RESUME_THRESHOLD = "resume_threshold"
CONF_EMERGENCY_THRESHOLD = "emergency_threshold"
CONF_COMM_TIMEOUT = "comm_timeout"
CONF_NUM_DESKS = "num_desks"

platform_sync_ns = cg.esphome_ns.namespace("platform_sync_controller")
PlatformSyncController = platform_sync_ns.class_("PlatformSyncController", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PlatformSyncController),
    cv.Optional(CONF_PAUSE_THRESHOLD, default=0.3): cv.float_,
    cv.Optional(CONF_RESUME_THRESHOLD, default=0.2): cv.float_,
    cv.Optional(CONF_EMERGENCY_THRESHOLD, default=1.0): cv.float_,
    cv.Optional(CONF_COMM_TIMEOUT, default=250): cv.positive_int,
    cv.Optional(CONF_NUM_DESKS, default=5): cv.int_range(min=2, max=10),
}).extend(cv.COMPONENT_SCHEMA)
```

**`platform_sync_controller.h`**:
```cpp
#pragma once
#include "esphome/core/component.h"
#include "esphome/components/espnow/espnow.h"

namespace esphome {
namespace platform_sync_controller {

enum class DeskState : uint8_t { STOPPED, MOVING, PAUSED };
enum class PlatformState : uint8_t { IDLE, MOVING_UP, MOVING_DOWN, ERROR };

class PlatformSyncController : public Component {
 public:
  void setup() override;
  void loop() override;

  // Configuration
  void set_pause_threshold(float t) { pause_threshold_ = t; }
  void set_resume_threshold(float t) { resume_threshold_ = t; }
  void set_emergency_threshold(float t) { emergency_threshold_ = t; }
  void set_comm_timeout(uint32_t t) { comm_timeout_ = t; }
  void set_num_desks(uint8_t n) { num_desks_ = n; }

  // Control methods
  void move_up();
  void move_down();
  void stop();
  void preset(uint8_t num);
  void move_to_height(float height);

  // Status
  float get_platform_height();
  float get_max_spread();
  bool is_moving();
  bool has_error();

 protected:
  float pause_threshold_{0.3};
  float resume_threshold_{0.2};
  float emergency_threshold_{1.0};
  uint32_t comm_timeout_{250};
  uint8_t num_desks_{5};

  float heights_[10];
  uint32_t last_update_[10];
  DeskState desk_state_[10];
  PlatformState platform_state_{PlatformState::IDLE};

  void on_height_received(uint8_t desk_id, float height);
  void send_command(uint8_t desk_id, const char* cmd);
  void broadcast_command(const char* cmd);
  void emergency_stop(const char* reason);
  void run_control_loop();
};

}  // namespace platform_sync_controller
}  // namespace esphome
```

### 2. `configs/platform/platform_slave.yaml`

```yaml
substitutions:
  desk_id: "2"  # Change for each slave: 2, 3, 4, 5
  device_name: platform-desk-${desk_id}
  standalone_mode: "false"  # Set "true" for individual desk testing

esphome:
  name: ${device_name}
  friendly_name: "Platform Desk ${desk_id}"

esp32:
  board: esp32dev
  framework:
    type: arduino

# Conditional WiFi based on standalone mode
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  power_save_mode: none

# Only enable API in standalone mode
api:
  encryption:
    key: !secret api_encryption_key
  # Conditionally disabled when not standalone

logger:
  level: DEBUG

external_components:
  - source:
      type: local
      path: ../../components
    components: [standing_desk_height]

# UART for desk communication
uart:
  id: standing_desk_uart
  rx_pin: GPIO16
  tx_pin: GPIO17
  baud_rate: 19200

# Desk height sensor
sensor:
  - platform: standing_desk_height
    id: desk_height
    name: "Desk ${desk_id} Height"
    unit_of_measurement: "cm"
    variant: wn17cm3
    on_value:
      then:
        - lambda: |-
            // Report height to master via ESP-NOW (when not standalone)
            if (std::string("${standalone_mode}") != "true") {
              char msg[16];
              snprintf(msg, sizeof(msg), "H${desk_id}:%.2f", x);
              // id(espnow_component).broadcast(msg);
            }

# ESP-NOW for platform coordination (disabled in standalone mode)
# espnow:
#   id: espnow_component
#   on_receive:
#     - lambda: |-
#         std::string cmd((char*)data, len);
#         if (cmd == "U" || cmd == "*U") id(desk_height)->move_up();
#         else if (cmd == "D" || cmd == "*D") id(desk_height)->move_down();
#         else if (cmd == "S" || cmd == "*S") id(desk_height)->stop();
#         // ... handle presets

# Standalone mode controls (always available for local testing)
button:
  - platform: template
    name: "Desk ${desk_id} Up"
    on_press:
      - standing_desk_height.move_up: desk_height
      - delay: 100ms
      - standing_desk_height.stop: desk_height

  - platform: template
    name: "Desk ${desk_id} Down"
    on_press:
      - standing_desk_height.move_down: desk_height
      - delay: 100ms
      - standing_desk_height.stop: desk_height

  - platform: template
    name: "Desk ${desk_id} Stop"
    on_press:
      - standing_desk_height.stop: desk_height

  - platform: template
    name: "Desk ${desk_id} Preset 1"
    on_press:
      - standing_desk_height.preset:
          id: desk_height
          preset: 1
```

### 3. `configs/platform/platform_master.yaml`

```yaml
substitutions:
  device_name: platform-master

esphome:
  name: ${device_name}
  friendly_name: "Platform Controller"

esp32:
  board: esp32dev
  framework:
    type: arduino

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  power_save_mode: none

api:
  encryption:
    key: !secret api_encryption_key

ota:
  platform: esphome
  password: !secret ota_password

logger:
  level: DEBUG

external_components:
  - source:
      type: local
      path: ../../components
    components: [standing_desk_height, platform_sync_controller]

# UART for local desk 1
uart:
  id: standing_desk_uart
  rx_pin: GPIO16
  tx_pin: GPIO17
  baud_rate: 19200

# Local desk sensor (desk 1)
sensor:
  - platform: standing_desk_height
    id: desk_1_height
    name: "Desk 1 Height"
    unit_of_measurement: "cm"
    variant: wn17cm3

# Platform sync controller
platform_sync_controller:
  id: platform_controller
  pause_threshold: 0.3
  resume_threshold: 0.2
  emergency_threshold: 1.0  # Configurable emergency threshold
  comm_timeout: 250
  num_desks: 5

# Platform sensors
sensor:
  - platform: template
    name: "Platform Height"
    id: platform_height
    unit_of_measurement: "cm"
    lambda: return id(platform_controller).get_platform_height();
    update_interval: 250ms

  - platform: template
    name: "Platform Max Spread"
    id: platform_spread
    unit_of_measurement: "cm"
    lambda: return id(platform_controller).get_max_spread();
    update_interval: 250ms

# Platform controls
button:
  - platform: template
    name: "Platform Up"
    icon: mdi:arrow-up-bold
    on_press:
      - lambda: id(platform_controller).move_up();

  - platform: template
    name: "Platform Down"
    icon: mdi:arrow-down-bold
    on_press:
      - lambda: id(platform_controller).move_down();

  - platform: template
    name: "Platform Stop"
    icon: mdi:stop
    on_press:
      - lambda: id(platform_controller).stop();

  - platform: template
    name: "Platform Preset 1"
    on_press:
      - lambda: id(platform_controller).preset(1);

  - platform: template
    name: "Platform Preset 2"
    on_press:
      - lambda: id(platform_controller).preset(2);

# Target height control
number:
  - platform: template
    name: "Platform Target Height"
    id: platform_target
    unit_of_measurement: "cm"
    min_value: 60
    max_value: 125
    step: 0.5
    optimistic: true
    set_action:
      - lambda: id(platform_controller).move_to_height(x);

# Status sensors
binary_sensor:
  - platform: template
    name: "Platform Moving"
    lambda: return id(platform_controller).is_moving();

  - platform: template
    name: "Platform Error"
    lambda: return id(platform_controller).has_error();
```

---

## Implementation Phases

### Phase 1: Slave Component & Configuration
1. Create `configs/platform/platform_slave.yaml`
2. Implement standalone mode with local desk control
3. Add ESP-NOW height reporting (high frequency, on change)
4. Add ESP-NOW command receiver
5. **Test**: Verify single desk works in standalone mode

### Phase 2: Sync Controller Component
1. Create `components/platform_sync_controller/__init__.py`
2. Create `components/platform_sync_controller/platform_sync_controller.h/.cpp`
3. Implement ESP-NOW peer discovery/management
4. Implement height tracking for all desks
5. Implement basic control loop (throttle/resume logic)
6. **Test**: Verify ESP-NOW communication between 2 ESP32s

### Phase 3: Master Configuration
1. Create `configs/platform/platform_master.yaml`
2. Integrate local desk 1 control
3. Add Home Assistant entities (buttons, sensors)
4. Add platform status sensors (height, spread, sync status)
5. **Test**: Verify master can control local desk and see slave heights

### Phase 4: Safety Features
1. Implement communication timeout detection
2. Implement emergency stop on threshold exceeded
3. Implement startup health check (all slaves responding)
4. Add error state reporting to Home Assistant
5. **Test**: Verify emergency stop triggers correctly

### Phase 5: Hardware Setup & Testing
1. Flash slave firmware to all 5 ESP32s (unique `desk_id` each)
2. Flash master firmware to ESP32 #1
3. Wire each ESP32 to desk via RJ45 breakout
4. Mount ESP32s under desks
5. Test individual desks in standalone mode
6. Test synchronized platform movement
7. Tune thresholds based on real-world behavior

---

## Home Assistant Entities

### Sensors
- `sensor.platform_height` - Current platform height (average of all desks)
- `sensor.desk_1_height` through `sensor.desk_5_height` - Individual desk heights
- `sensor.platform_max_spread` - Current max height difference between desks

### Buttons
- `button.platform_up` - Move platform up (with sync control)
- `button.platform_down` - Move platform down (with sync control)
- `button.platform_stop` - Stop all desks immediately
- `button.platform_preset_1` through `button.platform_preset_4`

### Number
- `number.platform_target_height` - Set target height for synchronized movement

### Binary Sensors
- `binary_sensor.platform_moving` - True while platform is in motion
- `binary_sensor.platform_error` - True if sync error or communication failure

---

## Critical Files Reference

| File | Purpose |
|------|---------|
| `components/standing_desk_height/standing_desk_height.cpp:142-169` | `move_up()`, `move_down()`, `stop()`, `preset()` methods |
| `components/standing_desk_height/wn17cm3_decoder.cpp` | UART command sending |
| `configs/test_esp32_wn17cm3.yaml` | Base ESP32 config to extend |
| `standing-desk-serial-protocol.md` | RJ45 pinout and WN17CM3 protocol |

## External Resources

- [ESPHome ESP-NOW Component](https://esphome.io/components/espnow.html) - ESP-NOW documentation
- [ESP-NOW Latency Testing](https://hackaday.io/project/164132-hello-world-for-esp-now/log/160572-latency-and-reliability-testing) - Latency benchmarks
