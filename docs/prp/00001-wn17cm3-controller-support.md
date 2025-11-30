# PRP-00001: WN17CM3 Standing Desk Controller Support

## Status

| Phase | Status | Commit |
|-------|--------|--------|
| Phase 1: Read-Only Decoder | ✅ Complete | `8c660cd` |
| Phase 2: UART Control Commands | ✅ Complete | - |
| Phase 3: YAML Configuration | ✅ Complete | - |

## Overview

Add support for the Hangzhou Winner WN17CM3 (and WN17CM2) controller to the existing `standing_desk_height` ESPHome component. This controller uses an ASCII text-based UART protocol at 19200 baud with checksum validation, and supports bidirectional communication for both reading height and sending control commands.

## Protocol Summary

Based on reverse-engineered documentation in `standing-desk-serial-protocol.md`:

- **UART**: 19200 baud, 8N1
- **Message Format**: `:command checksum;` (ASCII text)
- **Checksum**: Lower byte of sum of command ASCII bytes, as 2-char uppercase hex string
- **Display Command**: `:D71.10B;` where "71.1" is height (cm), "0B" is checksum
- **Key Commands**: `:KUAM2E;` (Up pressed), `:KDAB12;` (Down released)
- **Keys**: `UA`=Up, `DA`=Down, ` 1`-` 4`=presets (space prefix), ` S`=M button
- **Actions**: `M`=pressed, `B`=released

## Files to Create

### 1. `components/standing_desk_height/wn17cm3_decoder.h`

```cpp
#pragma once

#include <stdint.h>
#include "decoder.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace standing_desk_height {

class Wn17cm3Decoder : public Decoder {
public:
  bool put(uint8_t b) override;
  float decode() override;

  void set_uart(uart::UARTDevice *uart) { this->uart_ = uart; }

  // Command sending
  void send_key_pressed(const char *key);
  void send_key_released(const char *key);

protected:
  uart::UARTDevice *uart_{nullptr};

  // Line buffer (accumulate between ':' and ';')
  static const size_t BUF_SIZE = 32;
  char buf_[BUF_SIZE];
  size_t buf_len_ = 0;
  bool in_message_ = false;

  float last_height_ = -1;

  void reset();
  uint8_t calculate_checksum(const char *data, size_t len);
  bool process_message();
  bool parse_display_command();
  void send_command(const char *cmd);
};

}
}
```

### 2. `components/standing_desk_height/wn17cm3_decoder.cpp`

Implements:
- **Line-buffered parsing**: Accumulate bytes between `:` and `;`, then process
- **Checksum validation**: Calculate checksum, compare with received hex chars
- **Display command parsing**: Extract height from `D` commands (handles `,F600` suffix for flashing mode)
- **Command sending**: Build key commands with checksum, send via UART

Key implementation pattern:
```cpp
bool Wn17cm3Decoder::put(uint8_t b) {
  if (b == ':') {
    reset();
    in_message_ = true;
    return false;
  }
  if (!in_message_) return false;
  if (b == ';') {
    in_message_ = false;
    return process_message();
  }
  if (buf_len_ < BUF_SIZE - 1) {
    buf_[buf_len_++] = b;
  }
  return false;
}

bool Wn17cm3Decoder::process_message() {
  if (buf_len_ < 3) return false;  // Min: 1 cmd char + 2 checksum chars

  // Validate checksum (last 2 chars are hex checksum)
  uint8_t received = (hex_to_nibble(buf_[buf_len_-2]) << 4) | hex_to_nibble(buf_[buf_len_-1]);
  uint8_t expected = calculate_checksum(buf_, buf_len_ - 2);
  if (received != expected) return false;

  // Parse display command
  if (buf_[0] == 'D') return parse_display_command();
  return false;
}
```

### 3. `configs/desks/wn17cm3.yaml`

Desk-specific substitutions:
```yaml
substitutions:
  standing_desk_uart_rx_pin: "D2"
  standing_desk_uart_tx_pin: "D1"
  standing_desk_variant: "wn17cm3"
  standing_desk_min_height: "60"
  standing_desk_max_height: "125"
  standing_desk_height_units: "cm"
```

### 4. `configs/template_wn17cm3.yaml`

New template with 19200 baud and UART control buttons:
```yaml
uart:
  id: standing_desk_uart
  rx_pin: ${standing_desk_uart_rx_pin}
  tx_pin: ${standing_desk_uart_tx_pin}
  baud_rate: 19200

sensor:
  - platform: standing_desk_height
    id: desk_height
    name: ${desk_height_name}
    unit_of_measurement: ${standing_desk_height_units}
    variant: ${standing_desk_variant}

number:
  - platform: template
    id: target_desk_height
    # ... uses standing_desk_height.move_up/move_down/stop actions

button:
  - platform: template
    name: "Desk Up"
    on_press:
      - standing_desk_height.move_up: { id: desk_height }
  # ... Desk Down, Preset 1-4 buttons
```

## Files to Modify

### 1. `components/standing_desk_height/decoder_variant.h`

Add enum value:
```cpp
enum DecoderVariant : uint8_t {
  DECODER_VARIANT_UNKNOWN,
  DECODER_VARIANT_JARVIS,
  DECODER_VARIANT_UPLIFT,
  DECODER_VARIANT_OMNIDESK,
  DECODER_VARIANT_WN17CM3,  // ADD
  DECODER_VARIANT_COUNT
};
```

