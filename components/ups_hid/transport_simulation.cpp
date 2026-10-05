#include "transport_simulation.h"
#include "constants_hid.h"
#include "constants_ups.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace esphome {
namespace ups_hid {

static const char *const SIM_TRANSPORT_TAG = "ups_hid.simulation";

namespace {

using Value = SimulatedTransport::Value;

// Usage path nodes (page << 16 | usage id)
constexpr uint32_t UPS = HID_USAGE_POW(HID_USAGE_POW_UPS);
constexpr uint32_t POWER_SUMMARY = HID_USAGE_POW(HID_USAGE_POW_POWER_SUMMARY);
constexpr uint32_t PRESENT_STATUS = HID_USAGE_POW(HID_USAGE_POW_PRESENT_STATUS);
constexpr uint32_t BATTERY = HID_USAGE_POW(HID_USAGE_POW_BATTERY);
constexpr uint32_t INPUT = HID_USAGE_POW(HID_USAGE_POW_INPUT);
constexpr uint32_t POWER_CONVERTER = HID_USAGE_POW(HID_USAGE_POW_POWER_CONVERTER);
constexpr uint32_t APC_GENERAL_COLLECTION = 0xFF860005;

// Units as HID encodes them
constexpr uint32_t UNIT_NONE = 0;
constexpr uint32_t UNIT_VOLT = 0x00F0D121;
constexpr uint32_t UNIT_WATT = 0x0000D121;
constexpr uint32_t UNIT_SECOND = 0x00001001;

struct SimField {
  uint8_t report_id;
  uint8_t length;  // path nodes, from UPS to the usage
  uint32_t path[4];
  uint8_t bits;
  int32_t logical_min;
  int32_t logical_max;
  uint32_t unit;
  int8_t unit_exponent;  // HID unit exponent; volts and watts use 7 for the plain unit (NUT)
  Value value;
};

// An APC Back-UPS ES as NUT apc-hid.c sees it, with the report IDs of a real Back-UPS
const SimField SIM_FIELDS[] = {
    {0x0C, 3, {UPS, POWER_SUMMARY, HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_CAPACITY)}, 8, 0, 100, UNIT_NONE, 0,
     Value::CHARGE},
    {0x0C, 3, {UPS, POWER_SUMMARY, HID_USAGE_BAT(HID_USAGE_BAT_RUN_TIME_TO_EMPTY)}, 16, 0, 65535, UNIT_SECOND, 0,
     Value::RUNTIME},
    {0x0E, 3, {UPS, POWER_SUMMARY, HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_CAPACITY_LIMIT)}, 8, 0, 100, UNIT_NONE, 0,
     Value::CHARGE_LOW},
    {0x24, 3, {UPS, POWER_SUMMARY, HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_TIME_LIMIT)}, 16, 0, 65535, UNIT_SECOND, 0,
     Value::RUNTIME_LOW},
    {0x25, 3, {UPS, POWER_SUMMARY, HID_USAGE_POW(HID_USAGE_POW_CONFIG_VOLTAGE)}, 16, 0, 65535, UNIT_VOLT, 5,
     Value::BATTERY_VOLTAGE_NOMINAL},
    {0x26, 3, {UPS, POWER_SUMMARY, HID_USAGE_POW(HID_USAGE_POW_VOLTAGE)}, 16, 0, 65535, UNIT_VOLT, 5,
     Value::BATTERY_VOLTAGE},
    {0x03, 3, {UPS, POWER_SUMMARY, HID_USAGE_BAT(HID_USAGE_BAT_I_DEVICE_CHEMISTRY)}, 8, 0, 255, UNIT_NONE, 0,
     Value::CHEMISTRY},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_BAT(HID_USAGE_BAT_CHARGING)}, 1, 0, 1, UNIT_NONE, 0,
     Value::CHARGING},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_BAT(HID_USAGE_BAT_DISCHARGING)}, 1, 0, 1, UNIT_NONE, 0,
     Value::DISCHARGING},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_BAT(HID_USAGE_BAT_AC_PRESENT)}, 1, 0, 1, UNIT_NONE, 0,
     Value::AC_PRESENT},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_BAT(HID_USAGE_BAT_BATTERY_PRESENT)}, 1, 0, 1,
     UNIT_NONE, 0, Value::BATTERY_PRESENT},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_BAT(HID_USAGE_BAT_BELOW_REMAINING_CAPACITY_LIMIT)}, 1,
     0, 1, UNIT_NONE, 0, Value::BELOW_CAPACITY_LIMIT},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_POW(HID_USAGE_POW_SHUTDOWN_IMMINENT)}, 1, 0, 1,
     UNIT_NONE, 0, Value::SHUTDOWN_IMMINENT},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_TIME_LIMIT_EXPIRED)}, 1, 0,
     1, UNIT_NONE, 0, Value::TIME_LIMIT_EXPIRED},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_BAT(HID_USAGE_BAT_NEED_REPLACEMENT)}, 1, 0, 1,
     UNIT_NONE, 0, Value::NEED_REPLACEMENT},
    {0x16, 4, {UPS, POWER_SUMMARY, PRESENT_STATUS, HID_USAGE_POW(HID_USAGE_POW_OVERLOAD)}, 1, 0, 1, UNIT_NONE, 0,
     Value::OVERLOAD},
    {0x18, 3, {UPS, POWER_SUMMARY, HID_USAGE_POW(HID_USAGE_POW_AUDIBLE_ALARM_CONTROL)}, 8, 1, 3, UNIT_NONE, 0,
     Value::BEEPER},
    {0x20, 3, {UPS, BATTERY, HID_USAGE_BAT(HID_USAGE_BAT_MANUFACTURER_DATE)}, 16, 0, 65535, UNIT_NONE, 0,
     Value::BATTERY_MFR_DATE},
    {0x52, 3, {UPS, BATTERY, HID_USAGE_POW(HID_USAGE_POW_TEST)}, 8, 0, 7, UNIT_NONE, 0, Value::BATTERY_TEST},
    {0x30, 3, {UPS, INPUT, HID_USAGE_POW(HID_USAGE_POW_CONFIG_VOLTAGE)}, 8, 0, 255, UNIT_VOLT, 7,
     Value::INPUT_VOLTAGE_NOMINAL},
    {0x31, 3, {UPS, INPUT, HID_USAGE_POW(HID_USAGE_POW_VOLTAGE)}, 16, 0, 65535, UNIT_VOLT, 7, Value::INPUT_VOLTAGE},
    {0x32, 3, {UPS, INPUT, HID_USAGE_POW(HID_USAGE_POW_LOW_VOLTAGE_TRANSFER)}, 16, 0, 65535, UNIT_VOLT, 7,
     Value::TRANSFER_LOW},
    {0x33, 3, {UPS, INPUT, HID_USAGE_POW(HID_USAGE_POW_HIGH_VOLTAGE_TRANSFER)}, 16, 0, 65535, UNIT_VOLT, 7,
     Value::TRANSFER_HIGH},
    {0x35, 3, {UPS, INPUT, 0xFF860061 /* APCSensitivity */}, 8, 0, 2, UNIT_NONE, 0, Value::SENSITIVITY},
    {0x50, 3, {UPS, POWER_CONVERTER, HID_USAGE_POW(HID_USAGE_POW_PERCENT_LOAD)}, 8, 0, 100, UNIT_NONE, 0,
     Value::LOAD},
    {0x51, 3, {UPS, POWER_CONVERTER, HID_USAGE_POW(HID_USAGE_POW_CONFIG_ACTIVE_POWER)}, 16, 0, 65535, UNIT_WATT, 7,
     Value::REALPOWER_NOMINAL},
    {0x17, 2, {UPS, 0xFF860072 /* APCPanelTest */}, 8, 0, 1, UNIT_NONE, 0, Value::PANEL_TEST},
    {0x40, 3, {UPS, APC_GENERAL_COLLECTION, 0xFF86007C /* APCDelayBeforeReboot */}, 16, -1, 32767, UNIT_SECOND, 0,
     Value::TIMER_REBOOT},
    {0x41, 3, {UPS, APC_GENERAL_COLLECTION, 0xFF86007D /* APCDelayBeforeShutdown */}, 16, -1, 32767, UNIT_SECOND, 0,
     Value::TIMER_SHUTDOWN},
};
constexpr size_t SIM_FIELD_COUNT = sizeof(SIM_FIELDS) / sizeof(SIM_FIELDS[0]);

