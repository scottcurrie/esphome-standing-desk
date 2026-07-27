# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESPHome external component for reading and controlling standing desks via UART serial communication. It provides:
- A `standing_desk_height` sensor component that decodes desk height from UART
- YAML template packages for easy integration with Home Assistant
- Pre-configured packages for specific desk brands (UPLIFT v2, Fully Jarvis)

## Architecture

### ESPHome External Component (`components/standing_desk_height/`)

The component follows ESPHome's external component structure:
- `sensor.py` - ESPHome Python config schema and code generation
- `standing_desk_height.h/.cpp` - Main sensor class (`StandingDeskHeightSensor`) that extends `PollingComponent`, `UARTDevice`, and `Sensor`
- `decoder.h` - Abstract base class for protocol decoders
- `*_decoder.h/.cpp` - Desk-specific UART protocol implementations (jarvis, uplift, omnidesk)
- `decoder_variant.h/.cpp` - Enum and utilities for decoder selection
- `automation.h` - ESPHome automation action for runtime decoder detection

**Key pattern**: Each desk brand has its own decoder class implementing `Decoder::put(uint8_t)` and `Decoder::decode()`. The variant can be set explicitly or auto-detected.

### YAML Configuration (`configs/`)

- `template.yaml` - Main package with UART setup, GPIO outputs for up/down control, and target height number entity
- `configs/desks/` - Brand-specific substitution packages (pin mappings, height limits)
- `configs/addons/presets.yaml` - Optional preset button support

## Development Notes

- Target platform: ESP8266 (d1_mini board) for the original templates; the platform configs in `configs/platform/` target ESP32
- UART baud rate: 9600 for jarvis/uplift/omnidesk; 19200 for wn17cm3/wn17cm2
- Height units: inches by default, configurable to cm
- The component uses polling (default 500ms) to read UART data
