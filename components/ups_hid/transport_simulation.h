#pragma once

#include "transport_interface.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace ups_hid {

/**
 * Simulated USB Transport Implementation
 *
 * Emulates an APC Back-UPS ES as the APC HID protocol sees it: USB device strings,
 * a HID report descriptor and the feature reports it declares. A table of fields
 * produces both the descriptor and the reports, so they always agree. Mains power
 * drops for 30 seconds every 5 minutes.
 */
class SimulatedTransport : public IUsbTransport {
public:
    SimulatedTransport();
    ~SimulatedTransport() override = default;

    // IUsbTransport implementation
    esp_err_t initialize() override;
    esp_err_t deinitialize() override;

    bool is_connected() const override;
    uint16_t get_vendor_id() const override;
    uint16_t get_product_id() const override;

    esp_err_t hid_get_report(uint8_t report_type, uint8_t report_id,
                           uint8_t* data, size_t* data_len,
                           uint32_t timeout_ms = 1000) override;

    esp_err_t hid_set_report(uint8_t report_type, uint8_t report_id,
                           const uint8_t* data, size_t data_len,
                           uint32_t timeout_ms = 1000) override;

    esp_err_t get_string_descriptor(uint8_t string_index,
                                  std::string& result) override;

    esp_err_t get_hid_report_descriptor(uint8_t descriptor_index,
                                      std::vector<uint8_t>& descriptor) override;
    uint8_t get_device_string_index(UsbDeviceString which) const override;

    std::string get_last_error() const override;

    // Values the simulated UPS reports
    enum class Value : uint8_t {
        CHARGE, RUNTIME, CHARGE_LOW, RUNTIME_LOW, BATTERY_VOLTAGE, BATTERY_VOLTAGE_NOMINAL, CHEMISTRY,
        BATTERY_MFR_DATE, INPUT_VOLTAGE, INPUT_VOLTAGE_NOMINAL, TRANSFER_LOW, TRANSFER_HIGH, SENSITIVITY,
        LOAD, REALPOWER_NOMINAL, BEEPER, CHARGING, DISCHARGING, AC_PRESENT, BATTERY_PRESENT,
        BELOW_CAPACITY_LIMIT, SHUTDOWN_IMMINENT, TIME_LIMIT_EXPIRED, NEED_REPLACEMENT, OVERLOAD,
        BATTERY_TEST, PANEL_TEST, TIMER_REBOOT, TIMER_SHUTDOWN,
    };

private:
    bool connected_{false};
    bool initialized_{false};
    uint16_t vendor_id_{0x051D};  // APC
    uint16_t product_id_{0x0002}; // Back-UPS family
    std::string last_error_;

    // Layout of each simulated field: report ID and bit offset after the report ID byte
    struct FieldLayout {
        uint8_t report_id;
        uint16_t bit_offset;
    };
    std::vector<uint8_t> report_descriptor_;
    std::vector<FieldLayout> layout_;

    // Simulation state
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_update_;
    float battery_level_{85.0f};
    float input_voltage_{120.0f};
    float load_percent_{25.0f};
    bool on_battery_{false};
    int64_t beeper_{2};        // AudibleAlarmControl: 1 disabled, 2 enabled, 3 muted
    int64_t test_result_{6};   // Test: 6 no test initiated
    float test_started_{-1.0f};
    int64_t panel_test_{0};
    float timer_reboot_{-1.0f};
    float timer_shutdown_{-1.0f};

    void build_report_descriptor();
    void update_simulation_data();
    float get_elapsed_seconds() const;
    int64_t value_of(Value value) const;
    void set_value(Value value, int64_t raw);
    size_t report_length(uint8_t report_id) const;
};

} // namespace ups_hid
} // namespace esphome