// HID short item prefixes (tag | type), the size bits are added when written
constexpr uint8_t ITEM_USAGE_PAGE = 0x04;
constexpr uint8_t ITEM_LOGICAL_MIN = 0x14;
constexpr uint8_t ITEM_LOGICAL_MAX = 0x24;
constexpr uint8_t ITEM_UNIT_EXPONENT = 0x54;
constexpr uint8_t ITEM_UNIT = 0x64;
constexpr uint8_t ITEM_REPORT_SIZE = 0x74;
constexpr uint8_t ITEM_REPORT_ID = 0x84;
constexpr uint8_t ITEM_REPORT_COUNT = 0x94;
constexpr uint8_t ITEM_USAGE = 0x08;
constexpr uint8_t ITEM_COLLECTION = 0xA0;
constexpr uint8_t ITEM_END_COLLECTION = 0xC0;
constexpr uint8_t ITEM_FEATURE = 0xB0;
constexpr uint8_t COLLECTION_PHYSICAL = 0x00;
constexpr uint8_t COLLECTION_APPLICATION = 0x01;
constexpr uint8_t FEATURE_DATA_VARIABLE = 0x02;

void write_item(std::vector<uint8_t> &out, uint8_t prefix, uint32_t value, uint8_t size) {
  const uint8_t size_code = size == 4 ? 3 : size;
  out.push_back(static_cast<uint8_t>(prefix | size_code));
  for (uint8_t i = 0; i < size; i++) {
    out.push_back(static_cast<uint8_t>(value >> (8 * i)));
  }
}

