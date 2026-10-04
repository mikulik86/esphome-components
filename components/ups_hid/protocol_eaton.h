#pragma once

#include "ups_hid.h"
#include "constants_ups.h"
#include "hid_report_descriptor.h"

#include <cmath>
#include <string>
#include <vector>

namespace esphome {
namespace ups_hid {

/**
 * @brief Eaton / MGE HID Protocol Implementation
 *
 * Eaton UPSes (USB VID 0x0463: Ellipse, Protection Station, 3S, 5E, 5S, 5SC,
 * 5P/5PX, 9E/9SX/9PX, ...) are HID Power Devices, but report IDs and layouts
 * change between models and firmware versions. Instead of hard-coded report IDs,
 * this protocol reads the UPS's HID report descriptor and finds each value by
 * its usage path (e.g. UPS.PowerSummary.RemainingCapacity), using the paths and
 * model quirks of the NUT mge-hid.c subdriver.
 */
class EatonHidProtocol : public UpsProtocolBase {
 public:
  explicit EatonHidProtocol(UpsHidComponent *parent);

  bool detect() override;
  bool initialize() override;
  bool read_data(UpsData &data) override;
  DeviceInfo::DetectedProtocol get_protocol_type() const override { return DeviceInfo::PROTOCOL_EATON_HID; }
  std::string get_protocol_name() const override { return protocol::EATON_HID; }

  // Beeper control (UPS.*.AudibleAlarmControl)
  bool beeper_enable() override;
  bool beeper_disable() override;
  bool beeper_mute() override;

  // Battery test (UPS.BatterySystem.Battery.Test)
  bool start_battery_test_quick() override;
  bool start_battery_test_deep() override;
  bool stop_battery_test() override;

  // Shutdown/startup/reboot countdowns (UPS.PowerSummary.DelayBefore*)
  bool read_timer_data(UpsData &data) override;

  // UPS data points located through the report descriptor
  enum Item : uint8_t {
    // Identification (USB string descriptor indices)
    ITEM_MANUFACTURER,
    ITEM_PRODUCT,
    ITEM_MODEL,
    ITEM_SERIAL_NUMBER,
    ITEM_FIRMWARE,
    ITEM_BATTERY_TYPE,
    // Ratings
    ITEM_INPUT_VOLTAGE_NOMINAL,
    ITEM_OUTPUT_VOLTAGE_NOMINAL,
    ITEM_APPARENT_POWER_NOMINAL,
    ITEM_ACTIVE_POWER_NOMINAL,
    ITEM_BATTERY_VOLTAGE_NOMINAL,
    ITEM_SUMMARY_VOLTAGE_NOMINAL,
    // Battery
    ITEM_BATTERY_CHARGE,
    ITEM_BATTERY_RUNTIME,
    ITEM_BATTERY_CHARGE_LOW,
    ITEM_BATTERY_RUNTIME_LOW,
    ITEM_BATTERY_VOLTAGE,
    ITEM_SUMMARY_VOLTAGE,
    // Input / output
    ITEM_INPUT_VOLTAGE,
    ITEM_INPUT_FREQUENCY,
    ITEM_OUTPUT_VOLTAGE,
    ITEM_OUTPUT_FREQUENCY,
    ITEM_LOAD,
    ITEM_TRANSFER_LOW,
    ITEM_TRANSFER_HIGH,
    ITEM_SENSITIVITY,
    ITEM_BEEPER,
    // Status flags
    ITEM_AC_PRESENT,
    ITEM_CHARGING,
    ITEM_DISCHARGING,
    ITEM_BELOW_CAPACITY_LIMIT,
    ITEM_SHUTDOWN_IMMINENT,
    ITEM_OVERLOAD,
    ITEM_NEED_REPLACEMENT,
    ITEM_GOOD,
    ITEM_INTERNAL_FAILURE,
    ITEM_OVER_TEMPERATURE,
    ITEM_FAN_FAILURE,
    ITEM_BATTERY_PRESENT,
    ITEM_MAIN_INPUT_USED,
    ITEM_BYPASS_USED,
    ITEM_BATTERY_USED,
    ITEM_MANUAL_BYPASS_USED,
    ITEM_BOOST,
    ITEM_BUCK,
    // Advanced Battery Monitoring charger
    ITEM_ABM_ENABLED,
    ITEM_CHARGER_MODE,
    ITEM_CHARGER_STATUS,
    // Test and timers
    ITEM_BATTERY_TEST,
    ITEM_TIMER_SHUTDOWN,
    ITEM_TIMER_START,
    ITEM_TIMER_REBOOT,
    ITEM_COUNT
  };

 private:
  static constexpr uint8_t NO_FIELD = 0xFF;

  struct CachedReport {
    uint8_t report_id;
    uint8_t report_type;
    bool valid;
    std::vector<uint8_t> data;
  };

  HidReportDescriptor descriptor_;
  HidField fields_[ITEM_COUNT];
  uint8_t field_rank_[ITEM_COUNT];        // index of the matched path definition, NO_FIELD if absent
  std::vector<CachedReport> report_cache_;  // reports read during the current poll
  bool descriptor_loaded_{false};

  // Read once in initialize()
  std::string manufacturer_;
  std::string model_;
  std::string serial_number_;
  std::string firmware_version_;
  std::string battery_type_;
  float input_voltage_nominal_{NAN};
  float output_voltage_nominal_{NAN};
  float apparent_power_nominal_{NAN};
  float active_power_nominal_{NAN};
  float battery_voltage_nominal_{NAN};
  bool summary_voltage_is_battery_{false};

  bool load_report_descriptor();
  void map_field(const HidField &field);
  bool has(Item item) const { return field_rank_[item] != NO_FIELD; }

  bool read_raw(Item item, int64_t &value);
  bool read_value(Item item, float &value);
  bool read_flag(Item item, bool &value);
  bool read_string(Item item, std::string &value);
  bool write_value(Item item, float value, const char *action);

  void read_device_info();
  bool read_battery(UpsData &data);
  bool read_power(UpsData &data);
  bool read_status(UpsData &data);
  void read_settings(UpsData &data);
  bool read_timers(UpsData &data);
};

}  // namespace ups_hid
}  // namespace esphome
