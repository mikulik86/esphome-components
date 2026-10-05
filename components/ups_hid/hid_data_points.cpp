#include "hid_data_points.h"
#include "constants_hid.h"
#include "constants_ups.h"
#include "ups_hid.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace esphome {
namespace ups_hid {

namespace {

// Only used in log messages, which lower log levels compile out
[[maybe_unused]] const char *report_type_name(uint8_t report_type) {
  switch (report_type) {
    case HID_REPORT_TYPE_INPUT: return "Input";
    case HID_REPORT_TYPE_OUTPUT: return "Output";
    default: return "Feature";
  }
}

}  // namespace

HidDataPoints::HidDataPoints(UpsHidComponent *parent, const char *tag, const HidPathDefinition *paths,
                             size_t path_count, size_t item_count)
    : parent_(parent),
      tag_(tag),
      paths_(paths),
      path_count_(path_count),
      fields_(item_count),
      ranks_(item_count, NO_FIELD) {}

esp_err_t HidDataPoints::load(uint8_t descriptor_index) {
  loaded_ = false;
  std::fill(ranks_.begin(), ranks_.end(), NO_FIELD);
  report_cache_.clear();

  std::vector<uint8_t> raw;
  esp_err_t err = parent_->get_hid_report_descriptor(descriptor_index, raw);
  if (err != ESP_OK) {
    return err;
  }
  if (raw.empty()) {
    return ESP_ERR_INVALID_SIZE;
  }

  size_t field_count = 0;
  const bool complete = descriptor_.parse(raw.data(), raw.size(), [this, &field_count](const HidField &field) {
    field_count++;
    ESP_LOGV(tag_, "Path: %s, Type: %s, ReportID: 0x%02X, Offset: %u, Size: %u",
             HidReportDescriptor::path_to_string(field).c_str(), report_type_name(field.report_type), field.report_id,
             field.bit_offset, field.bit_size);
    map_field(field);
  });
  if (!complete) {
    ESP_LOGW(tag_, "Report descriptor is malformed or truncated, using the %zu fields before the error",
             field_count);
  }

  ESP_LOGI(tag_, "HID report descriptor: %zu bytes, %zu fields", raw.size(), field_count);
  if (field_count == 0) {
    return ESP_ERR_NOT_FOUND;
  }
  loaded_ = true;
  return ESP_OK;
}

void HidDataPoints::map_field(const HidField &field) {
  for (size_t rank = 0; rank < path_count_; rank++) {
    const HidPathDefinition &definition = paths_[rank];
    if (!field.matches(definition.nodes, definition.length)) {
      continue;
    }
    const uint8_t item = definition.item;
    const uint8_t current = ranks_[item];
    // Earlier table entries win. For the same path prefer the Feature report, which
    // is what NUT reads; Input reports with the same ID can have another layout.
    const bool better = current == NO_FIELD || rank < current ||
                        (rank == current && field.report_type == HID_REPORT_TYPE_FEATURE &&
                         fields_[item].report_type != HID_REPORT_TYPE_FEATURE);
    if (better) {
      fields_[item] = field;
      ranks_[item] = static_cast<uint8_t>(rank);
    }
    return;
  }
}

bool HidDataPoints::read_raw(uint8_t item, int64_t &value) {
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
      ESP_LOGV(tag_, "%s report 0x%02X failed: %s", report_type_name(field.report_type), field.report_id,
               esp_err_to_name(err));
    } else if (field.report_id != 0 && buffer[0] != field.report_id) {
      ESP_LOGD(tag_, "%s report 0x%02X answered with report ID 0x%02X, ignoring", report_type_name(field.report_type),
               field.report_id, buffer[0]);
    } else {
      report.valid = true;
      report.data.assign(buffer, buffer + received);
    }
    report_cache_.push_back(std::move(report));
    cached = report_cache_.end() - 1;
  }

  return cached->valid && field.extract(cached->data.data(), cached->data.size(), value);
}

bool HidDataPoints::read_value(uint8_t item, float &value) {
  int64_t raw;
  if (!read_raw(item, raw)) {
    return false;
  }
  value = static_cast<float>(fields_[item].to_physical(raw));
  return true;
}

bool HidDataPoints::read_flag(uint8_t item, bool &value) {
  int64_t raw;
  if (!read_raw(item, raw)) {
    return false;
  }
  value = raw != 0;
  return true;
}

bool HidDataPoints::read_string(uint8_t item, std::string &value) {
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

bool HidDataPoints::write_value(uint8_t item, float value, const char *action) {
  if (!has(item)) {
    ESP_LOGW(tag_, "%s: not supported by this UPS", action);
    return false;
  }
  const HidField &field = fields_[item];
  if (field.report_type != HID_REPORT_TYPE_FEATURE) {
    ESP_LOGW(tag_, "%s: control is read-only on this UPS", action);
    return false;
  }

  const size_t length = descriptor_.report_size(field.report_id, HID_REPORT_TYPE_FEATURE);
  uint8_t report[limits::MAX_HID_REPORT_SIZE];
  if (length == 0 || length > sizeof(report)) {
    ESP_LOGW(tag_, "%s: unsupported report size %zu", action, length);
    return false;
  }

  // Read-modify-write, so other settings in the same report keep their values
  size_t received = length;
  esp_err_t err = parent_->hid_get_report(HID_REPORT_TYPE_FEATURE, field.report_id, report, &received,
                                          parent_->get_protocol_timeout());
  if (err != ESP_OK || received < length || (field.report_id != 0 && report[0] != field.report_id)) {
    ESP_LOGW(tag_, "%s: cannot read report 0x%02X before writing it", action, field.report_id);
    return false;
  }
  field.insert(report, length, field.to_logical(value));

  err = parent_->hid_set_report(HID_REPORT_TYPE_FEATURE, field.report_id, report, length,
                                parent_->get_protocol_timeout());
  if (err != ESP_OK) {
    ESP_LOGW(tag_, "%s: HID SET_REPORT 0x%02X failed: %s", action, field.report_id, esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(tag_, "%s: command sent", action);
  return true;
}

void HidDataPoints::log_data_points(const std::function<bool(uint8_t)> &used) const {
  size_t found = 0;
  size_t unused = 0;
  for (size_t rank = 0; rank < path_count_; rank++) {
    const HidPathDefinition &definition = paths_[rank];
    if (ranks_[definition.item] != rank) {
      continue;
    }
    const HidField &field = fields_[definition.item];
    const bool is_used = used(definition.item);
    found++;
    if (!is_used) {
      unused++;
    }
    ESP_LOGD(tag_, "  %-28s <- %s (%s report 0x%02X, bit %u, %u bits)%s", definition.name,
             HidReportDescriptor::path_to_string(field).c_str(), report_type_name(field.report_type), field.report_id,
             field.bit_offset, field.bit_size, is_used ? "" : " - not used on this model");
  }

  ESP_LOGI(tag_, "%zu of %zu data points found, %zu not used on this model", found, fields_.size(), unused);
}

namespace hid_pdc {

bool in_range(float value, float min, float max) { return !std::isnan(value) && value >= min && value <= max; }

int16_t to_timer(float seconds) {
  const float rounded = std::round(seconds);
  return static_cast<int16_t>(std::max<float>(std::numeric_limits<int16_t>::min(),
                                              std::min<float>(std::numeric_limits<int16_t>::max(), rounded)));
}

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

}  // namespace hid_pdc

}  // namespace ups_hid
}  // namespace esphome