// Logical minimum/maximum are signed: use the smallest size that keeps the sign
void write_signed_item(std::vector<uint8_t> &out, uint8_t prefix, int32_t value) {
  const uint8_t size = value >= -128 && value <= 127 ? 1 : (value >= -32768 && value <= 32767 ? 2 : 4);
  write_item(out, prefix, static_cast<uint32_t>(value), size);
}

void write_usage(std::vector<uint8_t> &out, uint32_t usage) {
  write_item(out, ITEM_USAGE_PAGE, usage >> 16, 2);
  write_item(out, ITEM_USAGE, usage & 0xFFFF, 2);
}

}  // namespace

SimulatedTransport::SimulatedTransport() {
    start_time_ = std::chrono::steady_clock::now();
    last_update_ = start_time_;
    build_report_descriptor();
}

void SimulatedTransport::build_report_descriptor() {
    report_descriptor_.clear();
    layout_.clear();
    std::vector<std::pair<uint8_t, uint16_t>> report_bits;  // report ID, bits used so far

    write_usage(report_descriptor_, UPS);
    write_item(report_descriptor_, ITEM_COLLECTION, COLLECTION_APPLICATION, 1);
    for (const auto &field : SIM_FIELDS) {
        // Each field opens the collections of its path and closes them again
        for (uint8_t node = 1; node + 1 < field.length; node++) {
            write_usage(report_descriptor_, field.path[node]);
            write_item(report_descriptor_, ITEM_COLLECTION, COLLECTION_PHYSICAL, 1);
        }
        write_item(report_descriptor_, ITEM_REPORT_ID, field.report_id, 1);
        write_signed_item(report_descriptor_, ITEM_LOGICAL_MIN, field.logical_min);
        write_signed_item(report_descriptor_, ITEM_LOGICAL_MAX, field.logical_max);
        write_item(report_descriptor_, ITEM_UNIT, field.unit, 4);
        write_item(report_descriptor_, ITEM_UNIT_EXPONENT, static_cast<uint8_t>(field.unit_exponent), 1);
        write_item(report_descriptor_, ITEM_REPORT_SIZE, field.bits, 1);
        write_item(report_descriptor_, ITEM_REPORT_COUNT, 1, 1);
        write_usage(report_descriptor_, field.path[field.length - 1]);
        write_item(report_descriptor_, ITEM_FEATURE, FEATURE_DATA_VARIABLE, 1);
        for (uint8_t node = 1; node + 1 < field.length; node++) {
            write_item(report_descriptor_, ITEM_END_COLLECTION, 0, 0);
        }

        // Fields of one report follow each other in table order
        auto bits = std::find_if(report_bits.begin(), report_bits.end(),
                                 [&field](const std::pair<uint8_t, uint16_t> &entry) { return entry.first == field.report_id; });
        if (bits == report_bits.end()) {
            report_bits.emplace_back(field.report_id, 0);
            bits = report_bits.end() - 1;
        }
        layout_.push_back({field.report_id, bits->second});
        bits->second += field.bits;
    }
    write_item(report_descriptor_, ITEM_END_COLLECTION, 0, 0);
}

