#include "protocol_eaton.h"
#include "constants_hid.h"
#include "constants_ups.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace esphome {
namespace ups_hid {

static const char *const EATON_TAG = "ups_hid.eaton";

namespace {

using Item = EatonHidProtocol::Item;

// Usage path nodes (page << 16 | usage id)
constexpr uint32_t U_UPS = HID_USAGE_POW(HID_USAGE_POW_UPS);
constexpr uint32_t U_PRESENT_STATUS = HID_USAGE_POW(HID_USAGE_POW_PRESENT_STATUS);
constexpr uint32_t U_BATTERY_SYSTEM = HID_USAGE_POW(HID_USAGE_POW_BATTERY_SYSTEM);
constexpr uint32_t U_BATTERY = HID_USAGE_POW(HID_USAGE_POW_BATTERY);
constexpr uint32_t U_CHARGER = HID_USAGE_POW(HID_USAGE_POW_CHARGER);
constexpr uint32_t U_POWER_CONVERTER = HID_USAGE_POW(HID_USAGE_POW_POWER_CONVERTER);
constexpr uint32_t U_INPUT = HID_USAGE_POW(HID_USAGE_POW_INPUT);
constexpr uint32_t U_OUTPUT = HID_USAGE_POW(HID_USAGE_POW_OUTPUT);
constexpr uint32_t U_FLOW = HID_USAGE_POW(HID_USAGE_POW_FLOW);
constexpr uint32_t U_POWER_SUMMARY = HID_USAGE_POW(HID_USAGE_POW_POWER_SUMMARY);
constexpr uint32_t U_VOLTAGE = HID_USAGE_POW(HID_USAGE_POW_VOLTAGE);
constexpr uint32_t U_FREQUENCY = HID_USAGE_POW(HID_USAGE_POW_FREQUENCY);
constexpr uint32_t U_PERCENT_LOAD = HID_USAGE_POW(HID_USAGE_POW_PERCENT_LOAD);
constexpr uint32_t U_CONFIG_VOLTAGE = HID_USAGE_POW(HID_USAGE_POW_CONFIG_VOLTAGE);
constexpr uint32_t U_CONFIG_APPARENT_POWER = HID_USAGE_POW(HID_USAGE_POW_CONFIG_APPARENT_POWER);
constexpr uint32_t U_CONFIG_ACTIVE_POWER = HID_USAGE_POW(HID_USAGE_POW_CONFIG_ACTIVE_POWER);
constexpr uint32_t U_LOW_VOLTAGE_TRANSFER = HID_USAGE_POW(HID_USAGE_POW_LOW_VOLTAGE_TRANSFER);
constexpr uint32_t U_HIGH_VOLTAGE_TRANSFER = HID_USAGE_POW(HID_USAGE_POW_HIGH_VOLTAGE_TRANSFER);
constexpr uint32_t U_DELAY_BEFORE_REBOOT = HID_USAGE_POW(HID_USAGE_POW_DELAY_BEFORE_REBOOT);
constexpr uint32_t U_DELAY_BEFORE_STARTUP = HID_USAGE_POW(HID_USAGE_POW_DELAY_BEFORE_STARTUP);
constexpr uint32_t U_DELAY_BEFORE_SHUTDOWN = HID_USAGE_POW(HID_USAGE_POW_DELAY_BEFORE_SHUTDOWN);
constexpr uint32_t U_TEST = HID_USAGE_POW(HID_USAGE_POW_TEST);
constexpr uint32_t U_AUDIBLE_ALARM_CONTROL = HID_USAGE_POW(HID_USAGE_POW_AUDIBLE_ALARM_CONTROL);
constexpr uint32_t U_PRESENT = HID_USAGE_POW(HID_USAGE_POW_PRESENT);
constexpr uint32_t U_GOOD = HID_USAGE_POW(HID_USAGE_POW_GOOD);
constexpr uint32_t U_INTERNAL_FAILURE = HID_USAGE_POW(HID_USAGE_POW_INTERNAL_FAILURE);
constexpr uint32_t U_OVERLOAD = HID_USAGE_POW(HID_USAGE_POW_OVERLOAD);
constexpr uint32_t U_OVER_TEMPERATURE = HID_USAGE_POW(HID_USAGE_POW_OVER_TEMPERATURE);
constexpr uint32_t U_SHUTDOWN_IMMINENT = HID_USAGE_POW(HID_USAGE_POW_SHUTDOWN_IMMINENT);
constexpr uint32_t U_USED = HID_USAGE_POW(HID_USAGE_POW_USED);
constexpr uint32_t U_BOOST = HID_USAGE_POW(HID_USAGE_POW_BOOST);
constexpr uint32_t U_BUCK = HID_USAGE_POW(HID_USAGE_POW_BUCK);
constexpr uint32_t U_I_MANUFACTURER = HID_USAGE_POW(HID_USAGE_POW_I_MANUFACTURER);
constexpr uint32_t U_I_PRODUCT = HID_USAGE_POW(HID_USAGE_POW_I_PRODUCT);
constexpr uint32_t U_I_SERIAL_NUMBER = HID_USAGE_POW(HID_USAGE_POW_I_SERIAL_NUMBER);
constexpr uint32_t U_REMAINING_CAPACITY_LIMIT = HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_CAPACITY_LIMIT);
constexpr uint32_t U_REMAINING_TIME_LIMIT = HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_TIME_LIMIT);
constexpr uint32_t U_BELOW_REMAINING_CAPACITY_LIMIT = HID_USAGE_BAT(HID_USAGE_BAT_BELOW_REMAINING_CAPACITY_LIMIT);
constexpr uint32_t U_CHARGING = HID_USAGE_BAT(HID_USAGE_BAT_CHARGING);
constexpr uint32_t U_DISCHARGING = HID_USAGE_BAT(HID_USAGE_BAT_DISCHARGING);
constexpr uint32_t U_NEED_REPLACEMENT = HID_USAGE_BAT(HID_USAGE_BAT_NEED_REPLACEMENT);
constexpr uint32_t U_REMAINING_CAPACITY = HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_CAPACITY);
constexpr uint32_t U_RUN_TIME_TO_EMPTY = HID_USAGE_BAT(HID_USAGE_BAT_RUN_TIME_TO_EMPTY);
constexpr uint32_t U_I_DEVICE_CHEMISTRY = HID_USAGE_BAT(HID_USAGE_BAT_I_DEVICE_CHEMISTRY);
constexpr uint32_t U_AC_PRESENT = HID_USAGE_BAT(HID_USAGE_BAT_AC_PRESENT);

