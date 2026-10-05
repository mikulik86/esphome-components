#include "protocol_apc.h"
#include "constants_hid.h"
#include "constants_ups.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstdio>

namespace esphome {
namespace ups_hid {

static const char *const APC_HID_TAG = "ups_hid.apc_hid";

namespace {

using Item = ApcHidProtocol::Item;
using hid_pdc::in_range;
using hid_pdc::to_timer;

// Usage path nodes (page << 16 | usage id)
constexpr uint32_t U_UPS = HID_USAGE_POW(HID_USAGE_POW_UPS);
constexpr uint32_t U_PRESENT_STATUS = HID_USAGE_POW(HID_USAGE_POW_PRESENT_STATUS);
constexpr uint32_t U_BATTERY_SYSTEM = HID_USAGE_POW(HID_USAGE_POW_BATTERY_SYSTEM);
constexpr uint32_t U_BATTERY = HID_USAGE_POW(HID_USAGE_POW_BATTERY);
constexpr uint32_t U_POWER_CONVERTER = HID_USAGE_POW(HID_USAGE_POW_POWER_CONVERTER);
constexpr uint32_t U_INPUT = HID_USAGE_POW(HID_USAGE_POW_INPUT);
constexpr uint32_t U_OUTPUT = HID_USAGE_POW(HID_USAGE_POW_OUTPUT);
constexpr uint32_t U_POWER_SUMMARY = HID_USAGE_POW(HID_USAGE_POW_POWER_SUMMARY);
constexpr uint32_t U_VOLTAGE = HID_USAGE_POW(HID_USAGE_POW_VOLTAGE);
constexpr uint32_t U_FREQUENCY = HID_USAGE_POW(HID_USAGE_POW_FREQUENCY);
constexpr uint32_t U_PERCENT_LOAD = HID_USAGE_POW(HID_USAGE_POW_PERCENT_LOAD);
constexpr uint32_t U_CONFIG_VOLTAGE = HID_USAGE_POW(HID_USAGE_POW_CONFIG_VOLTAGE);
constexpr uint32_t U_CONFIG_ACTIVE_POWER = HID_USAGE_POW(HID_USAGE_POW_CONFIG_ACTIVE_POWER);
constexpr uint32_t U_LOW_VOLTAGE_TRANSFER = HID_USAGE_POW(HID_USAGE_POW_LOW_VOLTAGE_TRANSFER);
constexpr uint32_t U_HIGH_VOLTAGE_TRANSFER = HID_USAGE_POW(HID_USAGE_POW_HIGH_VOLTAGE_TRANSFER);
constexpr uint32_t U_DELAY_BEFORE_REBOOT = HID_USAGE_POW(HID_USAGE_POW_DELAY_BEFORE_REBOOT);
constexpr uint32_t U_DELAY_BEFORE_STARTUP = HID_USAGE_POW(HID_USAGE_POW_DELAY_BEFORE_STARTUP);
constexpr uint32_t U_DELAY_BEFORE_SHUTDOWN = HID_USAGE_POW(HID_USAGE_POW_DELAY_BEFORE_SHUTDOWN);
constexpr uint32_t U_TEST = HID_USAGE_POW(HID_USAGE_POW_TEST);
constexpr uint32_t U_AUDIBLE_ALARM_CONTROL = HID_USAGE_POW(HID_USAGE_POW_AUDIBLE_ALARM_CONTROL);
constexpr uint32_t U_OVERLOAD = HID_USAGE_POW(HID_USAGE_POW_OVERLOAD);
constexpr uint32_t U_SHUTDOWN_IMMINENT = HID_USAGE_POW(HID_USAGE_POW_SHUTDOWN_IMMINENT);
constexpr uint32_t U_REMAINING_CAPACITY = HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_CAPACITY);
constexpr uint32_t U_REMAINING_CAPACITY_LIMIT = HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_CAPACITY_LIMIT);
constexpr uint32_t U_WARNING_CAPACITY_LIMIT = HID_USAGE_BAT(HID_USAGE_BAT_WARNING_CAPACITY_LIMIT);
constexpr uint32_t U_RUN_TIME_TO_EMPTY = HID_USAGE_BAT(HID_USAGE_BAT_RUN_TIME_TO_EMPTY);
constexpr uint32_t U_REMAINING_TIME_LIMIT = HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_TIME_LIMIT);
constexpr uint32_t U_I_DEVICE_CHEMISTRY = HID_USAGE_BAT(HID_USAGE_BAT_I_DEVICE_CHEMISTRY);
constexpr uint32_t U_MANUFACTURER_DATE = HID_USAGE_BAT(HID_USAGE_BAT_MANUFACTURER_DATE);
constexpr uint32_t U_AC_PRESENT = HID_USAGE_BAT(HID_USAGE_BAT_AC_PRESENT);
constexpr uint32_t U_CHARGING = HID_USAGE_BAT(HID_USAGE_BAT_CHARGING);
constexpr uint32_t U_DISCHARGING = HID_USAGE_BAT(HID_USAGE_BAT_DISCHARGING);
constexpr uint32_t U_BELOW_REMAINING_CAPACITY_LIMIT = HID_USAGE_BAT(HID_USAGE_BAT_BELOW_REMAINING_CAPACITY_LIMIT);
constexpr uint32_t U_REMAINING_TIME_LIMIT_EXPIRED = HID_USAGE_BAT(HID_USAGE_BAT_REMAINING_TIME_LIMIT_EXPIRED);
constexpr uint32_t U_NEED_REPLACEMENT = HID_USAGE_BAT(HID_USAGE_BAT_NEED_REPLACEMENT);
constexpr uint32_t U_BATTERY_PRESENT = HID_USAGE_BAT(HID_USAGE_BAT_BATTERY_PRESENT);