esp_err_t SimulatedTransport::initialize() {
    if (initialized_) {
        return ESP_OK;
    }

    ESP_LOGI(SIM_TRANSPORT_TAG, "Initializing simulated USB transport");
    ESP_LOGI(SIM_TRANSPORT_TAG, "Simulating APC Back-UPS ES (VID=0x%04X, PID=0x%04X)", vendor_id_, product_id_);

    initialized_ = true;
    connected_ = true;

    return ESP_OK;
}

esp_err_t SimulatedTransport::deinitialize() {
    if (!initialized_) {
        return ESP_OK;
    }

    ESP_LOGI(SIM_TRANSPORT_TAG, "Deinitializing simulated USB transport");

    initialized_ = false;
    connected_ = false;

    return ESP_OK;
}

bool SimulatedTransport::is_connected() const {
    return connected_ && initialized_;
}

uint16_t SimulatedTransport::get_vendor_id() const {
    return vendor_id_;
}

uint16_t SimulatedTransport::get_product_id() const {
    return product_id_;
}

size_t SimulatedTransport::report_length(uint8_t report_id) const {
    size_t bits = 0;
    for (size_t i = 0; i < SIM_FIELD_COUNT; i++) {
        if (layout_[i].report_id == report_id) {
            bits = std::max<size_t>(bits, layout_[i].bit_offset + SIM_FIELDS[i].bits);
        }
    }
    return bits == 0 ? 0 : 1 + (bits + 7) / 8;
}

esp_err_t SimulatedTransport::hid_get_report(uint8_t report_type, uint8_t report_id,
                                           uint8_t* data, size_t* data_len,
                                           uint32_t timeout_ms) {
    if (!is_connected()) {
        last_error_ = "Simulated transport not connected";
        return ESP_ERR_INVALID_STATE;
    }

    const size_t length = report_length(report_id);
    if (length == 0 || length > limits::MAX_HID_REPORT_SIZE) {
        // A real UPS stalls the request for a report it does not have
        last_error_ = "Simulated UPS has no report " + std::to_string(report_id);
        return ESP_FAIL;
    }

    update_simulation_data();

    uint8_t report[limits::MAX_HID_REPORT_SIZE];
    memset(report, 0, sizeof(report));
    report[0] = report_id;
    for (size_t i = 0; i < SIM_FIELD_COUNT; i++) {
        if (layout_[i].report_id != report_id) {
            continue;
        }
        const uint64_t raw = static_cast<uint64_t>(value_of(SIM_FIELDS[i].value));
        for (uint8_t bit = 0; bit < SIM_FIELDS[i].bits; bit++) {
            const size_t position = 8 + layout_[i].bit_offset + bit;
            if (raw & (1ULL << bit)) {
                report[position >> 3] |= static_cast<uint8_t>(1u << (position & 7));
            }
        }
    }

    const size_t copied = std::min(length, *data_len);
    memcpy(data, report, copied);
    *data_len = copied;
    ESP_LOGV(SIM_TRANSPORT_TAG, "Simulated GET_REPORT: type=0x%02X, id=0x%02X, %zu bytes", report_type, report_id,
             copied);
    return ESP_OK;
}

