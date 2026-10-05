#pragma once

#include "esp_err.h"
#include "hid_report_descriptor.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace ups_hid {

class UpsHidComponent;

static constexpr size_t HID_PATH_DEFINITION_NODES = 6;

// One usage path that feeds a protocol data point
struct HidPathDefinition {
  uint8_t item;      // the protocol's data point
  const char *name;  // NUT variable fed by this path, for logs
  uint8_t length;
  uint32_t nodes[HID_PATH_DEFINITION_NODES];
};

/**
 * UPS data points located through the HID report descriptor
 *
 * Used by the protocols that find their values by usage path, as the NUT usbhid-ups
 * subdrivers do, instead of by hard-coded report IDs. The protocol lists the paths
 * of its data points; this class maps the descriptor's fields to them and reads and
 * writes the values.
 */
class HidDataPoints {
 public:
  // When several paths feed one item, the first one in the table that the descriptor has is used
  HidDataPoints(UpsHidComponent *parent, const char *tag, const HidPathDefinition *paths, size_t path_count,
                size_t item_count);

  // Reads report descriptor descriptor_index and maps its fields. ESP_ERR_NOT_FOUND if it has none.
  esp_err_t load(uint8_t descriptor_index);
  bool loaded() const { return loaded_; }

  bool has(uint8_t item) const { return ranks_[item] != NO_FIELD; }
  // Writable, so a protocol can correct a wrongly declared field (as NUT's fix_report_desc does)
  HidField &field(uint8_t item) { return fields_[item]; }
  const HidField &field(uint8_t item) const { return fields_[item]; }

  // Each report is read once per poll; call this before each poll
  void begin_poll() { report_cache_.clear(); }

  bool read_raw(uint8_t item, int64_t &value);
  bool read_value(uint8_t item, float &value);  // SI units: V, W, VA, Hz, s, %
  bool read_flag(uint8_t item, bool &value);
  bool read_string(uint8_t item, std::string &value);  // the field holds a USB string descriptor index
  bool write_value(uint8_t item, float value, const char *action);

  // DEBUG list of the data points found. used(item) == false marks a field the UPS has
  // but the protocol ignores on this model.
  void log_data_points(const std::function<bool(uint8_t)> &used) const;

 private:
  static constexpr uint8_t NO_FIELD = 0xFF;

  struct CachedReport {
    uint8_t report_id;
    uint8_t report_type;
    bool valid;
    std::vector<uint8_t> data;
  };

  UpsHidComponent *parent_;
  const char *tag_;
  const HidPathDefinition *paths_;
  size_t path_count_;
  HidReportDescriptor descriptor_;
  std::vector<HidField> fields_;
  std::vector<uint8_t> ranks_;  // index of the matched path definition, NO_FIELD if absent
  std::vector<CachedReport> report_cache_;
  bool loaded_{false};

  void map_field(const HidField &field);
};

// HID Power Device helpers shared by the descriptor-based protocols
namespace hid_pdc {

bool in_range(float value, float min, float max);
// Countdown as UpsData keeps it: whole seconds, clamped to int16_t
int16_t to_timer(float seconds);
// Text for a UPS.*.Test value (NUT test_read_info), nullptr if unknown
const char *test_result_text(int64_t value);

}  // namespace hid_pdc

}  // namespace ups_hid
}  // namespace esphome