### 2. `components/standing_desk_height/decoder_variant.cpp`

Add string mapping:
```cpp
case DECODER_VARIANT_WN17CM3:
  return LOG_STR("wn17cm3");
```

### 3. `components/standing_desk_height/standing_desk_height.h`

Add:
- Include `wn17cm3_decoder.h`
- Control methods: `move_up()`, `move_down()`, `stop()`, `preset(uint8_t)`
- Helper: `Wn17cm3Decoder* get_wn17cm3_decoder()`
- Method: `bool supports_uart_control()`

### 4. `components/standing_desk_height/standing_desk_height.cpp`

Add:
- Decoder instantiation case for `DECODER_VARIANT_WN17CM3`
- Pass UART reference to decoder: `decoder->set_uart(this)`
- Implement control methods that delegate to decoder

```cpp
case DECODER_VARIANT_WN17CM3: {
  auto *dec = new Wn17cm3Decoder();
  dec->set_uart(this);
  this->decoder = dec;
  break;
}

void StandingDeskHeightSensor::move_up() {
  if (auto *dec = get_wn17cm3_decoder()) dec->send_key_pressed("UA");
}

void StandingDeskHeightSensor::move_down() {
  if (auto *dec = get_wn17cm3_decoder()) dec->send_key_pressed("DA");
}

void StandingDeskHeightSensor::stop() {
  if (auto *dec = get_wn17cm3_decoder()) {
    dec->send_key_released("UA");
    dec->send_key_released("DA");
  }
}

void StandingDeskHeightSensor::preset(uint8_t num) {
  if (auto *dec = get_wn17cm3_decoder()) {
    const char *keys[] = {" 1", " 2", " 3", " 4"};
    if (num >= 1 && num <= 4) dec->send_key_pressed(keys[num-1]);
  }
}
```

### 5. `components/standing_desk_height/sensor.py`

Add:
- Variant to `DECODER_VARIANTS` dict: `"wn17cm3": DecoderVariants.DECODER_VARIANT_WN17CM3`
- New action classes: `MoveUpAction`, `MoveDownAction`, `StopAction`, `PresetAction`
- Register actions with `@automation.register_action()`

### 6. `components/standing_desk_height/automation.h`

Add action template classes:
```cpp
template<typename... Ts> class MoveUpAction : public Action<Ts...> {
public:
  explicit MoveUpAction(StandingDeskHeightSensor *sdh) : sdh_(sdh) {}
  void play(Ts... x) override { this->sdh_->move_up(); }
protected:
  StandingDeskHeightSensor *sdh_;
};
// ... MoveDownAction, StopAction, PresetAction
```

## Implementation Phases

### Phase 1: WN17CM3 Decoder (Read-Only) ✅ COMPLETE
1. ✅ Create `wn17cm3_decoder.h` and `wn17cm3_decoder.cpp`
2. ✅ Add variant to `decoder_variant.h/.cpp`
3. ✅ Add decoder instantiation to `standing_desk_height.cpp`
4. ✅ Add variant string to `sensor.py`
5. ⏳ **Test**: Verify height reading works with 19200 baud YAML config (requires hardware)

### Phase 2: UART Control Commands ✅ COMPLETE
1. ✅ Add `set_uart()` method and UART pointer to decoder
2. ✅ Implement `send_command()`, `send_key_pressed()`, `send_key_released()`
3. ✅ Add control methods to `StandingDeskHeightSensor`
4. ✅ Add action classes to `automation.h`
5. ✅ Register actions in `sensor.py`
6. ⏳ **Test**: Verify up/down/preset commands work (requires hardware)

### Phase 3: YAML Configuration ✅ COMPLETE
1. ✅ Create `configs/desks/wn17cm3.yaml`
2. ✅ Create `configs/template_wn17cm3.yaml` with buttons and target height
3. ✅ Update documentation

## Key Technical Decisions

### Auto-Detection
WN17CM3 uses 19200 baud while other decoders use 9600. Auto-detection across different baud rates is impractical. **Require explicit `variant: wn17cm3`** in config; skip WN17CM3 in the auto-detection loop.

### Checksum Validation
Validate all incoming messages. If checksum fails, log warning and discard message.

### Height Units
WN17CM3 reports centimeters natively. Template defaults to `cm` unit.

### Button Press/Release
For continuous movement (up/down), send press on start, release on stop. For presets, send single press (controller handles the movement).

## Critical Files Reference

| File | Purpose |
|------|---------|
| `standing-desk-serial-protocol.md` | Protocol specification |
| `jarvis_decoder.cpp` | State machine pattern reference |
| `standing_desk_height.cpp` | Decoder factory, UART loop |
| `sensor.py` | Python config schema, action registration |
| `automation.h` | Action class templates |
| `configs/template.yaml` | YAML template pattern |

## External Resources

- [jdkarpin/esphome-wn17cm3](https://github.com/jdkarpin/esphome-wn17cm3) - Existing WN17CM3 implementation (simpler, no checksum validation)
- [swoga/standingdesk](https://github.com/swoga/standingdesk) - Protocol documentation source
- [tjhorner/upsy-desky Issue #8](https://github.com/tjhorner/upsy-desky/issues/8) - WN17CM2 compatibility notes
