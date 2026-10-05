#pragma once

#include "ups_hid.h"
#include "constants_ups.h"
#include "hid_data_points.h"

#include <cmath>
#include <string>

namespace esphome {
namespace ups_hid {

/**
 * @brief APC HID Protocol Implementation
 *
 * APC UPSes (USB VID 0x051D: Back-UPS, Back-UPS Pro, Smart-UPS, ...) are HID Power
 * Devices, but report IDs and layouts change between models. Instead of hard-coded
 * report IDs, this protocol reads the UPS's HID report descriptor and finds each
 * value by its usage path, using the paths of the NUT apc-hid.c subdriver.
 * Manufacturer, model and serial number come from the USB device strings, and the
 * firmware versions from the product string, as in NUT.
 */
class ApcHidProtocol : public UpsProtocolBase {
 public:
  explicit ApcHidProtocol(UpsHidComponent *parent);

  bool detect() override;
  bool initialize() override;
  bool read_data(UpsData &data) override;
  DeviceInfo::DetectedProtocol get_protocol_type() const override { return DeviceInfo::PROTOCOL_APC_HID; }
  std::string get_protocol_name() const override { return "APC HID Protocol"; }

  // Beeper control (UPS.PowerSummary.AudibleAlarmControl)
  bool beeper_enable() override;
  bool beeper_disable() override;
  bool beeper_mute() override;

  // Battery test (UPS.Battery.Test) and front panel test (UPS.APCPanelTest)
  bool start_battery_test_quick() override;
  bool start_battery_test_deep() override;
  bool stop_battery_test() override;
  bool start_ups_test() override;
  bool stop_ups_test() override;

  // Shutdown/startup/reboot countdowns
  bool read_timer_data(UpsData &data) override;

  // UPS data points located through the report descriptor
  enum Item : uint8_t {
    // Identification and ratings
    ITEM_BATTERY_TYPE,
    ITEM_BATTERY_MFR_DATE,
    ITEM_BATTERY_REPLACE_DATE,
    ITEM_UPS_MFR_DATE,
    ITEM_BATTERY_VOLTAGE_NOMINAL,
    ITEM_INPUT_VOLTAGE_NOMINAL,
    ITEM_OUTPUT_VOLTAGE_NOMINAL,
    ITEM_ACTIVE_POWER_NOMINAL,
    // Battery
    ITEM_BATTERY_CHARGE,
    ITEM_BATTERY_CHARGE_LOW,
    ITEM_BATTERY_CHARGE_WARNING,
    ITEM_BATTERY_RUNTIME,
    ITEM_BATTERY_RUNTIME_LOW,
    ITEM_BATTERY_VOLTAGE,
    // Input / output
    ITEM_INPUT_VOLTAGE,
    ITEM_TRANSFER_LOW,
    ITEM_TRANSFER_HIGH,
    ITEM_SENSITIVITY,
    ITEM_OUTPUT_VOLTAGE,
    ITEM_OUTPUT_FREQUENCY,
    ITEM_LOAD,
    ITEM_BEEPER,
    // Status flags
    ITEM_AC_PRESENT,
    ITEM_CHARGING,
    ITEM_DISCHARGING,
    ITEM_BELOW_CAPACITY_LIMIT,
    ITEM_SHUTDOWN_IMMINENT,
    ITEM_TIME_LIMIT_EXPIRED,
    ITEM_OVERLOAD,
    ITEM_NEED_REPLACEMENT,
    ITEM_BATTERY_PRESENT,
    ITEM_STATUS_FLAG,
    // Tests and timers
    ITEM_BATTERY_TEST,
    ITEM_PANEL_TEST,
    ITEM_TIMER_SHUTDOWN,
    ITEM_TIMER_START,
    ITEM_TIMER_REBOOT,
    ITEM_COUNT
  };

 private:
  HidDataPoints points_;

  // Read once in initialize()
  std::string manufacturer_;
  std::string model_;
  std::string serial_number_;
  std::string firmware_version_;
  std::string firmware_aux_;
  std::string battery_type_;
  std::string battery_mfr_date_;
  std::string ups_mfr_date_;
  float battery_voltage_nominal_{NAN};
  float input_voltage_nominal_{NAN};
  float output_voltage_nominal_{NAN};
  float active_power_nominal_{NAN};

  bool load_report_descriptor();
  void fix_report_descriptor();
  bool read_device_string(UsbDeviceString which, std::string &value);

  bool has(Item item) const { return points_.has(item); }
  bool read_raw(Item item, int64_t &value) { return points_.read_raw(item, value); }
  bool read_value(Item item, float &value) { return points_.read_value(item, value); }
  bool read_flag(Item item, bool &value) { return points_.read_flag(item, value); }
  bool write_value(Item item, float value, const char *action) { return points_.write_value(item, value, action); }

  void read_device_info();
  bool read_battery(UpsData &data);
  bool read_power(UpsData &data);
  bool read_status(UpsData &data);
  void read_settings(UpsData &data);
  bool read_timers(UpsData &data);
};

}  // namespace ups_hid
}  // namespace esphome