// APC vendor usage page 0xFF86 (names from NUT apc-hid.c)
constexpr uint32_t APC_GENERAL_COLLECTION = 0xFF860005;
constexpr uint32_t APC_BATT_REPLACE_DATE = 0xFF860016;
constexpr uint32_t APC_STATUS_FLAG = 0xFF860060;
constexpr uint32_t APC_SENSITIVITY = 0xFF860061;
constexpr uint32_t APC_PANEL_TEST = 0xFF860072;
constexpr uint32_t APC_DELAY_BEFORE_REBOOT = 0xFF86007C;
constexpr uint32_t APC_DELAY_BEFORE_SHUTDOWN = 0xFF86007D;
constexpr uint32_t APC_DELAY_BEFORE_STARTUP = 0xFF86007E;

// Paths as used by NUT apc-hid.c. When several paths feed one item, the first one
// present in the descriptor is used.
const HidPathDefinition APC_PATHS[] = {
    {Item::ITEM_BATTERY_TYPE, "battery.type", 3, {U_UPS, U_POWER_SUMMARY, U_I_DEVICE_CHEMISTRY}},
    {Item::ITEM_BATTERY_MFR_DATE, "battery.mfr.date", 3, {U_UPS, U_BATTERY, U_MANUFACTURER_DATE}},
    {Item::ITEM_BATTERY_REPLACE_DATE, "battery.mfr.date", 3, {U_UPS, U_POWER_SUMMARY, APC_BATT_REPLACE_DATE}},
    {Item::ITEM_UPS_MFR_DATE, "ups.mfr.date", 2, {U_UPS, U_MANUFACTURER_DATE}},
    {Item::ITEM_UPS_MFR_DATE, "ups.mfr.date", 3, {U_UPS, U_POWER_SUMMARY, U_MANUFACTURER_DATE}},
    {Item::ITEM_BATTERY_VOLTAGE_NOMINAL, "battery.voltage.nominal", 3, {U_UPS, U_BATTERY, U_CONFIG_VOLTAGE}},
    {Item::ITEM_BATTERY_VOLTAGE_NOMINAL, "battery.voltage.nominal", 3, {U_UPS, U_POWER_SUMMARY, U_CONFIG_VOLTAGE}},
    {Item::ITEM_INPUT_VOLTAGE_NOMINAL, "input.voltage.nominal", 3, {U_UPS, U_INPUT, U_CONFIG_VOLTAGE}},
    {Item::ITEM_OUTPUT_VOLTAGE_NOMINAL, "output.voltage.nominal", 3, {U_UPS, U_OUTPUT, U_CONFIG_VOLTAGE}},
    {Item::ITEM_ACTIVE_POWER_NOMINAL, "ups.realpower.nominal", 3, {U_UPS, U_POWER_CONVERTER, U_CONFIG_ACTIVE_POWER}},
    {Item::ITEM_ACTIVE_POWER_NOMINAL, "ups.realpower.nominal", 3, {U_UPS, U_OUTPUT, U_CONFIG_ACTIVE_POWER}},

    {Item::ITEM_BATTERY_CHARGE, "battery.charge", 3, {U_UPS, U_POWER_SUMMARY, U_REMAINING_CAPACITY}},
    {Item::ITEM_BATTERY_CHARGE_LOW, "battery.charge.low", 3, {U_UPS, U_POWER_SUMMARY, U_REMAINING_CAPACITY_LIMIT}},
    {Item::ITEM_BATTERY_CHARGE_WARNING, "battery.charge.warning", 3, {U_UPS, U_POWER_SUMMARY, U_WARNING_CAPACITY_LIMIT}},
    {Item::ITEM_BATTERY_RUNTIME, "battery.runtime", 3, {U_UPS, U_BATTERY, U_RUN_TIME_TO_EMPTY}},
    {Item::ITEM_BATTERY_RUNTIME, "battery.runtime", 3, {U_UPS, U_POWER_SUMMARY, U_RUN_TIME_TO_EMPTY}},
    {Item::ITEM_BATTERY_RUNTIME_LOW, "battery.runtime.low", 3, {U_UPS, U_BATTERY, U_REMAINING_TIME_LIMIT}},
    {Item::ITEM_BATTERY_RUNTIME_LOW, "battery.runtime.low", 3, {U_UPS, U_POWER_SUMMARY, U_REMAINING_TIME_LIMIT}},
    {Item::ITEM_BATTERY_VOLTAGE, "battery.voltage", 3, {U_UPS, U_BATTERY, U_VOLTAGE}},
    {Item::ITEM_BATTERY_VOLTAGE, "battery.voltage", 3, {U_UPS, U_POWER_SUMMARY, U_VOLTAGE}},

    {Item::ITEM_INPUT_VOLTAGE, "input.voltage", 3, {U_UPS, U_INPUT, U_VOLTAGE}},
    {Item::ITEM_TRANSFER_LOW, "input.transfer.low", 3, {U_UPS, U_OUTPUT, U_LOW_VOLTAGE_TRANSFER}},
    {Item::ITEM_TRANSFER_LOW, "input.transfer.low", 3, {U_UPS, U_INPUT, U_LOW_VOLTAGE_TRANSFER}},
    {Item::ITEM_TRANSFER_HIGH, "input.transfer.high", 3, {U_UPS, U_OUTPUT, U_HIGH_VOLTAGE_TRANSFER}},
    {Item::ITEM_TRANSFER_HIGH, "input.transfer.high", 3, {U_UPS, U_INPUT, U_HIGH_VOLTAGE_TRANSFER}},
    {Item::ITEM_SENSITIVITY, "input.sensitivity", 3, {U_UPS, U_INPUT, APC_SENSITIVITY}},
    {Item::ITEM_OUTPUT_VOLTAGE, "output.voltage", 3, {U_UPS, U_OUTPUT, U_VOLTAGE}},
    {Item::ITEM_OUTPUT_FREQUENCY, "output.frequency", 3, {U_UPS, U_OUTPUT, U_FREQUENCY}},
    {Item::ITEM_LOAD, "ups.load", 3, {U_UPS, U_OUTPUT, U_PERCENT_LOAD}},
    {Item::ITEM_LOAD, "ups.load", 3, {U_UPS, U_POWER_CONVERTER, U_PERCENT_LOAD}},
    {Item::ITEM_BEEPER, "ups.beeper.status", 3, {U_UPS, U_POWER_SUMMARY, U_AUDIBLE_ALARM_CONTROL}},

    {Item::ITEM_AC_PRESENT, "ups.status OL", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_AC_PRESENT}},
    {Item::ITEM_AC_PRESENT, "ups.status OL", 3, {U_UPS, U_POWER_SUMMARY, U_AC_PRESENT}},  // Back-UPS 500
    {Item::ITEM_CHARGING, "ups.status CHRG", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_CHARGING}},
    {Item::ITEM_CHARGING, "ups.status CHRG", 3, {U_UPS, U_POWER_SUMMARY, U_CHARGING}},
    {Item::ITEM_DISCHARGING, "ups.status DISCHRG", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_DISCHARGING}},
    {Item::ITEM_DISCHARGING, "ups.status DISCHRG", 3, {U_UPS, U_POWER_SUMMARY, U_DISCHARGING}},
    {Item::ITEM_BELOW_CAPACITY_LIMIT, "ups.status LB", 4,
     {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_BELOW_REMAINING_CAPACITY_LIMIT}},
    {Item::ITEM_BELOW_CAPACITY_LIMIT, "ups.status LB", 3, {U_UPS, U_POWER_SUMMARY, U_BELOW_REMAINING_CAPACITY_LIMIT}},
    {Item::ITEM_SHUTDOWN_IMMINENT, "ups.status LB", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_SHUTDOWN_IMMINENT}},
    {Item::ITEM_SHUTDOWN_IMMINENT, "ups.status LB", 3, {U_UPS, U_POWER_SUMMARY, U_SHUTDOWN_IMMINENT}},
    {Item::ITEM_TIME_LIMIT_EXPIRED, "ups.status LB", 4,
     {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_REMAINING_TIME_LIMIT_EXPIRED}},
    {Item::ITEM_OVERLOAD, "ups.status OVER", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_OVERLOAD}},
    {Item::ITEM_NEED_REPLACEMENT, "ups.status RB", 4, {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_NEED_REPLACEMENT}},
    {Item::ITEM_BATTERY_PRESENT, "ups.status (battery present)", 4,
     {U_UPS, U_POWER_SUMMARY, U_PRESENT_STATUS, U_BATTERY_PRESENT}},
    {Item::ITEM_STATUS_FLAG, "ups.status OFF", 3, {U_UPS, U_POWER_SUMMARY, APC_STATUS_FLAG}},  // Back-UPS LS

    {Item::ITEM_BATTERY_TEST, "ups.test.result", 4, {U_UPS, U_BATTERY_SYSTEM, U_BATTERY, U_TEST}},
    {Item::ITEM_BATTERY_TEST, "ups.test.result", 3, {U_UPS, U_BATTERY, U_TEST}},
    {Item::ITEM_PANEL_TEST, "test.panel", 2, {U_UPS, APC_PANEL_TEST}},
    {Item::ITEM_PANEL_TEST, "test.panel", 3, {U_UPS, U_POWER_SUMMARY, APC_PANEL_TEST}},  // Back-UPS 500
    {Item::ITEM_TIMER_SHUTDOWN, "ups.timer.shutdown", 3, {U_UPS, U_POWER_SUMMARY, U_DELAY_BEFORE_SHUTDOWN}},
    {Item::ITEM_TIMER_SHUTDOWN, "ups.timer.shutdown", 3, {U_UPS, U_OUTPUT, U_DELAY_BEFORE_SHUTDOWN}},
    {Item::ITEM_TIMER_SHUTDOWN, "ups.timer.shutdown", 3, {U_UPS, APC_GENERAL_COLLECTION, APC_DELAY_BEFORE_SHUTDOWN}},
    {Item::ITEM_TIMER_START, "ups.timer.start", 3, {U_UPS, U_POWER_SUMMARY, U_DELAY_BEFORE_STARTUP}},
    {Item::ITEM_TIMER_START, "ups.timer.start", 3, {U_UPS, U_OUTPUT, U_DELAY_BEFORE_STARTUP}},
    {Item::ITEM_TIMER_START, "ups.timer.start", 3, {U_UPS, APC_GENERAL_COLLECTION, APC_DELAY_BEFORE_STARTUP}},
    {Item::ITEM_TIMER_REBOOT, "ups.timer.reboot", 3, {U_UPS, U_POWER_SUMMARY, U_DELAY_BEFORE_REBOOT}},
    {Item::ITEM_TIMER_REBOOT, "ups.timer.reboot", 3, {U_UPS, U_OUTPUT, U_DELAY_BEFORE_REBOOT}},
    {Item::ITEM_TIMER_REBOOT, "ups.timer.reboot", 3, {U_UPS, APC_GENERAL_COLLECTION, APC_DELAY_BEFORE_REBOOT}},
};
constexpr size_t PATH_COUNT = sizeof(APC_PATHS) / sizeof(APC_PATHS[0]);
static_assert(PATH_COUNT < 0xFF, "path index must fit HidDataPoints ranks");