// MGE UPS SYSTEMS / Eaton vendor usage page 0xFFFF (names from NUT mge-hid.c)
constexpr uint32_t MGE_SENSITIVITY_MODE = 0xFFFF004C;
constexpr uint32_t MGE_REMAINING_CAPACITY_LIMIT_SETTING = 0xFFFF004D;
constexpr uint32_t MGE_FAN_FAILURE = 0xFFFF0077;
constexpr uint32_t MGE_ABM_ENABLE = 0xFFFF0087;
constexpr uint32_t MGE_MODE = 0xFFFF0094;
constexpr uint32_t MGE_STATUS = 0xFFFF00E9;
constexpr uint32_t MGE_I_MODEL = 0xFFFF00F0;
constexpr uint32_t MGE_I_VERSION = 0xFFFF00F1;

// Indexed collections: UPS.Flow.[1] utility input, UPS.Flow.[4] UPS output;
// UPS.PowerConverter.Input.[1] main input, [2] automatic bypass, [3] battery, [4] manual bypass
constexpr uint32_t IDX_1 = HID_PATH_INDEX_BASE | 1;
constexpr uint32_t IDX_2 = HID_PATH_INDEX_BASE | 2;
constexpr uint32_t IDX_3 = HID_PATH_INDEX_BASE | 3;
constexpr uint32_t IDX_4 = HID_PATH_INDEX_BASE | 4;

constexpr size_t MAX_PATH_NODES = 6;

struct PathDefinition {
  Item item;
  const char *name;  // NUT variable fed by this path, for logs
  uint8_t length;
  uint32_t nodes[MAX_PATH_NODES];
};

