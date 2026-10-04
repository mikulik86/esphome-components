#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace ups_hid {

/**
 * HID Report Descriptor Parser
 *
 * Walks a USB HID report descriptor and reports every named data field (Input,
 * Output or Feature) with its usage path, report ID, bit position and scaling.
 *
 * Usage paths follow the NUT (Network UPS Tools) hidparser.c conventions, so they
 * match the paths used in NUT driver tables and "usbhid-ups -DD" output:
 *  - every Collection adds its usage to the path
 *  - a vendor-defined collection type (0x80-0xFF) also adds an index node,
 *    printed as "[n]" (e.g. UPS.PowerConverter.Input.[1].Voltage)
 *
 * This is needed for UPS families (e.g. Eaton/MGE) that place the same data in
 * different reports depending on the model. Platform independent so it can be
 * unit tested on a host.
 */

// Path node added by a vendor-defined collection type: HID_PATH_INDEX_BASE | index
static constexpr uint32_t HID_PATH_INDEX_BASE = 0x00FF0000;
static constexpr size_t HID_MAX_PATH_DEPTH = 10;  // NUT PATH_SIZE

struct HidField {
  uint32_t path[HID_MAX_PATH_DEPTH]{};  // usage path, page << 16 | usage id
  uint8_t path_length{0};
  uint8_t report_id{0};
  uint8_t report_type{0};  // HID_REPORT_TYPE_INPUT / _OUTPUT / _FEATURE
  uint16_t bit_offset{0};  // first bit after the report ID byte
  uint8_t bit_size{0};
  int64_t logical_min{0};  // 64-bit: an unsigned 32-bit maximum must stay positive
  int64_t logical_max{0};
  int32_t physical_min{0};
  int32_t physical_max{0};
  bool has_physical_range{false};
  uint32_t unit{0};
  int8_t unit_exponent{0};

  bool matches(const uint32_t *nodes, size_t count) const;

  // Raw value from a report buffer (byte 0 = report ID when report_id != 0).
  // High bits outside the logical range are dropped, the value is sign-extended
  // when logical_min < 0 and clamped to the logical range, as NUT GetValue() does.
  bool extract(const uint8_t *report, size_t report_length, int64_t &value) const;
  bool insert(uint8_t *report, size_t report_length, int64_t value) const;

  // Logical value -> value in SI units (V, W, VA, Hz, s, %), and back
  double to_physical(int64_t logical) const;
  int64_t to_logical(double physical) const;
};

class HidReportDescriptor {
 public:
  using FieldCallback = std::function<void(const HidField &field)>;

  // Calls on_field for every data field that has a usage. Returns false if the descriptor is
  // malformed or truncated; fields seen up to that point have already been reported.
  bool parse(const uint8_t *data, size_t length, const FieldCallback &on_field);

  // Report length in bytes including the report ID byte, 0 if the report is not declared
  size_t report_size(uint8_t report_id, uint8_t report_type) const;

  // NUT style path, e.g. "UPS.PowerSummary.PresentStatus.ACPresent"; unknown usages print as hex
  static std::string path_to_string(const HidField &field);

 private:
  struct ReportBits {
    uint8_t report_id;
    uint8_t report_type;
    uint32_t bits;
  };
  std::vector<ReportBits> report_bits_;

  uint32_t &report_bits_for_(uint8_t report_id, uint8_t report_type);
};

}  // namespace ups_hid
}  // namespace esphome