// NUT apc_fix_report_desc() applies to product ID 0x0002 with these report IDs
constexpr uint16_t APC_FIX_PRODUCT_ID = 0x0002;
constexpr uint8_t APC_FIX_CONFIG_VOLTAGE_REPORT = 0x30;
constexpr uint8_t APC_FIX_VOLTAGE_REPORT = 0x31;
constexpr uint8_t APC_FIX_HIGH_TRANSFER_REPORT = 0x33;
constexpr int64_t APC_FIX_CONFIG_VOLTAGE_MAX = 255;

constexpr const char *APC_MANUFACTURER = "APC";
constexpr float SECONDS_PER_MINUTE = 60.0f;
constexpr float MAX_PLAUSIBLE_VOLTAGE = 500.0f;
constexpr float MAX_PLAUSIBLE_FREQUENCY = 100.0f;
constexpr float MAX_LOAD_PERCENT = 200.0f;  // overloaded units report above 100%

// APC panel test commands (NUT test.panel.start / test.panel.stop)
constexpr float PANEL_TEST_START = 1.0f;
constexpr float PANEL_TEST_STOP = 0.0f;

std::string trim(const std::string &text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// NUT apc_format_model(): "Back-UPS ES 700G FW:871.O2 .I USB FW:O2" becomes model
// "Back-UPS ES 700G", firmware "871.O2 .I" and auxiliary firmware "O2"
void split_product(const std::string &product, std::string &model, std::string &firmware, std::string &aux) {
  model = product;
  firmware.clear();
  aux.clear();
  const size_t fw = product.find("FW:");
  if (fw == std::string::npos) {
    return;
  }
  model = trim(product.substr(0, fw));
  std::string rest = product.substr(fw + 3);
  const size_t usb_fw = rest.find("USB FW:");
  if (usb_fw != std::string::npos) {
    aux = trim(rest.substr(usb_fw + 7));
    rest = rest.substr(0, usb_fw);
  }
  firmware = trim(rest);
}

// USB HID date: year since 1980 in bits 15-9, month in bits 8-5, day in bits 4-0
// (NUT date_conversion)
std::string hid_date(int64_t value) {
  if (value <= 0) {
    return "";
  }
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%04d/%02d/%02d", static_cast<int>(1980 + (value >> 9)),
           static_cast<int>((value >> 5) & 0x0F), static_cast<int>(value & 0x1F));
  return buffer;
}

