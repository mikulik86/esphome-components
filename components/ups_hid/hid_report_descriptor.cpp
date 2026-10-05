#include "hid_report_descriptor.h"
#include "constants_hid.h"

#include <cmath>
#include <cstdio>

namespace esphome {
namespace ups_hid {

namespace {

// Item prefix layout (HID 1.11, 6.2.2.2): bits 0-1 data size, bits 2-7 type and tag
constexpr uint8_t ITEM_SIZE_MASK = 0x03;
constexpr uint8_t ITEM_TAG_MASK = 0xFC;
constexpr uint8_t ITEM_LONG = 0xFE;
constexpr uint8_t ITEM_DATA_SIZES[4] = {0, 1, 2, 4};

constexpr uint8_t MAIN_INPUT = 0x80;
constexpr uint8_t MAIN_OUTPUT = 0x90;
constexpr uint8_t MAIN_COLLECTION = 0xA0;
constexpr uint8_t MAIN_FEATURE = 0xB0;
constexpr uint8_t MAIN_END_COLLECTION = 0xC0;

constexpr uint8_t GLOBAL_USAGE_PAGE = 0x04;
constexpr uint8_t GLOBAL_LOGICAL_MIN = 0x14;
constexpr uint8_t GLOBAL_LOGICAL_MAX = 0x24;
constexpr uint8_t GLOBAL_PHYSICAL_MIN = 0x34;
constexpr uint8_t GLOBAL_PHYSICAL_MAX = 0x44;
constexpr uint8_t GLOBAL_UNIT_EXPONENT = 0x54;
constexpr uint8_t GLOBAL_UNIT = 0x64;
constexpr uint8_t GLOBAL_REPORT_SIZE = 0x74;
constexpr uint8_t GLOBAL_REPORT_ID = 0x84;
constexpr uint8_t GLOBAL_REPORT_COUNT = 0x94;
constexpr uint8_t GLOBAL_PUSH = 0xA4;
constexpr uint8_t GLOBAL_POP = 0xB4;

constexpr uint8_t LOCAL_USAGE = 0x08;
constexpr uint8_t LOCAL_USAGE_MIN = 0x18;
constexpr uint8_t LOCAL_USAGE_MAX = 0x28;

constexpr uint32_t COLLECTION_TYPE_VENDOR = 0x80;  // 0x80-0xFF: NUT reads the low 7 bits as an index
constexpr uint32_t MAX_USAGE_RANGE = 256;          // cap for Usage Minimum/Maximum expansion
constexpr size_t MAX_GLOBAL_STACK = 8;
constexpr uint32_t MAX_FIELD_BITS = 32;

// HID units are CGS based: 1 V and 1 W (or VA) carry a built-in exponent of 7
// (HID PDC 1.1 section 3.2.3, NUT libhid.c HIDUnits)
constexpr uint32_t UNIT_VOLT = 0x00F0D121;
constexpr uint32_t UNIT_WATT = 0x0000D121;
constexpr int CGS_EXPONENT = 7;
constexpr int8_t UNIT_EXPONENT_NIBBLE_MAX = 7;
constexpr uint8_t UNIT_EXPONENT_SIGN_EXTEND = 0xF0;

struct GlobalState {
  uint16_t usage_page{0};
  int64_t logical_min{0};
  int64_t logical_max{0};
  int32_t physical_min{0};
  int32_t physical_max{0};
  bool has_physical_min{false};
  bool has_physical_max{false};
  uint32_t unit{0};
  int8_t unit_exponent{0};
  uint32_t report_size{0};
  uint8_t report_id{0};
  uint32_t report_count{0};
};

int32_t sign_extend(uint32_t value, uint8_t size) {
  switch (size) {
    case 1:
      return static_cast<int8_t>(value);
    case 2:
      return static_cast<int16_t>(value);
    default:
      return static_cast<int32_t>(value);
  }
}

uint8_t report_type_for(uint8_t main_tag) {
  switch (main_tag) {
    case MAIN_INPUT:
      return HID_REPORT_TYPE_INPUT;
    case MAIN_OUTPUT:
      return HID_REPORT_TYPE_OUTPUT;
    default:
      return HID_REPORT_TYPE_FEATURE;
  }
}

unsigned int high_bit(uint64_t value) {
  unsigned int bits = 0;
  while (value != 0) {
    value >>= 1;
    bits++;
  }
  return bits;
}

int si_exponent(const HidField &field) {
  int exponent = field.unit_exponent;
  if (field.unit == UNIT_VOLT || field.unit == UNIT_WATT) {
    exponent -= CGS_EXPONENT;
  }
  return exponent;
}

bool uses_physical_range(const HidField &field) {
  // HID: an undefined (or 0/0) physical range means physical == logical
  return field.has_physical_range && field.physical_max > field.physical_min &&
         field.logical_max > field.logical_min;
}

struct UsageName {
  uint32_t usage;
  const char *name;
};

// Names as printed by NUT, for readable debug output
const UsageName USAGE_NAMES[] = {
    // Power Device page (0x84)
    {0x00840001, "iName"}, {0x00840002, "PresentStatus"}, {0x00840003, "ChangedStatus"},
    {0x00840004, "UPS"}, {0x00840005, "PowerSupply"}, {0x00840010, "BatterySystem"},
    {0x00840011, "BatterySystemID"}, {0x00840012, "Battery"}, {0x00840013, "BatteryID"},
    {0x00840014, "Charger"}, {0x00840015, "ChargerID"}, {0x00840016, "PowerConverter"},
    {0x00840017, "PowerConverterID"}, {0x00840018, "OutletSystem"}, {0x00840019, "OutletSystemID"},
    {0x0084001a, "Input"}, {0x0084001b, "InputID"}, {0x0084001c, "Output"}, {0x0084001d, "OutputID"},
    {0x0084001e, "Flow"}, {0x0084001f, "FlowID"}, {0x00840020, "Outlet"}, {0x00840021, "OutletID"},
    {0x00840022, "Gang"}, {0x00840023, "GangID"}, {0x00840024, "PowerSummary"},
    {0x00840025, "PowerSummaryID"}, {0x00840030, "Voltage"}, {0x00840031, "Current"},
    {0x00840032, "Frequency"}, {0x00840033, "ApparentPower"}, {0x00840034, "ActivePower"},
    {0x00840035, "PercentLoad"}, {0x00840036, "Temperature"}, {0x00840037, "Humidity"},
    {0x00840038, "BadCount"}, {0x00840040, "ConfigVoltage"}, {0x00840041, "ConfigCurrent"},
    {0x00840042, "ConfigFrequency"}, {0x00840043, "ConfigApparentPower"},
    {0x00840044, "ConfigActivePower"}, {0x00840045, "ConfigPercentLoad"},
    {0x00840046, "ConfigTemperature"}, {0x00840047, "ConfigHumidity"},
    {0x00840050, "SwitchOnControl"}, {0x00840051, "SwitchOffControl"}, {0x00840052, "ToggleControl"},
    {0x00840053, "LowVoltageTransfer"}, {0x00840054, "HighVoltageTransfer"},
    {0x00840055, "DelayBeforeReboot"}, {0x00840056, "DelayBeforeStartup"},
    {0x00840057, "DelayBeforeShutdown"}, {0x00840058, "Test"}, {0x00840059, "ModuleReset"},
    {0x0084005a, "AudibleAlarmControl"}, {0x00840060, "Present"}, {0x00840061, "Good"},
    {0x00840062, "InternalFailure"}, {0x00840063, "VoltageOutOfRange"},
    {0x00840064, "FrequencyOutOfRange"}, {0x00840065, "Overload"}, {0x00840066, "OverCharged"},
    {0x00840067, "OverTemperature"}, {0x00840068, "ShutdownRequested"},
    {0x00840069, "ShutdownImminent"}, {0x0084006b, "SwitchOn/Off"}, {0x0084006c, "Switchable"},
    {0x0084006d, "Used"}, {0x0084006e, "Boost"}, {0x0084006f, "Buck"}, {0x00840070, "Initialized"},
    {0x00840071, "Tested"}, {0x00840072, "AwaitingPower"}, {0x00840073, "CommunicationLost"},
    {0x008400fd, "iManufacturer"}, {0x008400fe, "iProduct"}, {0x008400ff, "iSerialNumber"},
    // Battery System page (0x85), entries seen in UPS descriptors
    {0x00850029, "RemainingCapacityLimit"}, {0x0085002a, "RemainingTimeLimit"},
    {0x0085002c, "CapacityMode"}, {0x00850042, "BelowRemainingCapacityLimit"},
    {0x00850043, "RemainingTimeLimitExpired"}, {0x00850044, "Charging"}, {0x00850045, "Discharging"},
    {0x00850046, "FullyCharged"}, {0x00850047, "FullyDischarged"}, {0x00850048, "ConditioningFlag"},
    {0x0085004b, "NeedReplacement"}, {0x00850066, "RemainingCapacity"},
    {0x00850067, "FullChargeCapacity"}, {0x00850068, "RunTimeToEmpty"},
    {0x00850069, "AverageTimeToEmpty"}, {0x0085006a, "AverageTimeToFull"}, {0x0085006b, "CycleCount"},
    {0x00850083, "DesignCapacity"}, {0x00850085, "ManufacturerDate"}, {0x00850086, "SerialNumber"},
    {0x00850087, "iManufacturerName"}, {0x00850088, "iDevicename"}, {0x00850089, "iDeviceChemistry"},
    {0x0085008b, "Rechargeable"}, {0x0085008c, "WarningCapacityLimit"},
    {0x0085008d, "CapacityGranularity1"}, {0x0085008e, "CapacityGranularity2"},
    {0x0085008f, "iOEMInformation"}, {0x008500d0, "ACPresent"}, {0x008500d1, "BatteryPresent"},
    {0x008500d2, "PowerFail"}, {0x008500d8, "VoltageOutOfRange"},
    // APC vendor page (0xFF86), names from NUT apc-hid.c
    {0xff860005, "APCGeneralCollection"}, {0xff860006, "APCEnvironment"}, {0xff860007, "APCProbe1"},
    {0xff860008, "APCProbe2"}, {0xff860016, "APCBattReplaceDate"}, {0xff860019, "APCBattCapBeforeStartup"},
    {0xff860042, "APC_UPS_FirmwareRevision"}, {0xff860052, "APCLineFailCause"}, {0xff860060, "APCStatusFlag"},
    {0xff860061, "APCSensitivity"}, {0xff860072, "APCPanelTest"}, {0xff860076, "APCShutdownAfterDelay"},
    {0xff860079, "APC_USB_FirmwareRevision"}, {0xff86007c, "APCDelayBeforeReboot"},
    {0xff86007d, "APCDelayBeforeShutdown"}, {0xff86007e, "APCDelayBeforeStartup"},
};

const char *usage_name(uint32_t usage) {
  for (const auto &entry : USAGE_NAMES) {
    if (entry.usage == usage) {
      return entry.name;
    }
  }
  return nullptr;
}

}  // namespace

bool HidField::matches(const uint32_t *nodes, size_t count) const {
  if (count != path_length) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    if (path[i] != nodes[i]) {
      return false;
    }
  }
  return true;
}

bool HidField::extract(const uint8_t *report, size_t report_length, int64_t &value) const {
  const size_t first_bit = bit_offset + (report_id != 0 ? 8 : 0);
  if (bit_size == 0 || bit_size > MAX_FIELD_BITS || (first_bit + bit_size + 7) / 8 > report_length) {
    return false;
  }

  uint64_t raw = 0;
  for (uint8_t i = 0; i < bit_size; i++) {
    const size_t bit = first_bit + i;
    if (report[bit >> 3] & (1u << (bit & 7))) {
      raw |= (1ULL << i);
    }
  }

  // Keep only the bits the logical range needs (devices may send garbage above
  // them), then sign-extend and clamp - same rules as NUT hidparser.c GetValue()
  const int64_t min = logical_min;
  const int64_t max = logical_max;
  const uint64_t magnitude_max = max >= 0 ? static_cast<uint64_t>(max) : static_cast<uint64_t>(-(max + 1));
  const uint64_t magnitude_min = min >= 0 ? static_cast<uint64_t>(min) : static_cast<uint64_t>(-(min + 1));
  const uint64_t sign_bit = 1ULL << high_bit(magnitude_max > magnitude_min ? magnitude_max : magnitude_min);
  const uint64_t mask = (sign_bit - 1) | (min < 0 ? sign_bit : 0);

  int64_t result = static_cast<int64_t>(raw & mask);
  if (min < 0 && (static_cast<uint64_t>(result) & sign_bit) != 0) {
    result = static_cast<int64_t>(static_cast<uint64_t>(result) | ~mask);
  }
  if (result < min) {
    result = min;
  } else if (result > max) {
    result = max;
  }

  value = result;
  return true;
}

bool HidField::insert(uint8_t *report, size_t report_length, int64_t value) const {
  const size_t first_bit = bit_offset + (report_id != 0 ? 8 : 0);
  if (bit_size == 0 || bit_size > MAX_FIELD_BITS || (first_bit + bit_size + 7) / 8 > report_length) {
    return false;
  }

  const uint64_t bits = static_cast<uint64_t>(value);
  for (uint8_t i = 0; i < bit_size; i++) {
    const size_t bit = first_bit + i;
    const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
    if (bits & (1ULL << i)) {
      report[bit >> 3] |= mask;
    } else {
      report[bit >> 3] &= static_cast<uint8_t>(~mask);
    }
  }
  return true;
}

double HidField::to_physical(int64_t logical) const {
  double physical = logical;
  if (uses_physical_range(*this)) {
    const double factor = static_cast<double>(physical_max - physical_min) / (logical_max - logical_min);
    physical = (static_cast<double>(logical) - logical_min) * factor + physical_min;
    if (physical > physical_max) {
      physical = physical_max;
    } else if (physical < physical_min) {
      physical = physical_min;
    }
  }
  return physical * std::pow(10.0, si_exponent(*this));
}

int64_t HidField::to_logical(double physical) const {
  double logical = physical / std::pow(10.0, si_exponent(*this));
  if (uses_physical_range(*this)) {
    const double factor = static_cast<double>(logical_max - logical_min) / (physical_max - physical_min);
    logical = (logical - physical_min) * factor + logical_min;
  }
  int64_t rounded = std::llround(logical);
  if (rounded < logical_min) {
    rounded = logical_min;
  } else if (rounded > logical_max) {
    rounded = logical_max;
  }
  return rounded;
}

uint32_t &HidReportDescriptor::report_bits_for_(uint8_t report_id, uint8_t report_type) {
  for (auto &entry : report_bits_) {
    if (entry.report_id == report_id && entry.report_type == report_type) {
      return entry.bits;
    }
  }
  report_bits_.push_back({report_id, report_type, 0});
  return report_bits_.back().bits;
}

size_t HidReportDescriptor::report_size(uint8_t report_id, uint8_t report_type) const {
  for (const auto &entry : report_bits_) {
    if (entry.report_id == report_id && entry.report_type == report_type) {
      return (entry.bits + 7) / 8 + (report_id != 0 ? 1 : 0);
    }
  }
  return 0;
}

bool HidReportDescriptor::parse(const uint8_t *data, size_t length, const FieldCallback &on_field) {
  report_bits_.clear();

  GlobalState global;
  std::vector<GlobalState> global_stack;
  std::vector<uint32_t> usages;  // local usages waiting for the next main item
  uint32_t usage_min = 0;
  bool has_usage_min = false;

  uint32_t path[HID_MAX_PATH_DEPTH] = {};
  size_t depth = 0;
  uint8_t collection_nodes[HID_MAX_PATH_DEPTH] = {};  // path nodes pushed by each open collection
  size_t open_collections = 0;

  auto reset_local_state = [&]() {
    usages.clear();
    has_usage_min = false;
  };

  size_t pos = 0;
  while (pos < length) {
    const uint8_t prefix = data[pos++];

    if (prefix == ITEM_LONG) {
      // Long item: data size byte, tag byte, data. Not used by HID 1.11, skip.
      if (length - pos < 2) {
        return false;
      }
      const size_t long_size = data[pos];
      pos += 2;
      if (length - pos < long_size) {
        return false;
      }
      pos += long_size;
      continue;
    }

    const uint8_t size = ITEM_DATA_SIZES[prefix & ITEM_SIZE_MASK];
    if (length - pos < size) {
      return false;  // truncated item
    }
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++) {
      value |= static_cast<uint32_t>(data[pos + i]) << (8 * i);
    }
    pos += size;

    const uint8_t tag = prefix & ITEM_TAG_MASK;
    switch (tag) {
      case GLOBAL_USAGE_PAGE:
        global.usage_page = static_cast<uint16_t>(value);
        break;
      case GLOBAL_LOGICAL_MIN:
        global.logical_min = sign_extend(value, size);
        break;
      case GLOBAL_LOGICAL_MAX:
        global.logical_max = sign_extend(value, size);
        // Firmwares often encode an unsigned maximum in too few bytes (e.g. 0xFFFF
        // as 2 bytes reads as -1). Like NUT, read it back as unsigned in that case.
        if (global.logical_max < global.logical_min) {
          global.logical_max = value;
        }
        break;
      case GLOBAL_PHYSICAL_MIN:
        global.physical_min = sign_extend(value, size);
        global.has_physical_min = true;
        break;
      case GLOBAL_PHYSICAL_MAX:
        global.physical_max = sign_extend(value, size);
        global.has_physical_max = true;
        break;
      case GLOBAL_UNIT_EXPONENT: {
        // Spec: 4-bit signed nibble (0x0E = -2). A full signed byte (0xFE) also works, as in NUT.
        int8_t exponent = static_cast<int8_t>(value);
        if (exponent > UNIT_EXPONENT_NIBBLE_MAX) {
          exponent = static_cast<int8_t>(exponent | UNIT_EXPONENT_SIGN_EXTEND);
        }
        global.unit_exponent = exponent;
        break;
      }
      case GLOBAL_UNIT:
        global.unit = value;
        break;
      case GLOBAL_REPORT_SIZE:
        global.report_size = value;
        break;
      case GLOBAL_REPORT_ID:
        global.report_id = static_cast<uint8_t>(value);
        break;
      case GLOBAL_REPORT_COUNT:
        global.report_count = value;
        break;
      case GLOBAL_PUSH:
        if (global_stack.size() >= MAX_GLOBAL_STACK) {
          return false;
        }
        global_stack.push_back(global);
        break;
      case GLOBAL_POP:
        if (global_stack.empty()) {
          return false;
        }
        global = global_stack.back();
        global_stack.pop_back();
        break;

      case LOCAL_USAGE:
        // A 4-byte usage carries its own page, shorter ones use the current Usage Page
        usages.push_back(size == 4 ? value : (static_cast<uint32_t>(global.usage_page) << 16) | (value & 0xFFFF));
        break;
      case LOCAL_USAGE_MIN:
        usage_min = size == 4 ? value : (static_cast<uint32_t>(global.usage_page) << 16) | (value & 0xFFFF);
        has_usage_min = true;
        break;
      case LOCAL_USAGE_MAX: {
        const uint32_t usage_max =
            size == 4 ? value : (static_cast<uint32_t>(global.usage_page) << 16) | (value & 0xFFFF);
        if (has_usage_min && usage_max >= usage_min && usage_max - usage_min < MAX_USAGE_RANGE) {
          for (uint32_t usage = usage_min; usage <= usage_max; usage++) {
            usages.push_back(usage);
          }
        }
        has_usage_min = false;
        break;
      }

      case MAIN_COLLECTION: {
        const bool indexed = value >= COLLECTION_TYPE_VENDOR;
        const size_t nodes = indexed ? 2 : 1;
        if (depth + nodes > HID_MAX_PATH_DEPTH) {
          return false;
        }
        path[depth++] = usages.empty() ? 0 : usages.front();
        if (indexed) {
          path[depth++] = HID_PATH_INDEX_BASE | (value & 0x7F);
        }
        collection_nodes[open_collections++] = static_cast<uint8_t>(nodes);
        reset_local_state();
        break;
      }
      case MAIN_END_COLLECTION:
        if (open_collections == 0) {
          return false;  // unbalanced End Collection
        }
        depth -= collection_nodes[--open_collections];
        reset_local_state();
        break;

      case MAIN_INPUT:
      case MAIN_OUTPUT:
      case MAIN_FEATURE: {
        const uint8_t report_type = report_type_for(tag);
        uint32_t &report_bits = report_bits_for_(global.report_id, report_type);
        for (uint32_t i = 0; i < global.report_count; i++) {
          // Like NUT, only the first usages.size() fields are named; the rest
          // (and constant padding without a usage) just take up space
          const uint32_t usage = i < usages.size() ? usages[i] : 0;
          if (usage != 0 && depth < HID_MAX_PATH_DEPTH && report_bits <= UINT16_MAX &&
              global.report_size <= MAX_FIELD_BITS) {
            HidField field;
            for (size_t n = 0; n < depth; n++) {
              field.path[n] = path[n];
            }
            field.path[depth] = usage;
            field.path_length = static_cast<uint8_t>(depth + 1);
            field.report_id = global.report_id;
            field.report_type = report_type;
            field.bit_offset = static_cast<uint16_t>(report_bits);
            field.bit_size = static_cast<uint8_t>(global.report_size);
            field.logical_min = global.logical_min;
            field.logical_max = global.logical_max;
            field.physical_min = global.physical_min;
            field.physical_max = global.physical_max;
            field.has_physical_range = global.has_physical_min && global.has_physical_max &&
                                       !(global.physical_min == 0 && global.physical_max == 0);
            field.unit = global.unit;
            field.unit_exponent = global.unit_exponent;
            on_field(field);
          }
          report_bits += global.report_size;
        }
        reset_local_state();
        break;
      }

      default:
        // Designator, string and delimiter items do not affect data layout
        break;
    }
  }

  return open_collections == 0;
}

std::string HidReportDescriptor::path_to_string(const HidField &field) {
  std::string result;
  char buffer[12];
  for (size_t i = 0; i < field.path_length; i++) {
    if (i > 0) {
      result += '.';
    }
    const uint32_t node = field.path[i];
    const char *name = usage_name(node);
    if (name != nullptr) {
      result += name;
    } else if ((node & 0xFFFF0000) == HID_PATH_INDEX_BASE) {
      snprintf(buffer, sizeof(buffer), "[%u]", static_cast<unsigned>(node & 0xFFFF));
      result += buffer;
    } else {
      snprintf(buffer, sizeof(buffer), "%08x", static_cast<unsigned>(node));
      result += buffer;
    }
  }
  return result;
}

}  // namespace ups_hid
}  // namespace esphome