// Paths as used by NUT mge-hid.c. When several paths feed one item, the first one
// present in the descriptor is used.
const PathDefinition EATON_PATHS[] = {
    {Item::ITEM_MANUFACTURER, "ups.mfr", 3, {U_UPS, U_POWER_SUMMARY, U_I_MANUFACTURER}},
    {Item::ITEM_PRODUCT, "ups.model (product)", 3, {U_UPS, U_POWER_SUMMARY, U_I_PRODUCT}},
    {Item::ITEM_MODEL, "ups.model (model)", 3, {U_UPS, U_POWER_SUMMARY, MGE_I_MODEL}},
    {Item::ITEM_SERIAL_NUMBER, "ups.serial", 3, {U_UPS, U_POWER_SUMMARY, U_I_SERIAL_NUMBER}},
    {Item::ITEM_FIRMWARE, "ups.firmware", 3, {U_UPS, U_POWER_SUMMARY, MGE_I_VERSION}},
    {Item::ITEM_BATTERY_TYPE, "battery.type", 3, {U_UPS, U_POWER_SUMMARY, U_I_DEVICE_CHEMISTRY}},

    {Item::ITEM_INPUT_VOLTAGE_NOMINAL, "input.voltage.nominal", 4, {U_UPS, U_FLOW, IDX_1, U_CONFIG_VOLTAGE}},
    {Item::ITEM_OUTPUT_VOLTAGE_NOMINAL, "output.voltage.nominal", 4, {U_UPS, U_FLOW, IDX_4, U_CONFIG_VOLTAGE}},
    {Item::ITEM_APPARENT_POWER_NOMINAL, "ups.power.nominal", 4, {U_UPS, U_FLOW, IDX_4, U_CONFIG_APPARENT_POWER}},
    {Item::ITEM_ACTIVE_POWER_NOMINAL, "ups.realpower.nominal", 4, {U_UPS, U_FLOW, IDX_4, U_CONFIG_ACTIVE_POWER}},
    {Item::ITEM_BATTERY_VOLTAGE_NOMINAL, "battery.voltage.nominal", 3, {U_UPS, U_BATTERY_SYSTEM, U_CONFIG_VOLTAGE}},
    {Item::ITEM_SUMMARY_VOLTAGE_NOMINAL, "battery.voltage.nominal", 3, {U_UPS, U_POWER_SUMMARY, U_CONFIG_VOLTAGE}},

    {Item::ITEM_BATTERY_CHARGE, "battery.charge", 3, {U_UPS, U_POWER_SUMMARY, U_REMAINING_CAPACITY}},
    {Item::ITEM_BATTERY_RUNTIME, "battery.runtime", 3, {U_UPS, U_POWER_SUMMARY, U_RUN_TIME_TO_EMPTY}},
    {Item::ITEM_BATTERY_CHARGE_LOW, "battery.charge.low", 3, {U_UPS, U_POWER_SUMMARY, MGE_REMAINING_CAPACITY_LIMIT_SETTING}},
    {Item::ITEM_BATTERY_CHARGE_LOW, "battery.charge.low", 3, {U_UPS, U_POWER_SUMMARY, U_REMAINING_CAPACITY_LIMIT}},
    {Item::ITEM_BATTERY_RUNTIME_LOW, "battery.runtime.low", 3, {U_UPS, U_POWER_SUMMARY, U_REMAINING_TIME_LIMIT}},
    {Item::ITEM_BATTERY_VOLTAGE, "battery.voltage", 3, {U_UPS, U_BATTERY_SYSTEM, U_VOLTAGE}},
    {Item::ITEM_SUMMARY_VOLTAGE, "battery.voltage", 3, {U_UPS, U_POWER_SUMMARY, U_VOLTAGE}},

    {Item::ITEM_INPUT_VOLTAGE, "input.voltage", 5, {U_UPS, U_POWER_CONVERTER, U_INPUT, IDX_1, U_VOLTAGE}},
    {Item::ITEM_INPUT_FREQUENCY, "input.frequency", 5, {U_UPS, U_POWER_CONVERTER, U_INPUT, IDX_1, U_FREQUENCY}},
    {Item::ITEM_OUTPUT_VOLTAGE, "output.voltage", 4, {U_UPS, U_POWER_CONVERTER, U_OUTPUT, U_VOLTAGE}},
    {Item::ITEM_OUTPUT_FREQUENCY, "output.frequency", 4, {U_UPS, U_POWER_CONVERTER, U_OUTPUT, U_FREQUENCY}},
    {Item::ITEM_LOAD, "ups.load", 3, {U_UPS, U_POWER_SUMMARY, U_PERCENT_LOAD}},
    {Item::ITEM_LOAD, "ups.load", 4, {U_UPS, U_POWER_CONVERTER, U_OUTPUT, U_PERCENT_LOAD}},
    {Item::ITEM_TRANSFER_LOW, "input.transfer.low", 4, {U_UPS, U_POWER_CONVERTER, U_OUTPUT, U_LOW_VOLTAGE_TRANSFER}},
    {Item::ITEM_TRANSFER_HIGH, "input.transfer.high", 4, {U_UPS, U_POWER_CONVERTER, U_OUTPUT, U_HIGH_VOLTAGE_TRANSFER}},
    {Item::ITEM_SENSITIVITY, "input.sensitivity", 4, {U_UPS, U_POWER_CONVERTER, U_OUTPUT, MGE_SENSITIVITY_MODE}},
    // 3S units use the BatterySystem path
    {Item::ITEM_BEEPER, "ups.beeper.status", 4, {U_UPS, U_BATTERY_SYSTEM, U_BATTERY, U_AUDIBLE_ALARM_CONTROL}},
    {Item::ITEM_BEEPER, "ups.beeper.status", 3, {U_UPS, U_POWER_SUMMARY, U_AUDIBLE_ALARM_CONTROL}},
    {Item::ITEM_BEEPER, "ups.beeper.status", 2, {U_UPS, U_AUDIBLE_ALARM_CONTROL}},

    {Item::ITEM_AC_PRESENT, "ups.status OL", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_AC_PRESENT}},
    {Item::ITEM_CHARGING, "ups.status CHRG", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_CHARGING}},
    {Item::ITEM_DISCHARGING, "ups.status DISCHRG", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_DISCHARGING}},
    {Item::ITEM_BELOW_CAPACITY_LIMIT, "ups.status LB", 4,
     {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_BELOW_REMAINING_CAPACITY_LIMIT}},
    {Item::ITEM_SHUTDOWN_IMMINENT, "ups.status LB", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_SHUTDOWN_IMMINENT}},
    {Item::ITEM_OVERLOAD, "ups.status OVER", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_OVERLOAD}},
    {Item::ITEM_NEED_REPLACEMENT, "ups.status RB", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_NEED_REPLACEMENT}},
    {Item::ITEM_GOOD, "ups.status OFF", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_GOOD}},
    {Item::ITEM_INTERNAL_FAILURE, "ups.alarm", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_INTERNAL_FAILURE}},
    {Item::ITEM_OVER_TEMPERATURE, "ups.alarm", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_OVER_TEMPERATURE}},
    {Item::ITEM_FAN_FAILURE, "ups.alarm", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, MGE_FAN_FAILURE}},
    {Item::ITEM_BATTERY_PRESENT, "ups.status (battery present)", 5,
     {U_UPS, U_BATTERY_SYSTEM, U_BATTERY, U_PRESENT_STATUS, U_PRESENT}},
    {Item::ITEM_MAIN_INPUT_USED, "ups.status OL", 6, {U_UPS, U_POWER_CONVERTER, U_INPUT, IDX_1, U_PRESENT_STATUS, U_USED}},
    {Item::ITEM_BYPASS_USED, "ups.status BYPASS", 6, {U_UPS, U_POWER_CONVERTER, U_INPUT, IDX_2, U_PRESENT_STATUS, U_USED}},
    {Item::ITEM_BATTERY_USED, "ups.status OB", 6, {U_UPS, U_POWER_CONVERTER, U_INPUT, IDX_3, U_PRESENT_STATUS, U_USED}},
    {Item::ITEM_MANUAL_BYPASS_USED, "ups.status BYPASS", 6,
     {U_UPS, U_POWER_CONVERTER, U_INPUT, IDX_4, U_PRESENT_STATUS, U_USED}},
    {Item::ITEM_BOOST, "ups.status BOOST", 6, {U_UPS, U_POWER_CONVERTER, U_INPUT, IDX_1, U_PRESENT_STATUS, U_BOOST}},
    {Item::ITEM_BUCK, "ups.status TRIM", 6, {U_UPS, U_POWER_CONVERTER, U_INPUT, IDX_1, U_PRESENT_STATUS, U_BUCK}},

    {Item::ITEM_ABM_ENABLED, "battery.charger.status", 4, {U_UPS, U_BATTERY_SYSTEM, U_CHARGER, MGE_ABM_ENABLE}},
    {Item::ITEM_CHARGER_MODE, "battery.charger.status", 4, {U_UPS, U_BATTERY_SYSTEM, U_CHARGER, MGE_MODE}},
    {Item::ITEM_CHARGER_STATUS, "battery.charger.status", 4, {U_UPS, U_BATTERY_SYSTEM, U_CHARGER, MGE_STATUS}},

    {Item::ITEM_BATTERY_TEST, "ups.test.result", 4, {U_UPS, U_BATTERY_SYSTEM, U_BATTERY, U_TEST}},
    {Item::ITEM_TIMER_SHUTDOWN, "ups.timer.shutdown", 3, {U_UPS, U_POWER_SUMMARY, U_DELAY_BEFORE_SHUTDOWN}},
    {Item::ITEM_TIMER_START, "ups.timer.start", 3, {U_UPS, U_POWER_SUMMARY, U_DELAY_BEFORE_STARTUP}},
    {Item::ITEM_TIMER_REBOOT, "ups.timer.reboot", 3, {U_UPS, U_POWER_SUMMARY, U_DELAY_BEFORE_REBOOT}},
};
constexpr size_t PATH_COUNT = sizeof(EATON_PATHS) / sizeof(EATON_PATHS[0]);
static_assert(PATH_COUNT < 0xFF, "path index must fit field_rank_");