// APC date: hexadecimal digits read as decimal, 0x102202 = 2002/10/22 (NUT apc_date_conversion)
std::string apc_date(int64_t value) {
  if (value <= 0) {
    return "";
  }
  int year = static_cast<int>((value & 0x0F) + 10 * ((value >> 4) & 0x0F));
  const int month = static_cast<int>(((value >> 16) & 0x0F) + 10 * ((value >> 20) & 0x0F));
  const int day = static_cast<int>(((value >> 8) & 0x0F) + 10 * ((value >> 12) & 0x0F));
  year += year >= 70 ? 1900 : 2000;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%04d/%02d/%02d", year, month, day);
  return buffer;
}

}  // namespace

ApcHidProtocol::ApcHidProtocol(UpsHidComponent *parent)
    : UpsProtocolBase(parent), points_(parent, APC_HID_TAG, APC_PATHS, PATH_COUNT, ITEM_COUNT) {}

bool ApcHidProtocol::detect() {
  ESP_LOGD(APC_HID_TAG, "Detecting APC HID protocol...");

  if (!parent_->is_device_connected()) {
    ESP_LOGD(APC_HID_TAG, "Device not connected, skipping protocol detection");
    return false;
  }

  // Give the device time to settle after enumeration
  vTaskDelay(pdMS_TO_TICKS(timing::USB_INITIALIZATION_DELAY_MS));

  if (!load_report_descriptor()) {
    return false;
  }

  if (!has(ITEM_BATTERY_CHARGE) && !has(ITEM_AC_PRESENT)) {
    ESP_LOGW(APC_HID_TAG, "Report descriptor has no UPS.PowerSummary battery or status data - not an APC HID UPS");
    return false;
  }

  // Make sure the UPS answers the reports the descriptor promises
  points_.begin_poll();
  const Item probe = has(ITEM_BATTERY_CHARGE) ? ITEM_BATTERY_CHARGE : ITEM_AC_PRESENT;
  int64_t value;
  if (!read_raw(probe, value)) {
    ESP_LOGW(APC_HID_TAG, "UPS did not answer HID report 0x%02X", points_.field(probe).report_id);
    return false;
  }

  ESP_LOGI(APC_HID_TAG, "APC HID protocol detected");
  return true;
}

