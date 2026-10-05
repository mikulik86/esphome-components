# ESPHome Components Collection

A collection of ESPHome components for various hardware integrations and monitoring solutions.

## About This Fork

This is a fork of [bullshit/esphome-components](https://github.com/bullshit/esphome-components).
It changes the `ups_hid` component:

- **Eaton HID protocol** for Eaton and MGE UPSes, tested on an Eaton 5E 1500i
- **APC protocol rebuilt** on the UPS's HID report descriptor with NUT's `apc-hid` paths, instead
  of report numbers recorded from one unit. Tested on an APC Back-UPS BX2200MI.
- **ESP32-P4** support, tested with ESPHome 2026.9.1
- **Replugging**: the UPS is detected again after its USB cable is unplugged or swapped, without a reboot
- **No stale values**: entities show *unknown* when the UPS stops reporting a value or is unplugged
- **Status from the UPS itself**: online, on battery and overload come from the UPS's status flags
  (APC, Eaton), and a new `replace_battery` binary sensor reports a worn battery
- **Safe USB timeouts**: a request the UPS does not answer no longer leaves the USB stack
  pointing at freed memory
- **ESPHome 2026.9** compatibility, and builds without the button, number or text sensor platforms

To use this fork in your ESPHome configuration:

```yaml
external_components:
  - source: github://mikulik86/esphome-components
    components: [ups_hid]
```

## Available Components

### 🔋 UPS HID Component (`ups_hid`)

Monitor UPS devices via direct USB connection on ESP32-S3 and ESP32-P4. Supports APC, CyberPower, Eaton, and generic HID UPS devices with real-time monitoring of battery status, power conditions, and device information.

**Key Features:**
- **Real-time UPS monitoring**: Battery, voltage, load, runtime, and 15+ sensors
- **Multi-protocol support**: APC HID, CyberPower HID, Eaton HID, Generic HID with auto-detection
- **UPS Control**: Beeper control (enable/disable/mute) and battery testing, where the UPS supports them
- ⏱️ **Delay configuration**: Configure UPS shutdown, start, and reboot delays via USB HID (CyberPower and generic HID)
- **Home Assistant integration**: Full device discovery and management
- **Developer-friendly**: Simulation mode, comprehensive logging

[📖 Full Documentation](components/ups_hid/README.md)

### 💡 UPS Status LED Component (`ups_status_led`)

Smart LED status indicator for UPS monitoring with automatic pattern management and night mode. Provides visual status indication using solid colors with thread-safe runtime configuration.

**Key Features:**
- **7 solid color patterns**: Critical (red), Battery (orange), Charging (yellow), Normal (green), Offline (blue), No Data (purple), Error (white)
- **Night mode**: Time-based brightness dimming with color compensation for WS2812 LEDs
- **Home Assistant controls**: Enable/disable, brightness, night mode settings via web UI
- **Thread-safe operation**: Safe concurrent access from web UI and main loop
- **Minimum brightness logic**: 20% minimum ensures meaningful enable/disable distinction

[📖 Full Documentation](components/ups_status_led/README.md)

### 🌐 NUT Server Component (`nut_server`)

Network UPS Tools (NUT) protocol TCP server for exposing UPS data to standard monitoring tools. Provides NUT v1.3 compliant server for integration with existing infrastructure.

**Key Features:**
- **Standard NUT Protocol**: v1.3 compliant TCP server on port 3493
- **Multi-client support**: Up to 4 simultaneous monitoring connections
- **Full protocol implementation**: LIST UPS/VAR/CMD/CLIENT, INSTCMD, NETVER
- **Dynamic UPS detection**: Automatically exposes connected UPS manufacturer/model
- **Authentication support**: Optional username/password protection
- **Data Provider Pattern**: Direct access to UPS data without sensor overhead

[📖 Full Documentation](components/nut_server/README.md)

## 📦 Configuration Management

Modular, maintainable ESPHome configuration system using packages. Build configurations by combining reusable components instead of maintaining large monolithic files.

**Key Features:**
- **Modular packages**: Core, sensors, controls, device-specific optimizations
- **Grouped organization**: Optional entity grouping for cleaner web interface
- **Example configurations**: Production-ready configs for APC, CyberPower, Eaton, and rack UPS
- **Regional defaults**: Voltage/frequency presets for different regions
- **Smart LED integration**: Automatic visual status indication

[📖 Configuration Guide](configs/README.md)

## Development

### Project Structure

```
components/
├── ups_hid/                 # UPS HID monitoring component
│   ├── README.md           # Component-specific documentation
│   ├── __init__.py         # Component configuration
│   ├── ups_hid.h           # Main component header
│   ├── ups_hid.cpp         # Main component implementation
│   ├── sensor.py           # Sensor platform
│   ├── binary_sensor.py    # Binary sensor platform
│   ├── text_sensor.py      # Text sensor platform
│   └── ...                 # Protocol implementations
├── ups_status_led/         # Smart LED status indicator component
│   ├── README.md           # Component documentation
│   ├── __init__.py         # Component configuration
│   ├── ups_status_led.h    # Component header
│   └── ups_status_led.cpp  # Component implementation
├── nut_server/             # Network UPS Tools (NUT) TCP server component
│   ├── README.md           # Component documentation
│   ├── __init__.py         # Component configuration
│   ├── nut_server.h        # Component header
│   └── nut_server.cpp      # Component implementation
└── ...

configs/
├── README.md               # Configuration management documentation
├── base_ups.yaml           # Core UPS configuration
├── essential_sensors.yaml  # Essential monitoring sensors
├── extended_sensors.yaml   # Extended monitoring sensors
├── ups_controls.yaml       # UPS control buttons
├── nut_server.yaml         # NUT server configuration
├── examples/               # Example device configurations
│   ├── apc-ups-monitor.yaml
│   ├── cyberpower-ups-monitor.yaml
│   ├── eaton-ups-monitor.yaml
│   └── rack-ups-monitor.yaml
└── ...                     # Additional modular packages

tools/
├── scan-usb.sh             # USB device scanning utility
└── README.md               # Tools documentation

.vscode/
├── tasks.json              # VSCode development tasks
```

### Development Tools

- **Scan USB Devices**: `bash tools/scan-usb.sh`
  - Lists connected ESP32, UPS, and serial devices
- **VSCode Tasks**: Integrated development tasks via Command Palette

### Adding New Components

1. Create component directory: `components/your_component/`
2. Implement core files:
   - `__init__.py` - Component configuration and validation
   - `your_component.h` - Component header
   - `your_component.cpp` - Component implementation
   - Platform files (`sensor.py`, etc.) as needed
3. Add component documentation: `components/your_component/README.md`
4. Update this main README with component description
5. Test with both real hardware and simulation where applicable

### Coding Guidelines

- Follow ESPHome coding standards and patterns
- Don't use exceptions (ESPHome disables them)
- Include comprehensive logging with appropriate levels
- Implement thread-safe code where necessary
- Provide simulation modes for testing
- Include detailed documentation and examples

## Hardware Requirements

Component-specific requirements are documented in each component's README:

- **UPS HID**: ESP32-S3 (e.g. ESP32-S3-DevKitC-1 v1.1) or ESP32-P4 with USB OTG support. Tested on an ESP32-P4 with ESPHome 2026.9.1 with an Eaton 5E 1500i and an APC Back-UPS BX2200MI.
- **UPS Status LED**: WS2812 LED strip (1 LED), requires time component for night mode
- **Future components**: Requirements will be listed here

## License

This project follows the ESPHome dual-license model:

- **C++ Runtime Code** (`.c`, `.cpp`, `.h`, `.hpp`, `.tcc`, `.ino`): Licensed under [GNU GPLv3](LICENSE)
- **Python Code** (`.py`) and other files: Licensed under [MIT License](LICENSE)

See the [LICENSE](LICENSE) file for complete license text.

### Third-Party Components

This project is built as ESPHome external components and follows ESPHome's licensing structure. The components are designed to work within the ESPHome ecosystem.