esp_err_t SimulatedTransport::hid_set_report(uint8_t report_type, uint8_t report_id,
                                           const uint8_t* data, size_t data_len,
                                           uint32_t timeout_ms) {
    if (!is_connected()) {
        last_error_ = "Simulated transport not connected";
        return ESP_ERR_INVALID_STATE;
    }
    if (data_len < report_length(report_id) || data_len == 0 || data[0] != report_id) {
        last_error_ = "Simulated UPS rejected report " + std::to_string(report_id);
        return ESP_FAIL;
    }

    update_simulation_data();

    for (size_t i = 0; i < SIM_FIELD_COUNT; i++) {
        if (layout_[i].report_id != report_id) {
            continue;
        }
        const SimField &field = SIM_FIELDS[i];
        uint64_t raw = 0;
        for (uint8_t bit = 0; bit < field.bits; bit++) {
            const size_t position = 8 + layout_[i].bit_offset + bit;
            if (data[position >> 3] & (1u << (position & 7))) {
                raw |= 1ULL << bit;
            }
        }
        int64_t value = static_cast<int64_t>(raw);
        if (field.logical_min < 0 && (raw & (1ULL << (field.bits - 1))) != 0) {
            value -= static_cast<int64_t>(1ULL << field.bits);  // sign-extend
        }
        set_value(field.value, value);
    }
    return ESP_OK;
}