// Firmware 2.02 exposes a reduced report descriptor (index 0, for the OS battery
// driver) and the complete one (index 1); NUT libusb1.c reads the complete one
constexpr uint16_t EATON_DUAL_DESCRIPTOR_RELEASE = 0x0202;
constexpr uint8_t EATON_FULL_DESCRIPTOR_INDEX = 1;
constexpr uint8_t EATON_DEFAULT_DESCRIPTOR_INDEX = 0;

constexpr const char *EATON_MANUFACTURER = "Eaton";
constexpr float SECONDS_PER_MINUTE = 60.0f;
constexpr float MAX_PLAUSIBLE_VOLTAGE = 500.0f;
constexpr float MAX_PLAUSIBLE_FREQUENCY = 100.0f;
constexpr float MAX_LOAD_PERCENT = 200.0f;  // overloaded units report above 100%

// Charger state with Advanced Battery Monitoring enabled
enum class ChargerState : uint8_t { UNKNOWN, CHARGING, FLOATING, RESTING, DISCHARGING, OFF };

// UPS.BatterySystem.Charger.Mode values (NUT eaton_abm_status_fun)
ChargerState charger_state_from_mode(int64_t mode) {
  switch (mode) {
    case 1: return ChargerState::CHARGING;
    case 2: return ChargerState::DISCHARGING;
    case 3: return ChargerState::FLOATING;
    case 4: return ChargerState::RESTING;
    case 6: return ChargerState::OFF;
    default: return ChargerState::UNKNOWN;
  }
}

// UPS.BatterySystem.Charger.Status values, used by 9-series units (NUT eaton_abm_status_fun)
ChargerState charger_state_from_status(int64_t status) {
  switch (status) {
    case 1: return ChargerState::CHARGING;
    case 2: return ChargerState::FLOATING;
    case 3: return ChargerState::RESTING;
    case 4: return ChargerState::DISCHARGING;
    case 6: return ChargerState::OFF;
    default: return ChargerState::UNKNOWN;
  }
}

// HID PDC Test values (NUT test_read_info)
const char *test_result_text(int64_t value) {
  switch (value) {
    case 1: return test::RESULT_DONE_PASSED;
    case 2: return test::RESULT_DONE_WARNING;
    case 3: return test::RESULT_DONE_ERROR;
    case 4: return test::RESULT_ABORTED;
    case 5: return test::RESULT_IN_PROGRESS;
    case 6: return test::RESULT_NO_TEST;
    case 7: return test::RESULT_SCHEDULED;
    default: return nullptr;
  }
}

bool in_range(float value, float min, float max) { return !std::isnan(value) && value >= min && value <= max; }

int16_t to_timer(float seconds) {
  const float rounded = std::round(seconds);
  return static_cast<int16_t>(std::max<float>(std::numeric_limits<int16_t>::min(),
                                              std::min<float>(std::numeric_limits<int16_t>::max(), rounded)));
}

// The Eaton 5S identifies itself as "Ellipse PRO" (NUT mge_model_names)
bool is_eaton_5s(const std::string &product, const std::string &model) {
  return product == "Ellipse PRO" && (model == "1200" || model == "1500" || model == "1600");
}

struct SeriesMatch {
  const char *product;
  const char *model;  // nullptr: any model, and product is a prefix
};

// Series whose UPS.PowerSummary.Voltage / ConfigVoltage hold the battery voltage
// (NUT mge-hid EATON_5P, EATON_9E, MGE_PULSAR_M and newer MGE_EVOLUTION types).
// Other series put unrelated values there, so NUT ignores them.
const SeriesMatch SUMMARY_BATTERY_VOLTAGE_SERIES[] = {
    {"Eaton 5P", nullptr},  // 5P and 5PX
    {"Eaton 5SC", nullptr},
    {"Eaton 9E", nullptr},
    {"Eaton 9SX", nullptr},
    {"Eaton 9PX", nullptr},
    {"PULSAR M", nullptr},
    {"EX", "2200"},
    {"EX", "3000"},
    {"EX", "3000 XL"},
    {"Evolution", "650"},
    {"Evolution", "850"},
    {"Evolution", "1150"},
    {"Evolution", "S 1250"},
    {"Evolution", "1550"},
    {"Evolution", "S 1750"},
    {"Evolution", "2000"},
    {"Evolution", "S 2500"},
    {"Evolution", "S 3000"},
    // No product string, model from UPS.Flow.[4].ConfigApparentPower: presumed 9E
    {"", "1000"},
    {"", "2000"},
    {"", "3000"},
};

bool summary_voltage_is_battery(const std::string &product, const std::string &model) {
  if (is_eaton_5s(product, model)) {
    return true;
  }
  for (const auto &series : SUMMARY_BATTERY_VOLTAGE_SERIES) {
    if (series.model == nullptr ? product.rfind(series.product, 0) == 0
                                : product == series.product && model == series.model) {
      return true;
    }
  }
  return false;
}

// NUT get_model_name(): "<iProduct> <iModel>", with a few renamed models
std::string format_model_name(const std::string &product, const std::string &model) {
  if (is_eaton_5s(product, model)) {
    return "Eaton 5S" + model;
  }
  if (product.empty() || model.empty()) {
    return product.empty() ? model : product;
  }
  return product + " " + model;
}

const char *report_type_name(uint8_t report_type) {
  switch (report_type) {
    case HID_REPORT_TYPE_INPUT: return "Input";
    case HID_REPORT_TYPE_OUTPUT: return "Output";
    default: return "Feature";
  }
}

}  // namespace

EatonHidProtocol::EatonHidProtocol(UpsHidComponent *parent) : UpsProtocolBase(parent) {
  std::fill(std::begin(field_rank_), std::end(field_rank_), NO_FIELD);
}

bool EatonHidProtocol::detect() {
  ESP_LOGD(EATON_TAG, "Detecting Eaton HID protocol...");

  if (!parent_->is_device_connected()) {
    ESP_LOGD(EATON_TAG, "Device not connected, skipping protocol detection");
    return false;
  }

  // Give the device time to settle after enumeration
  vTaskDelay(pdMS_TO_TICKS(timing::USB_INITIALIZATION_DELAY_MS));

  if (!load_report_descriptor()) {
    return false;
  }

  if (!has(ITEM_BATTERY_CHARGE) && !has(ITEM_AC_PRESENT)) {
    ESP_LOGW(EATON_TAG, "Report descriptor has no UPS.PowerSummary battery or status data - not an Eaton HID UPS");
    return false;
  }

  // Make sure the UPS answers the reports the descriptor promises
  report_cache_.clear();
  const Item probe = has(ITEM_BATTERY_CHARGE) ? ITEM_BATTERY_CHARGE : ITEM_AC_PRESENT;
  int64_t value;
  if (!read_raw(probe, value)) {
    ESP_LOGW(EATON_TAG, "UPS did not answer HID report 0x%02X", fields_[probe].report_id);
    return false;
  }

  ESP_LOGI(EATON_TAG, "Eaton HID protocol detected");
  return true;
}

