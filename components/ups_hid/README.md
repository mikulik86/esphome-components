# UPS HID Component for ESPHome

A ESPHome component for monitoring UPS devices via USB connection on ESP32-S3 and ESP32-P4. Direct USB HID communication with support for APC, CyberPower, Eaton, and generic HID UPS devices.

## Features

- 🔋 **Real-time UPS monitoring**: Battery level, voltages, load, runtime, status
- 🧪 **UPS self-test control**: Battery tests (quick/deep), panel tests, with real-time result monitoring
- 🔊 **Beeper control**: Enable/disable/mute UPS audible alarms via HID write operations (beeper test on some models)
- ⏱️ **Delay configuration**: Configure UPS shutdown, start, and reboot delays via USB HID (CyberPower and generic HID; APC and Eaton UPSes store no delays)
- 🌈 **Visual status indicator**: RGB LED with customizable status colors
- 🏠 **Home Assistant integration**: Automatic entity discovery via ESPHome API
- 🔌 **Multi-protocol support**: APC HID, CyberPower HID, Eaton HID, Generic HID
- 🎯 **Auto-detection**: Intelligent protocol detection based on USB vendor IDs
- 🔧 **Robust USB handling**: the UPS is detected again after a USB replug, and unanswered requests time out safely
- 🧪 **Simulation mode**: Test integration without physical UPS device

## Quick Start

### Hardware Requirements