esp_err_t SimulatedTransport::get_string_descriptor(uint8_t string_index,
                                                  std::string& result) {
    if (!is_connected()) {
        last_error_ = "Simulated transport not connected";
        return ESP_ERR_INVALID_STATE;
    }

    switch (string_index) {
        case 1:
            result = "American Power Conversion";
            break;
        case 2:
            result = "Back-UPS ES 700 FW:866.L4 .I USB FW:L4";
            break;
        case 3:
            result = "4B2037P30628";
            break;
        case 4:  // battery chemistry, referenced by UPS.PowerSummary.iDeviceChemistry
            result = "PbAc";
            break;
        default:
            last_error_ = "Simulated UPS has no string " + std::to_string(string_index);
            return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t SimulatedTransport::get_hid_report_descriptor(uint8_t descriptor_index,
                                                      std::vector<uint8_t>& descriptor) {
    if (!is_connected()) {
        last_error_ = "Simulated transport not connected";
        return ESP_ERR_INVALID_STATE;
    }
    if (descriptor_index != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    descriptor = report_descriptor_;
    return ESP_OK;
}

uint8_t SimulatedTransport::get_device_string_index(UsbDeviceString which) const {
    switch (which) {
        case UsbDeviceString::MANUFACTURER: return 1;
        case UsbDeviceString::PRODUCT: return 2;
        case UsbDeviceString::SERIAL_NUMBER: return 3;
        default: return 0;
    }
}

std::string SimulatedTransport::get_last_error() const {
    return last_error_;
}

float SimulatedTransport::get_elapsed_seconds() const {
    auto duration = std::chrono::steady_clock::now() - start_time_;
    return std::chrono::duration<float>(duration).count();
}

void SimulatedTransport::update_simulation_data() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_update_).count();
    last_update_ = now;
    const float elapsed = get_elapsed_seconds();

    // Mains fails for the last 30 seconds of every 5 minutes
    on_battery_ = std::fmod(elapsed, 300.0f) >= 270.0f;
    input_voltage_ = on_battery_ ? 0.0f : 120.0f + std::sin(elapsed * 0.1f) * 2.0f;
    load_percent_ = 25.0f + std::sin(elapsed * 0.2f) * 5.0f;
    if (on_battery_) {
        battery_level_ = std::max(10.0f, battery_level_ - dt * 0.5f);
    } else {
        battery_level_ = std::min(100.0f, battery_level_ + dt * 0.1f);
    }

    // A started battery test passes after 10 seconds
    if (test_result_ == 5 && test_started_ >= 0.0f && elapsed - test_started_ >= 10.0f) {
        test_result_ = 1;
        test_started_ = -1.0f;
    }

    // Running countdowns
    for (float *timer : {&timer_reboot_, &timer_shutdown_}) {
        if (*timer >= 0.0f) {
            *timer -= dt;
            if (*timer < 0.0f) {
                *timer = -1.0f;
            }
        }
    }
}

int64_t SimulatedTransport::value_of(Value value) const {
    switch (value) {
        case Value::CHARGE: return std::lround(battery_level_);
        case Value::RUNTIME: return std::lround(battery_level_ * 36.0f);  // seconds
        case Value::CHARGE_LOW: return 10;
        case Value::RUNTIME_LOW: return 120;
        case Value::BATTERY_VOLTAGE: return on_battery_ ? std::lround(2300.0f + battery_level_ * 4.0f) : 2720;  // cV
        case Value::BATTERY_VOLTAGE_NOMINAL: return 2400;
        case Value::CHEMISTRY: return 4;  // string index of "PbAc"
        case Value::BATTERY_MFR_DATE: return ((2021 - 1980) << 9) | (3 << 5) | 15;  // 2021/03/15
        case Value::INPUT_VOLTAGE: return std::lround(input_voltage_);
        case Value::INPUT_VOLTAGE_NOMINAL: return 120;
        case Value::TRANSFER_LOW: return 92;
        case Value::TRANSFER_HIGH: return 139;
        case Value::SENSITIVITY: return 1;  // medium
        case Value::LOAD: return std::lround(load_percent_);
        case Value::REALPOWER_NOMINAL: return 405;
        case Value::BEEPER: return beeper_;
        case Value::CHARGING: return !on_battery_ && battery_level_ < 99.5f;
        case Value::DISCHARGING: return on_battery_;
        case Value::AC_PRESENT: return !on_battery_;
        case Value::BATTERY_PRESENT: return 1;
        case Value::BELOW_CAPACITY_LIMIT: return battery_level_ <= 10.0f;
        case Value::SHUTDOWN_IMMINENT: return 0;
        case Value::TIME_LIMIT_EXPIRED: return 0;
        case Value::NEED_REPLACEMENT: return 0;
        case Value::OVERLOAD: return load_percent_ > 100.0f;
        case Value::BATTERY_TEST: return test_result_;
        case Value::PANEL_TEST: return panel_test_;
        case Value::TIMER_REBOOT: return timer_reboot_ < 0.0f ? -1 : std::lround(timer_reboot_);
        case Value::TIMER_SHUTDOWN: return timer_shutdown_ < 0.0f ? -1 : std::lround(timer_shutdown_);
    }
    return 0;
}

void SimulatedTransport::set_value(Value value, int64_t raw) {
    switch (value) {
        case Value::BEEPER:
            if (raw >= 1 && raw <= 3 && raw != beeper_) {
                beeper_ = raw;
                ESP_LOGI(SIM_TRANSPORT_TAG, "Simulated beeper %s", raw == 1 ? "disabled" : (raw == 2 ? "enabled" : "muted"));
            }
            break;
        case Value::BATTERY_TEST:
            if (raw == 1 || raw == 2) {
                test_result_ = 5;  // in progress
                test_started_ = get_elapsed_seconds();
                ESP_LOGI(SIM_TRANSPORT_TAG, "Simulated %s battery test started", raw == 1 ? "quick" : "deep");
            } else if (raw == 3 && test_result_ == 5) {
                test_result_ = 4;  // aborted
                test_started_ = -1.0f;
                ESP_LOGI(SIM_TRANSPORT_TAG, "Simulated battery test aborted");
            }
            break;
        case Value::PANEL_TEST:
            panel_test_ = raw ? 1 : 0;
            break;
        case Value::TIMER_REBOOT:
            timer_reboot_ = static_cast<float>(raw);
            break;
        case Value::TIMER_SHUTDOWN:
            timer_shutdown_ = static_cast<float>(raw);
            break;
        default:
            break;  // read-only
    }
}

} // namespace ups_hid
} // namespace esphome