bool EatonHidProtocol::initialize() {
  ESP_LOGD(EATON_TAG, "Initializing Eaton HID protocol...");

  // Manual protocol selection (protocol: eaton) skips detect(), which loads the descriptor
  if (!descriptor_loaded_ && !load_report_descriptor()) {
    ESP_LOGE(EATON_TAG, "Cannot initialize without the HID report descriptor");
    return false;
  }

  read_device_info();
  ESP_LOGI(EATON_TAG, "Eaton HID protocol initialized: %s %s (serial: %s, firmware: %s)", manufacturer_.c_str(),
           model_.empty() ? "UPS" : model_.c_str(), serial_number_.empty() ? "unknown" : serial_number_.c_str(),
           firmware_version_.empty() ? "unknown" : firmware_version_.c_str());
  log_data_points();  // after read_device_info(): what is used depends on the model
  return true;
}

bool EatonHidProtocol::load_report_descriptor() {
  std::vector<uint8_t> raw;
  esp_err_t err = ESP_FAIL;

  if (parent_->get_device_release() == EATON_DUAL_DESCRIPTOR_RELEASE) {
    err = parent_->get_hid_report_descriptor(EATON_FULL_DESCRIPTOR_INDEX, raw);
    if (err != ESP_OK) {
      ESP_LOGW(EATON_TAG, "Full report descriptor unavailable (%s), using the reduced one", esp_err_to_name(err));
    }
  }
  if (err != ESP_OK) {
    err = parent_->get_hid_report_descriptor(EATON_DEFAULT_DESCRIPTOR_INDEX, raw);
  }
  if (err != ESP_OK || raw.empty()) {
    ESP_LOGW(EATON_TAG, "Cannot read the HID report descriptor: %s", esp_err_to_name(err));
    return false;
  }

  std::fill(std::begin(field_rank_), std::end(field_rank_), NO_FIELD);
  size_t field_count = 0;
  const bool complete = descriptor_.parse(raw.data(), raw.size(), [this, &field_count](const HidField &field) {
    field_count++;
    ESP_LOGV(EATON_TAG, "Path: %s, Type: %s, ReportID: 0x%02X, Offset: %u, Size: %u",
             HidReportDescriptor::path_to_string(field).c_str(), report_type_name(field.report_type), field.report_id,
             field.bit_offset, field.bit_size);
    map_field(field);
  });
  if (!complete) {
    ESP_LOGW(EATON_TAG, "Report descriptor is malformed or truncated, using the %zu fields before the error",
             field_count);
  }

  ESP_LOGI(EATON_TAG, "HID report descriptor: %zu bytes, %zu fields", raw.size(), field_count);
  descriptor_loaded_ = field_count > 0;
  return descriptor_loaded_;
}

bool EatonHidProtocol::used_on_this_model(Item item) const {
  // UPS.PowerSummary.Voltage / ConfigVoltage only hold the battery voltage on some series
  if (item == ITEM_SUMMARY_VOLTAGE || item == ITEM_SUMMARY_VOLTAGE_NOMINAL) {
    return summary_voltage_is_battery_;
  }
  return true;
}

void EatonHidProtocol::log_data_points() const {
  size_t found = 0;
  size_t unused = 0;
  for (size_t rank = 0; rank < PATH_COUNT; rank++) {
    const PathDefinition &definition = EATON_PATHS[rank];
    if (field_rank_[definition.item] != rank) {
      continue;
    }
    const HidField &field = fields_[definition.item];
    const bool used = used_on_this_model(definition.item);
    found++;
    if (!used) {
      unused++;
    }
    ESP_LOGD(EATON_TAG, "  %-28s <- %s (%s report 0x%02X, bit %u, %u bits)%s", definition.name,
             HidReportDescriptor::path_to_string(field).c_str(), report_type_name(field.report_type), field.report_id,
             field.bit_offset, field.bit_size, used ? "" : " - not used on this model");
  }

  ESP_LOGI(EATON_TAG, "%zu of %u Eaton data points found, %zu not used on this model", found,
           static_cast<unsigned>(ITEM_COUNT), unused);
}

void EatonHidProtocol::map_field(const HidField &field) {
  for (size_t rank = 0; rank < PATH_COUNT; rank++) {
    const PathDefinition &definition = EATON_PATHS[rank];
    if (!field.matches(definition.nodes, definition.length)) {
      continue;
    }
    const Item item = definition.item;
    const uint8_t current = field_rank_[item];
    // Earlier table entries win. For the same path prefer the Feature report, which
    // is what NUT reads; Input reports with the same ID can have another layout.
    const bool better = current == NO_FIELD || rank < current ||
                        (rank == current && field.report_type == HID_REPORT_TYPE_FEATURE &&
                         fields_[item].report_type != HID_REPORT_TYPE_FEATURE);
    if (better) {
      fields_[item] = field;
      field_rank_[item] = static_cast<uint8_t>(rank);
    }
    return;
  }
}