bool ApcHidProtocol::initialize() {
  ESP_LOGD(APC_HID_TAG, "Initializing APC HID protocol...");

  // Manual protocol selection (protocol: apc) skips detect(), which loads the descriptor
  if (!points_.loaded() && !load_report_descriptor()) {
    ESP_LOGE(APC_HID_TAG, "Cannot initialize without the HID report descriptor");
    return false;
  }

  read_device_info();
  ESP_LOGI(APC_HID_TAG, "APC HID protocol initialized: %s %s (serial: %s, firmware: %s)", manufacturer_.c_str(),
           model_.empty() ? "UPS" : model_.c_str(), serial_number_.empty() ? "unknown" : serial_number_.c_str(),
           firmware_version_.empty() ? "unknown" : firmware_version_.c_str());
  points_.log_data_points([](uint8_t) { return true; });
  return true;
}

bool ApcHidProtocol::load_report_descriptor() {
  esp_err_t err = points_.load(0);
  if (err != ESP_OK) {
    ESP_LOGW(APC_HID_TAG, "Cannot read the HID report descriptor: %s", esp_err_to_name(err));
    return false;
  }
  fix_report_descriptor();
  return true;
}

void ApcHidProtocol::fix_report_descriptor() {
  // NUT apc_fix_report_desc(): some product ID 0x0002 units (e.g. Back-UPS XS 1400U on
  // 220-240 V) declare input voltage ranges below their high transfer voltage, which
  // would clamp the readings. Widen them the same way NUT does.
  if (parent_->get_vendor_id() != usb::VENDOR_ID_APC || parent_->get_product_id() != APC_FIX_PRODUCT_ID ||
      !has(ITEM_TRANSFER_HIGH) || points_.field(ITEM_TRANSFER_HIGH).report_id != APC_FIX_HIGH_TRANSFER_REPORT) {
    return;
  }
  const int64_t transfer_max = points_.field(ITEM_TRANSFER_HIGH).logical_max;

  if (has(ITEM_INPUT_VOLTAGE)) {
    HidField &voltage = points_.field(ITEM_INPUT_VOLTAGE);
    if (voltage.report_id == APC_FIX_VOLTAGE_REPORT && transfer_max > voltage.logical_max) {
      voltage.logical_min = 0;
      voltage.logical_max = transfer_max * 2;
      ESP_LOGD(APC_HID_TAG, "Fixed input voltage logical range: 0..%lld", static_cast<long long>(voltage.logical_max));
    }
  }
  if (has(ITEM_INPUT_VOLTAGE_NOMINAL)) {
    HidField &nominal = points_.field(ITEM_INPUT_VOLTAGE_NOMINAL);
    if (nominal.report_id == APC_FIX_CONFIG_VOLTAGE_REPORT && transfer_max > nominal.logical_max &&
        nominal.logical_max != APC_FIX_CONFIG_VOLTAGE_MAX) {
      nominal.logical_max = APC_FIX_CONFIG_VOLTAGE_MAX;
      ESP_LOGD(APC_HID_TAG, "Fixed nominal input voltage logical maximum: %lld",
               static_cast<long long>(nominal.logical_max));
    }
  }
}