- **ESP32-S3** (e.g. ESP32-S3-DevKitC-1 v1.1) or **ESP32-P4** board with USB OTG support. The P4 is tested with ESPHome 2026.9.1.
- **UPS device** with USB monitoring port
- **USB cable** (UPS to the ESP32's USB OTG port)

### Minimal Configuration

```yaml
# Quick start example - see configs/README.md for complete modular configurations
external_components:
  - source: github://mikulik86/esphome-components
    components: [ ups_hid ]  # Add more components as needed

# Use modular configuration packages for maintainable setup
packages:
  base_ups: !include configs/base_ups.yaml           # Hardware, network, LED
  essential: !include configs/essential_sensors.yaml  # Core 7 sensors
  
# Optional: Add more features
# controls: !include configs/ups_controls.yaml        # Beeper/test buttons  
# extended: !include configs/extended_sensors.yaml    # 17+ additional sensors
# device: !include configs/device_types/apc_backups_es.yaml  # Device-specific optimizations
```

> **📦 For complete configuration options, examples, and device-specific setups, see [`configs/README.md`](../../configs/README.md)**

## Hardware Setup

### USB OTG Connection

```
UPS USB Port (Type-B)  ←→  USB Cable  ←→  ESP32-S3 / ESP32-P4 USB OTG Port
```

### LED Status Indicators (Optional RGB LED on GPIO48)

- 🟢 **Green Breathing**: UPS online, normal operation
- 🟠 **Orange Fade**: UPS running on battery power  
- 🟡 **Yellow Double-Blink**: Battery charging
- 🔴 **Red Strobe**: Critical conditions - low battery, fault, overload
- 🔵 **Blue Fade**: System offline/unknown

## Supported UPS Devices

### Tested Compatible Models

| Vendor | Models | Protocol | Vendor ID | Beeper Control |
|--------|--------|----------|-----------|----------------|
| **APC** | Back-UPS BX2200MI (tested), other Back-UPS, Back-UPS Pro, Smart-UPS (USB HID) | APC HID | 0x051D | 🔧 Model dependent (works on BX2200MI) |
| **CyberPower** | CP1500EPFCLCD, CP1000PFCLCD | CyberPower HID | 0x0764 | ✅ Confirmed |
| **Eaton/MGE** | 5E 1500i (tested), other 5E, Ellipse, 3S, 5S, 5SC, 5P/5PX, 9E/9SX/9PX | Eaton HID | 0x0463 | 🔧 Model dependent (works on 5E 1500i) |
| **Tripp Lite** | SMART1500LCDT, UPS series | Generic HID | 0x09AE | ⚠️ Limited |
| **Belkin** | Older USB UPS models | Generic HID | 0x050D | ⚠️ Limited |

**Beeper Control Legend:**
- ✅ **Confirmed**: Full beeper control tested and working (enable/disable/mute/test)
- 🔧 **Model dependent**: enable/disable/mute when the UPS exposes the control (no beeper test)
- ⚠️ **Limited**: Basic support via generic HID (device-dependent functionality)

> The Eaton HID protocol is new. It was tested on an Eaton 5E 1500i (auto-detection, every
> value the 5E reports, the switch to battery power, and the beeper enable/disable/mute
> buttons) and checked against a real Eaton 9PX report descriptor with NUT's parser. Other
> Eaton models are untested so far. See [Eaton UPS Notes](#eaton-ups-notes).
>
> The APC HID protocol now works the same way, with NUT's `apc-hid` paths. It was tested on
> an APC Back-UPS BX2200MI (every value the BX reports, the switch to battery power and the
> beeper enable/disable buttons) and against a simulated Back-UPS ES. Other APC models are
> untested so far. See [APC UPS Notes](#apc-ups-notes).

### Protocol Compatibility Matrix

| Protocol | Communication | Auto-Detection | Read Features | Write Features |
|----------|---------------|----------------|---------------|----------------|
| **APC HID** | HID-PDC, paths from report descriptor | ✅ | Battery, voltages, status, ratings, dates, timers | ✅ Beeper, battery and panel test |
| **CyberPower HID** | Vendor-specific HID | ✅ | Extended sensors, config | ✅ Beeper control |
| **Eaton HID** | HID-PDC, paths from report descriptor | ✅ | Battery, voltages, status, ratings, timers | ✅ Beeper, battery test |
| **Generic HID** | Standard HID-PDC | ✅ | Basic monitoring | ⚠️ Limited writes |

## Configuration Reference

### Sensor Overview

| Sensor Package | Count | Description |
|----------------|-------|-------------|
| **Essential Monitoring** | 7 sensors | Basic UPS monitoring (battery, voltage, load, status) |
| **Extended Features** | 17+ sensors | Advanced metrics (timers, thresholds, device info) |
| **Status Indicators** | 6 binary sensors | Online, battery, fault, charging states |
| **Device Controls** | 10 buttons | Beeper control, battery/panel testing |
| **Configuration** | 3 number entities | UPS delay settings (shutdown/start/reboot) |

### Component Configuration

```yaml
ups_hid:
  id: ups_monitor                # Required component ID
  update_interval: 30s           # Polling interval (5s-60s)
  protocol: auto                 # Protocol: auto, apc, cyberpower, eaton, generic
  simulation_mode: false         # Testing without UPS hardware
```

### Platform Types

**Sensor Platform**: `battery_level`, `input_voltage`, `output_voltage`, `load_percent`, `runtime`, `frequency` + extended sensors

**Binary Sensor Platform**: `online`, `on_battery`, `low_battery`, `charging`, `fault`, `overload`, `replace_battery`

`overload` follows the UPS's overload flag with the APC and Eaton protocols; with the other protocols it turns on above 95% load. `replace_battery` turns on when the UPS reports the battery needs replacing (APC, Eaton and generic HID; off on UPSes that don't report it).

A sensor shows *unknown* when the UPS stops reporting its value, for example `input_voltage` on battery with the CyberPower and generic protocols. The APC and Eaton protocols take `online` and `on_battery` from the UPS's status flags and report the measured input voltage. When the UPS is unplugged, sensors and binary sensors become *unknown*, `status` reads `Disconnected`, the other changing text sensors are cleared, and the model, manufacturer and firmware of the last UPS stay. They all update again once the UPS is detected.

**Text Sensor Platform**: `manufacturer`, `model`, `status`, `protocol`, `serial_number`, `firmware_version`

**Button Platform**: Beeper control (`enable`, `disable`, `mute`, `test`) + UPS testing (`battery_quick`, `battery_deep`, `ups_test`)

**Switch Platform**: `beeper` keeps the UPS beeper enabled or disabled. The ESP saves the
setting, and when the UPS reports a different one after a poll, the component sets it back.
The Eaton 5E, for example, enables its beeper again after being switched off and on. A muted
beeper (an alarm silenced for now) is left alone. The beeper `enable` and `disable` buttons
and the NUT `beeper.enable` / `beeper.disable` commands move the switch too. Works with the
APC and Eaton protocols.

```yaml
switch:
  - platform: ups_hid
    ups_hid_id: ups_monitor
    type: beeper
    name: "UPS Beeper"
    # restore_mode: RESTORE_DEFAULT_ON (default) starts on until the switch is changed;
    # DISABLED takes over the UPS's own setting at each start instead of saving one
```

**Number Platform**: Delay configuration (`shutdown`, `start`, `reboot`)

> **📦 Complete configuration examples with all platforms are available in [`configs/README.md`](../../configs/README.md)**

## Advanced Configuration

### Protocol Selection

You can now manually select which UPS protocol to use instead of relying on auto-detection:

```yaml
ups_hid:
  protocol: auto                 # Default: automatic selection based on USB vendor ID
  # protocol: apc                # Force APC HID protocol
  # protocol: cyberpower         # Force CyberPower HID protocol  
  # protocol: eaton              # Force Eaton HID protocol
  # protocol: generic            # Force Generic HID protocol
```

**Protocol Options:**

- **`auto`** (default): Automatically select protocol based on USB vendor ID
  - APC devices (0x051D): Uses APC HID Protocol
  - CyberPower (0x0764): Uses CyberPower HID Protocol
  - Eaton (0x0463): Uses Eaton HID Protocol
  - Unknown devices: Falls back to Generic HID Protocol

- **`apc`**: Force APC HID Protocol
  - Use for APC USB HID devices: Back-UPS, Back-UPS Pro, Smart-UPS
  - Finds values through the UPS's HID report descriptor, so it adapts to each model
  - Battery and panel test when the model has them

- **`cyberpower`**: Force CyberPower HID Protocol
  - Use for CyberPower CP series devices
  - Enhanced sensor support with 12+ additional sensors
  - Runtime scaling and advanced thresholds

- **`eaton`**: Force Eaton HID Protocol
  - Use for Eaton and MGE devices: Ellipse, 3S, 5E, 5S, 5SC, 5P/5PX, 9E/9SX/9PX
  - Finds values through the UPS's HID report descriptor, so it adapts to each model
  - Also worth trying for Eaton OEM units (Dell, HP, IBM) that report a different vendor ID

- **`generic`**: Force Generic HID Protocol
  - Universal fallback for unknown UPS brands
  - Basic 5-sensor support with intelligent detection
  - Limited beeper/testing functionality

**When to use manual selection:**
- Testing different protocols on the same device
- Troubleshooting protocol detection issues
- Using non-standard USB vendor/product IDs
- Forcing generic protocol for maximum compatibility

### APC UPS Notes

APC models place the same data in different HID reports. The APC protocol reads the UPS's
HID report descriptor at startup and looks each value up by its usage path, using the paths
of NUT's `apc-hid` driver, as the Eaton protocol does for Eaton.

- **Manufacturer, model and serial number** come from the USB device strings. The model and
  firmware are split out of the product string the way NUT does it:
  `Back-UPS ES 700G FW:871.O2 .I USB FW:O2` gives model `Back-UPS ES 700G`, firmware
  `871.O2 .I` and auxiliary firmware `O2`.
- **Missing sensors are normal.** Back-UPS models do not report output voltage or frequency.
- **Online and on battery come from the UPS's status flags.** `input_voltage` shows what the
  UPS measures.
- **Beeper, battery test and panel test** buttons work when the model exposes those controls;
  otherwise the log says the action is not supported. There is no beeper test.
- **Delay settings (`number` entities) are not supported.** As with Eaton, writing a
  `DelayBeforeShutdown` value starts a real countdown that switches the load off. The
  `ups_timer_*` sensors show running countdowns.
- **Back-UPS BX2200MI:** reports battery level, runtime, battery voltage and its nominal
  value, input voltage, transfer thresholds, sensitivity, load, its real power rating
  (`ups_realpower_nominal`), battery and UPS manufacture dates, beeper status, battery test
  result, status flags and countdowns. It has no output voltage or frequency and no panel test.
  Its input voltage range needs NUT's report descriptor fix, which is applied.

To check what your UPS provides, set `ups_hid.apc_hid: DEBUG`. At detection, the log lists
which HID path feeds each value; set the logger level to `VERBOSE` for every path. Please
include that log when reporting a problem with an APC model.

### Eaton UPS Notes

Eaton models place the same data in different HID reports. The Eaton protocol reads the
UPS's HID report descriptor at startup and looks each value up by its usage path, using
the paths and model quirks of NUT's `mge-hid` driver. A model works without code changes
as long as it uses these paths.

- **Missing sensors are normal.** Offline models (Ellipse, 3S, Protection Station) do not
  measure input or output voltage, so those sensors stay unknown.
- **Online and on battery come from the UPS's status flags**, not from the input voltage.
  `input_voltage` always shows what the UPS measures: a few volts in an outage (about 3 V on
  the 5E), or the real mains voltage when a brownout puts the UPS on battery.
- **Beeper and battery test** buttons work when the model exposes those controls; otherwise
  the log says the action is not supported. Beeper test and UPS (panel) test are not available.
- **Delay settings (`number` entities) are not supported.** Eaton UPSes have no stored
  shutdown/start delay over USB: writing `UPS.PowerSummary.DelayBeforeShutdown` starts a real
  countdown that switches the load off. The `ups_timer_*` sensors show running countdowns.
- **Firmware 2.02 units** (e.g. 9PX, 9SX) have a reduced and a full report descriptor; the
  full one is used, as in NUT.
- **5E series:** reports battery level, runtime, load, input and output voltage, output
  frequency, beeper status, online/charging state and the overload and replace-battery flags.
  It has no serial number, battery voltage, input sensitivity or battery test over USB. It
  charges constantly, so `charging` turns off at 100%. It enables its beeper again after
  being switched off and on; the `beeper` switch keeps it disabled.

To check what your UPS provides, set `ups_hid.eaton: DEBUG`. At detection (at startup and when
the UPS is plugged back in), the log lists which HID path feeds each value, and marks values
that the model has but the protocol ignores. For a full listing of every path (same format as NUT
`usbhid-ups -DD`), set the logger level to `VERBOSE`. Please include that log when reporting
a problem with an Eaton model.

### Performance Tuning

```yaml
ups_hid:
  id: ups_monitor
  update_interval: 10s           # Faster polling (minimum 5s recommended)
  protocol_timeout: 5s           # Faster timeout for responsive networks
  
# Individual sensor update intervals are controlled by the component
```

### Simulation Mode

For testing without physical UPS:

```yaml
ups_hid:
  id: ups_monitor
  simulation_mode: true          # Enables realistic simulated data
  update_interval: 5s            # Faster updates to see simulation changes
  
logger:
  level: DEBUG                   # See simulation data changes
```

The simulated UPS is an APC Back-UPS ES with its own HID report descriptor, read by the APC
protocol like a real one. Mains power drops for the last 30 seconds of every 5 minutes, and
the beeper, battery test and panel test buttons change what it reports.

## Troubleshooting

### Common Issues

#### 1. No UPS devices found
- Verify USB cable connection (UPS ↔ ESP32-S3)
- Ensure UPS is powered on and USB monitoring enabled
- Try different USB cable

#### 2. Protocol detection failed
- Check if your UPS model is supported (see device compatibility table)
- Enable debug logging: `logger: level: DEBUG`
- Try manual protocol: `protocol: generic`

#### 3. USB communication errors
- Increase timeout: `protocol_timeout: 15s`
- Restart ESP32-S3 device
- Verify stable power supply

#### 4. Beeper control not working
- Verify UPS model supports write operations (check compatibility table)
- Test during actual power outage for mute functionality
- Some UPS models have hardware beeper disable switches

### Debug Logging

```yaml
logger:
  level: DEBUG
  logs:
    ups_hid: DEBUG
    ups_hid.apc_hid: DEBUG
    ups_hid.cyberpower_hid: DEBUG
    ups_hid.eaton: DEBUG
```

**Normal Operation**: Protocol detection < 500ms, consistent update intervals
**USB Disconnection**: System automatically recovers, sensors show "unavailable" until reconnected  
**Rate Limiting**: Normal protection behavior, waits 5s between retry attempts


## FAQ

**Q: Can I monitor multiple UPS devices?**  
A: Each ESP32-S3 supports one UPS via USB OTG. Use multiple ESP32-S3 devices for multiple UPS units.

**Q: Does this work with network-attached UPS devices?**  
A: No, this component requires direct USB connection. For network UPS monitoring, use different ESPHome components.

**Q: Can I use other ESP32 variants?**  
A: Only ESP32-S3 supports USB OTG required for direct UPS communication.

**Q: Why doesn't the beeper test make sound?**  
A: UPS beepers only sound during actual alarms. Test button verifies write operations work correctly.

**Q: Do beeper settings persist after restart?**  
A: Yes, beeper settings are stored in UPS NVRAM and persist across reboots.

## Development

Use `simulation_mode: true` for testing without UPS hardware. For custom protocols, inherit from `UpsProtocolBase` and implement required methods.