bool EatonHidProtocol::read_raw(Item item, int64_t &value) {
  if (!has(item)) {
    return false;
  }
  const HidField &field = fields_[item];

  // Several values usually share one report: read each report once per poll
  auto cached = std::find_if(report_cache_.begin(), report_cache_.end(), [&field](const CachedReport &report) {
    return report.report_id == field.report_id && report.report_type == field.report_type;
  });
  if (cached == report_cache_.end()) {
    CachedReport report{field.report_id, field.report_type, false, {}};
    size_t length = descriptor_.report_size(field.report_id, field.report_type);
    if (length == 0 || length > limits::MAX_HID_REPORT_SIZE) {
      length = limits::MAX_HID_REPORT_SIZE;
    }
    uint8_t buffer[limits::MAX_HID_REPORT_SIZE];
    size_t received = length;
    esp_err_t err = parent_->hid_get_report(field.report_type, field.report_id, buffer, &received,
                                            parent_->get_protocol_timeout());
    if (err != ESP_OK || received == 0) {
      ESP_LOGV(EATON_TAG, "%s report 0x%02X failed: %s", report_type_name(field.report_type), field.report_id,
               esp_err_to_name(err));
    } else if (field.report_id != 0 && buffer[0] != field.report_id) {
      ESP_LOGD(EATON_TAG, "%s report 0x%02X answered with report ID 0x%02X, ignoring",
               report_type_name(field.report_type), field.report_id, buffer[0]);
    } else {
      report.valid = true;
      report.data.assign(buffer, buffer + received);
    }
    report_cache_.push_back(std::move(report));
    cached = report_cache_.end() - 1;
  }

  return cached->valid && field.extract(cached->data.data(), cached->data.size(), value);
}

bool EatonHidProtocol::read_value(Item item, float &value) {
  int64_t raw;
  if (!read_raw(item, raw)) {
    return false;
  }
  value = static_cast<float>(fields_[item].to_physical(raw));
  return true;
}

bool EatonHidProtocol::read_flag(Item item, bool &value) {
  int64_t raw;
  if (!read_raw(item, raw)) {
    return false;
  }
  value = raw != 0;
  return true;
}

bool EatonHidProtocol::read_string(Item item, std::string &value) {
  int64_t index;
  if (!read_raw(item, index) || index <= 0 || index > UINT8_MAX) {
    return false;
  }
  std::string text;
  if (parent_->get_string_descriptor(static_cast<uint8_t>(index), text) != ESP_OK || text.empty()) {
    return false;
  }
  value = text;
  return true;
}