bool ApcHidProtocol::read_device_string(UsbDeviceString which, std::string &value) {
  const uint8_t index = parent_->get_device_string_index(which);
  if (index == 0) {
    return false;
  }
  std::string text;
  if (parent_->get_string_descriptor(index, text) != ESP_OK) {
    return false;
  }
  text = trim(text);
  if (text.empty()) {
    return false;
  }
  value = text;
  return true;
}

void ApcHidProtocol::read_device_info() {
  points_.begin_poll();

  // Manufacturer, model and serial number from the USB device strings (NUT apc_format_mfr,
  // apc_format_model, apc_format_serial)
  if (!read_device_string(UsbDeviceString::MANUFACTURER, manufacturer_)) {
    manufacturer_ = APC_MANUFACTURER;
  }
  std::string product;
  if (read_device_string(UsbDeviceString::PRODUCT, product)) {
    split_product(product, model_, firmware_version_, firmware_aux_);
  }
  read_device_string(UsbDeviceString::SERIAL_NUMBER, serial_number_);
  points_.read_string(ITEM_BATTERY_TYPE, battery_type_);

  int64_t raw;
  if (read_raw(ITEM_BATTERY_MFR_DATE, raw)) {
    battery_mfr_date_ = hid_date(raw);
  }
  if (battery_mfr_date_.empty() && read_raw(ITEM_BATTERY_REPLACE_DATE, raw)) {
    battery_mfr_date_ = apc_date(raw);
  }
  if (read_raw(ITEM_UPS_MFR_DATE, raw)) {
    ups_mfr_date_ = hid_date(raw);
  }

  float value;
  if (read_value(ITEM_BATTERY_VOLTAGE_NOMINAL, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    battery_voltage_nominal_ = value;
  }
  if (read_value(ITEM_INPUT_VOLTAGE_NOMINAL, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    input_voltage_nominal_ = value;
  }
  if (read_value(ITEM_OUTPUT_VOLTAGE_NOMINAL, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    output_voltage_nominal_ = value;
  }
  if (read_value(ITEM_ACTIVE_POWER_NOMINAL, value) && value > 0) {
    active_power_nominal_ = value;
  }
}

bool ApcHidProtocol::read_data(UpsData &data) {
  points_.begin_poll();

  bool success = read_battery(data);
  success |= read_power(data);
  success |= read_status(data);  // after battery: uses the charge level
  read_settings(data);
  read_timers(data);

  data.device.manufacturer = manufacturer_;
  data.device.model = model_;
  data.device.serial_number = serial_number_;
  data.device.firmware_version = firmware_version_;
  data.device.firmware_aux = firmware_aux_;
  data.device.mfr_date = ups_mfr_date_;
  data.device.usb_vendor_id = parent_->get_vendor_id();
  data.device.usb_product_id = parent_->get_product_id();
  data.battery.type = battery_type_;
  data.battery.mfr_date = battery_mfr_date_;
  data.battery.voltage_nominal = battery_voltage_nominal_;
  data.power.input_voltage_nominal = input_voltage_nominal_;
  data.power.output_voltage_nominal = output_voltage_nominal_;
  data.power.realpower_nominal = active_power_nominal_;

  return success;
}

bool ApcHidProtocol::read_battery(UpsData &data) {
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
  if (read_value(ITEM_BATTERY_CHARGE_WARNING, value) && in_range(value, 0.0f, battery::MAX_LEVEL_PERCENT)) {
    data.battery.charge_warning = value;
  }
  if (read_value(ITEM_BATTERY_RUNTIME_LOW, value) && value >= 0) {
    data.battery.runtime_low = value / SECONDS_PER_MINUTE;
  }
  if (read_value(ITEM_BATTERY_VOLTAGE, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_VOLTAGE)) {
    data.battery.voltage = value;
  }

  return success;
}

bool ApcHidProtocol::read_power(UpsData &data) {
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
  if (read_value(ITEM_OUTPUT_FREQUENCY, value) && in_range(value, 1.0f, MAX_PLAUSIBLE_FREQUENCY)) {
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

bool ApcHidProtocol::read_status(UpsData &data) {
  bool ac_present = false, discharging = false, charging = false;
  const bool has_ac_present = read_flag(ITEM_AC_PRESENT, ac_present);
  const bool has_discharging = read_flag(ITEM_DISCHARGING, discharging);
  const bool has_charging = read_flag(ITEM_CHARGING, charging);

  // APCStatusFlag (Back-UPS LS) is 0 while the UPS output is off (NUT apcstatusflag_info)
  int64_t status_flag;
  const bool ups_off = read_raw(ITEM_STATUS_FLAG, status_flag) && status_flag == 0;

  // Online / on battery from the UPS's own flags; the input voltage stays as measured
  const bool on_battery = has_ac_present ? (!ac_present || discharging) : discharging;
  data.power.on_battery_reported = has_ac_present || has_discharging;
  data.power.on_battery = on_battery;

  bool overload = false;
  data.power.overload_reported = read_flag(ITEM_OVERLOAD, overload);
  data.power.overload = overload;

  data.power.status = ups_off ? status::OFF : (on_battery ? status::ON_BATTERY : status::ONLINE);
  if (overload) {
    data.power.status += " - Overload";
  }

  bool battery_present = true;
  read_flag(ITEM_BATTERY_PRESENT, battery_present);
  if (!battery_present) {
    data.battery.status = battery_status::NOT_PRESENT;
  } else if (on_battery || discharging) {
    data.battery.status = battery_status::DISCHARGING;
  } else if (charging) {
    data.battery.status = battery_status::CHARGING;
  } else if (data.battery.level >= battery::MAX_LEVEL_PERCENT) {
    data.battery.status = battery_status::FULLY_CHARGED;
  } else {
    data.battery.status = battery_status::NORMAL;
  }

  bool need_replacement = false, below_capacity_limit = false, shutdown_imminent = false, time_limit_expired = false;
  read_flag(ITEM_NEED_REPLACEMENT, need_replacement);
  read_flag(ITEM_BELOW_CAPACITY_LIMIT, below_capacity_limit);
  read_flag(ITEM_SHUTDOWN_IMMINENT, shutdown_imminent);
  read_flag(ITEM_TIME_LIMIT_EXPIRED, time_limit_expired);
  data.battery.needs_replacement = need_replacement;
  if (need_replacement) {
    data.battery.status += battery_status::REPLACE_BATTERY_SUFFIX;
  }
  if (shutdown_imminent) {
    data.battery.status += battery_status::SHUTDOWN_IMMINENT_SUFFIX;
  } else if (time_limit_expired) {
    data.battery.status += battery_status::TIME_LIMIT_EXPIRED_SUFFIX;
  }

  // Low battery is reported by comparing the level with charge_low: raise the
  // threshold to the current level when the UPS itself signals low battery (NUT LB)
  const bool low_battery = below_capacity_limit || shutdown_imminent || time_limit_expired;
  if (low_battery && !std::isnan(data.battery.level) &&
      (std::isnan(data.battery.charge_low) || data.battery.level > data.battery.charge_low)) {
    data.battery.charge_low = data.battery.level;
  }

  ESP_LOGD(APC_HID_TAG, "Status: %s | Battery: %s", data.power.status.c_str(), data.battery.status.c_str());
  return has_ac_present || has_discharging || has_charging;
}

void ApcHidProtocol::read_settings(UpsData &data) {
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
    const char *result = hid_pdc::test_result_text(raw);
    if (result != nullptr) {
      data.test.ups_test_result = result;
    }
  }

  // UPS.Input.APCSensitivity (NUT apc_sensitivity_info)
  if (read_raw(ITEM_SENSITIVITY, raw)) {
    switch (raw) {
      case 0:
        data.config.input_sensitivity = sensitivity::LOW;
        data.config.sensitivity_level = ConfigData::SENSITIVITY_LOW;
        break;
      case 1:
        data.config.input_sensitivity = sensitivity::NORMAL;
        data.config.sensitivity_level = ConfigData::SENSITIVITY_MEDIUM;
        break;
      case 2:
        data.config.input_sensitivity = sensitivity::HIGH;
        data.config.sensitivity_level = ConfigData::SENSITIVITY_HIGH;
        break;
      default:
        break;
    }
  }
}

bool ApcHidProtocol::read_timers(UpsData &data) {
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

bool ApcHidProtocol::read_timer_data(UpsData &data) {
  points_.begin_poll();
  return read_timers(data);
}

bool ApcHidProtocol::beeper_enable() { return write_value(ITEM_BEEPER, beeper::CONTROL_ENABLE, "Beeper enable"); }

bool ApcHidProtocol::beeper_disable() { return write_value(ITEM_BEEPER, beeper::CONTROL_DISABLE, "Beeper disable"); }

bool ApcHidProtocol::beeper_mute() { return write_value(ITEM_BEEPER, beeper::CONTROL_MUTE, "Beeper mute"); }

bool ApcHidProtocol::start_battery_test_quick() {
  return write_value(ITEM_BATTERY_TEST, test::COMMAND_QUICK, "Quick battery test");
}

bool ApcHidProtocol::start_battery_test_deep() {
  return write_value(ITEM_BATTERY_TEST, test::COMMAND_DEEP, "Deep battery test");
}

bool ApcHidProtocol::stop_battery_test() {
  return write_value(ITEM_BATTERY_TEST, test::COMMAND_ABORT, "Stop battery test");
}

bool ApcHidProtocol::start_ups_test() { return write_value(ITEM_PANEL_TEST, PANEL_TEST_START, "Panel test"); }

bool ApcHidProtocol::stop_ups_test() { return write_value(ITEM_PANEL_TEST, PANEL_TEST_STOP, "Stop panel test"); }

std::unique_ptr<UpsProtocolBase> create_apc_protocol(UpsHidComponent *parent) {
  return std::make_unique<ApcHidProtocol>(parent);
}

}  // namespace ups_hid
}  // namespace esphome

// Register APC protocol for vendor ID 0x051D
REGISTER_UPS_PROTOCOL_FOR_VENDOR(0x051D, apc_hid_protocol, esphome::ups_hid::create_apc_protocol, "APC HID Protocol", "APC HID Power Device protocol, located through the HID report descriptor (NUT apc-hid compatible)", 100);