bool EatonHidProtocol::write_value(Item item, float value, const char *action) {
  if (!has(item)) {
    ESP_LOGW(EATON_TAG, "%s: not supported by this UPS", action);
    return false;
  }
  const HidField &field = fields_[item];
  if (field.report_type != HID_REPORT_TYPE_FEATURE) {
    ESP_LOGW(EATON_TAG, "%s: control is read-only on this UPS", action);
    return false;
  }

  const size_t length = descriptor_.report_size(field.report_id, HID_REPORT_TYPE_FEATURE);
  uint8_t report[limits::MAX_HID_REPORT_SIZE];
  if (length == 0 || length > sizeof(report)) {
    ESP_LOGW(EATON_TAG, "%s: unsupported report size %zu", action, length);
    return false;
  }

  // Read-modify-write, so other settings in the same report keep their values
  size_t received = length;
  esp_err_t err = parent_->hid_get_report(HID_REPORT_TYPE_FEATURE, field.report_id, report, &received,
                                          parent_->get_protocol_timeout());
  if (err != ESP_OK || received < length || (field.report_id != 0 && report[0] != field.report_id)) {
    ESP_LOGW(EATON_TAG, "%s: cannot read report 0x%02X before writing it", action, field.report_id);
    return false;
  }
  field.insert(report, length, field.to_logical(value));

  err = parent_->hid_set_report(HID_REPORT_TYPE_FEATURE, field.report_id, report, length,
                                parent_->get_protocol_timeout());
  if (err != ESP_OK) {
    ESP_LOGW(EATON_TAG, "%s: HID SET_REPORT 0x%02X failed: %s", action, field.report_id, esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(EATON_TAG, "%s: command sent", action);
  return true;
}

void EatonHidProtocol::read_device_info() {
  report_cache_.clear();

  if (!read_string(ITEM_MANUFACTURER, manufacturer_)) {
    manufacturer_ = EATON_MANUFACTURER;
  }
  read_string(ITEM_SERIAL_NUMBER, serial_number_);
  read_string(ITEM_FIRMWARE, firmware_version_);
  read_string(ITEM_BATTERY_TYPE, battery_type_);

  float value;
  if (read_value(ITEM_INPUT_VOLTAGE_NOMINAL, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    input_voltage_nominal_ = value;
  }
  if (read_value(ITEM_OUTPUT_VOLTAGE_NOMINAL, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    output_voltage_nominal_ = value;
  }
  if (read_value(ITEM_APPARENT_POWER_NOMINAL, value) && value > 0) {
    apparent_power_nominal_ = value;
  }
  if (read_value(ITEM_ACTIVE_POWER_NOMINAL, value) && value > 0) {
    active_power_nominal_ = value;
  }

  // Model name and series, as in NUT mge_format_model()
  std::string product;
  std::string model;
  read_string(ITEM_PRODUCT, product);
  read_string(ITEM_MODEL, model);
  if (model.empty() && !std::isnan(apparent_power_nominal_)) {
    model = std::to_string(static_cast<int>(apparent_power_nominal_));
  }
  model_ = format_model_name(product, model);
  summary_voltage_is_battery_ = summary_voltage_is_battery(product, model);

  if (read_value(ITEM_BATTERY_VOLTAGE_NOMINAL, value) && value > 0) {
    battery_voltage_nominal_ = value;
  } else if (summary_voltage_is_battery_ && read_value(ITEM_SUMMARY_VOLTAGE_NOMINAL, value) && value > 0) {
    battery_voltage_nominal_ = value;
  }
}

bool EatonHidProtocol::read_data(UpsData &data) {
  report_cache_.clear();

  bool success = read_battery(data);
  success |= read_power(data);
  success |= read_status(data);  // after battery and power: adjusts their values
  read_settings(data);
  read_timers(data);

  data.device.manufacturer = manufacturer_;
  data.device.model = model_;
  data.device.serial_number = serial_number_;
  data.device.firmware_version = firmware_version_;
  data.device.usb_vendor_id = parent_->get_vendor_id();
  data.device.usb_product_id = parent_->get_product_id();
  data.battery.type = battery_type_;
  data.battery.voltage_nominal = battery_voltage_nominal_;
  data.power.input_voltage_nominal = input_voltage_nominal_;
  data.power.output_voltage_nominal = output_voltage_nominal_;
  data.power.apparent_power_nominal = apparent_power_nominal_;
  data.power.realpower_nominal = active_power_nominal_;

  return success;
}

bool EatonHidProtocol::read_battery(UpsData &data) {
  bool success = false;
  float value;

  if (read_value(ITEM_BATTERY_CHARGE, value) && value >= 0) {
    data.battery.level = std::min(value, battery::MAX_LEVEL_PERCENT);
    success = true;
  }
  if (read_value(ITEM_BATTERY_RUNTIME, value) && value >= 0) {
    data.battery.runtime_minutes = value / SECONDS_PER_MINUTE;
    success = true;
  }
  if (read_value(ITEM_BATTERY_CHARGE_LOW, value) && in_range(value, 0.0f, battery::MAX_LEVEL_PERCENT)) {
    data.battery.charge_low = value;
  }
  if (read_value(ITEM_BATTERY_RUNTIME_LOW, value) && value >= 0) {
    data.battery.runtime_low = value / SECONDS_PER_MINUTE;
  }
  if (read_value(ITEM_BATTERY_VOLTAGE, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    data.battery.voltage = value;
  } else if (summary_voltage_is_battery_ && read_value(ITEM_SUMMARY_VOLTAGE, value) &&
             in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    data.battery.voltage = value;
  }

  return success;
}

bool EatonHidProtocol::read_power(UpsData &data) {
  bool success = false;
  float value;

  if (read_value(ITEM_INPUT_VOLTAGE, value) && in_range(value, 0.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    data.power.input_voltage = value;
    success = true;
  }
  if (read_value(ITEM_OUTPUT_VOLTAGE, value) && in_range(value, 0.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    data.power.output_voltage = value;
    success = true;
  }
  if ((read_value(ITEM_INPUT_FREQUENCY, value) || read_value(ITEM_OUTPUT_FREQUENCY, value)) &&
      in_range(value, 1.0f, MAX_PLAUSIBLE_FREQUENCY)) {
    data.power.frequency = value;
  }
  if (read_value(ITEM_LOAD, value) && in_range(value, 0.0f, MAX_LOAD_PERCENT)) {
    data.power.load_percent = value;
    success = true;
  }
  if (read_value(ITEM_TRANSFER_LOW, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    data.power.input_transfer_low = value;
  }
  if (read_value(ITEM_TRANSFER_HIGH, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    data.power.input_transfer_high = value;
  }

  return success;
}

bool EatonHidProtocol::read_status(UpsData &data) {
  bool ac_present = false, discharging = false, charging = false;
  bool battery_used = false, main_input_used = false, bypass = false, manual_bypass = false;
  const bool has_ac_present = read_flag(ITEM_AC_PRESENT, ac_present);
  const bool has_discharging = read_flag(ITEM_DISCHARGING, discharging);
  const bool has_charging = read_flag(ITEM_CHARGING, charging);
  const bool has_battery_used = read_flag(ITEM_BATTERY_USED, battery_used);
  const bool has_main_input_used = read_flag(ITEM_MAIN_INPUT_USED, main_input_used);
  read_flag(ITEM_BYPASS_USED, bypass);
  read_flag(ITEM_MANUAL_BYPASS_USED, manual_bypass);
  const bool on_bypass = bypass || manual_bypass;

  bool good = true;  // UPS.PowerSummary.PresentStatus.Good = 0 means the UPS output is off
  read_flag(ITEM_GOOD, good);
  const bool ups_off = !good;

  // On battery: Input.[3] (the battery input) is the most direct indicator on
  // line-interactive and online units, ACPresent on the others
  bool on_battery;
  if (has_battery_used) {
    on_battery = battery_used;
  } else if (has_ac_present) {
    on_battery = !ac_present || discharging;
  } else if (has_main_input_used && !ups_off) {
    on_battery = !main_input_used;
  } else {
    on_battery = discharging;
  }
  if (on_bypass) {
    on_battery = false;
  }

  // The component derives online/on battery from the input voltage
  if (on_battery) {
    if (data.power.input_voltage_valid()) {
      data.power.input_voltage = NAN;  // brownout reading would look like utility power
    }
  } else if (!data.power.input_voltage_valid()) {
    // Offline models (Ellipse, 3S, Protection Station) do not measure the input voltage
    data.power.input_voltage =
        !std::isnan(input_voltage_nominal_) ? input_voltage_nominal_ : parent_->get_fallback_nominal_voltage();
  }

  bool boost = false, buck = false, overload = false;
  bool internal_failure = false, over_temperature = false, fan_failure = false;
  read_flag(ITEM_BOOST, boost);
  read_flag(ITEM_BUCK, buck);
  read_flag(ITEM_OVERLOAD, overload);
  read_flag(ITEM_INTERNAL_FAILURE, internal_failure);
  read_flag(ITEM_OVER_TEMPERATURE, over_temperature);
  read_flag(ITEM_FAN_FAILURE, fan_failure);

  data.power.status = ups_off ? status::OFF : (on_battery ? status::ON_BATTERY : status::ONLINE);
  if (on_bypass) {
    data.power.status += " - Bypass";
  } else if (boost) {
    data.power.status += " - Boost";
  } else if (buck) {
    data.power.status += " - Trim";
  }
  if (overload) {
    data.power.status += " - Overload";
  }
  if (internal_failure) {
    data.power.status += " - Internal Failure";
  }
  if (over_temperature) {
    data.power.status += " - Over Temperature";
  }
  if (fan_failure) {
    data.power.status += " - Fan Failure";
  }

  // Charging state. With Advanced Battery Monitoring the PresentStatus flags are
  // not meaningful and the charger reports its own state (NUT eaton_abm_*).
  ChargerState charger = ChargerState::UNKNOWN;
  bool abm_enabled = false;
  int64_t raw;
  if (read_flag(ITEM_ABM_ENABLED, abm_enabled) && abm_enabled) {
    if (read_raw(ITEM_CHARGER_MODE, raw)) {
      charger = charger_state_from_mode(raw);
    } else if (read_raw(ITEM_CHARGER_STATUS, raw)) {
      charger = charger_state_from_status(raw);
    }
  }
  if (charger != ChargerState::UNKNOWN) {
    charging = charger == ChargerState::CHARGING || charger == ChargerState::FLOATING;
    discharging = charger == ChargerState::DISCHARGING;
  } else if (has_charging && charging && data.battery.level >= battery::MAX_LEVEL_PERCENT) {
    // Constant-charge units (e.g. Eaton 5E) keep Charging set while floating at 100%
    charging = false;
  }

  bool battery_present = true;
  read_flag(ITEM_BATTERY_PRESENT, battery_present);
  if (!battery_present) {
    data.battery.status = battery_status::NOT_PRESENT;
  } else if (on_battery || discharging) {
    data.battery.status = battery_status::DISCHARGING;
  } else if (charger == ChargerState::FLOATING) {
    data.battery.status = battery_status::FLOATING;
  } else if (charging) {
    data.battery.status = battery_status::CHARGING;
  } else if (charger == ChargerState::RESTING) {
    data.battery.status = battery_status::RESTING;
  } else if (data.battery.level >= battery::MAX_LEVEL_PERCENT) {
    data.battery.status = battery_status::FULLY_CHARGED;
  } else {
    data.battery.status = battery_status::NORMAL;
  }

  bool need_replacement = false, below_capacity_limit = false, shutdown_imminent = false;
  read_flag(ITEM_NEED_REPLACEMENT, need_replacement);
  read_flag(ITEM_BELOW_CAPACITY_LIMIT, below_capacity_limit);
  read_flag(ITEM_SHUTDOWN_IMMINENT, shutdown_imminent);
  if (need_replacement) {
    data.battery.status += battery_status::REPLACE_BATTERY_SUFFIX;
  }
  if (shutdown_imminent) {
    data.battery.status += battery_status::SHUTDOWN_IMMINENT_SUFFIX;
  }

  // Low battery is reported by comparing the level with charge_low: raise the
  // threshold to the current level when the UPS itself signals low battery
  if ((below_capacity_limit || shutdown_imminent) && !std::isnan(data.battery.level) &&
      (std::isnan(data.battery.charge_low) || data.battery.level > data.battery.charge_low)) {
    data.battery.charge_low = data.battery.level;
  }

  ESP_LOGD(EATON_TAG, "Status: %s | Battery: %s", data.power.status.c_str(), data.battery.status.c_str());
  return has_ac_present || has_battery_used || has_main_input_used || has_discharging;
}

void EatonHidProtocol::read_settings(UpsData &data) {
  int64_t raw;

  if (read_raw(ITEM_BEEPER, raw)) {
    switch (raw) {
      case beeper::CONTROL_DISABLE:
        data.config.beeper_status = "disabled";
        data.config.beeper_state = ConfigData::BEEPER_DISABLED;
        break;
      case beeper::CONTROL_ENABLE:
        data.config.beeper_status = "enabled";
        data.config.beeper_state = ConfigData::BEEPER_ENABLED;
        break;
      case beeper::CONTROL_MUTE:
        data.config.beeper_status = "muted";
        data.config.beeper_state = ConfigData::BEEPER_MUTED;
        break;
      default:
        break;
    }
  }

  if (read_raw(ITEM_BATTERY_TEST, raw)) {
    const char *result = test_result_text(raw);
    if (result != nullptr) {
      data.test.ups_test_result = result;
    }
  }

  // UPS.PowerConverter.Output.SensitivityMode (NUT mge_sensitivity_info)
  if (read_raw(ITEM_SENSITIVITY, raw)) {
    switch (raw) {
      case 0:
        data.config.input_sensitivity = sensitivity::NORMAL;
        data.config.sensitivity_level = ConfigData::SENSITIVITY_MEDIUM;
        break;
      case 1:
        data.config.input_sensitivity = sensitivity::HIGH;
        data.config.sensitivity_level = ConfigData::SENSITIVITY_HIGH;
        break;
      case 2:
        data.config.input_sensitivity = sensitivity::LOW;
        data.config.sensitivity_level = ConfigData::SENSITIVITY_LOW;
        break;
      default:
        break;
    }
  }
}

bool EatonHidProtocol::read_timers(UpsData &data) {
  // -1 when no countdown is running
  bool success = false;
  float seconds;
  if (read_value(ITEM_TIMER_SHUTDOWN, seconds)) {
    data.test.timer_shutdown = to_timer(seconds);
    success = true;
  }
  if (read_value(ITEM_TIMER_START, seconds)) {
    data.test.timer_start = to_timer(seconds);
    success = true;
  }
  if (read_value(ITEM_TIMER_REBOOT, seconds)) {
    data.test.timer_reboot = to_timer(seconds);
    success = true;
  }
  return success;
}

bool EatonHidProtocol::read_timer_data(UpsData &data) {
  report_cache_.clear();
  return read_timers(data);
}

bool EatonHidProtocol::beeper_enable() { return write_value(ITEM_BEEPER, beeper::CONTROL_ENABLE, "Beeper enable"); }

bool EatonHidProtocol::beeper_disable() { return write_value(ITEM_BEEPER, beeper::CONTROL_DISABLE, "Beeper disable"); }

bool EatonHidProtocol::beeper_mute() { return write_value(ITEM_BEEPER, beeper::CONTROL_MUTE, "Beeper mute"); }

bool EatonHidProtocol::start_battery_test_quick() {
  return write_value(ITEM_BATTERY_TEST, test::COMMAND_QUICK, "Quick battery test");
}

bool EatonHidProtocol::start_battery_test_deep() {
  return write_value(ITEM_BATTERY_TEST, test::COMMAND_DEEP, "Deep battery test");
}

bool EatonHidProtocol::stop_battery_test() {
  return write_value(ITEM_BATTERY_TEST, test::COMMAND_ABORT, "Stop battery test");
}

std::unique_ptr<UpsProtocolBase> create_eaton_protocol(UpsHidComponent *parent) {
  return std::make_unique<EatonHidProtocol>(parent);
}

}  // namespace ups_hid
}  // namespace esphome

// Register Eaton protocol for vendor ID 0x0463 (Eaton, formerly MGE Office Protection Systems)
REGISTER_UPS_PROTOCOL_FOR_VENDOR(0x0463, eaton_hid_protocol, esphome::ups_hid::create_eaton_protocol, "Eaton HID Protocol", "Eaton/MGE HID Power Device protocol, located through the HID report descriptor (NUT mge-hid compatible)", 100);
