#include "control_switch.h"

#ifdef USE_SWITCH

#include "esphome/core/log.h"

namespace esphome {
namespace ups_hid {

static const char *const TAG_SWITCH = "ups_hid.switch";

void UpsBeeperSwitch::setup() {
  // Only restore the wanted setting: no UPS is connected yet, the component sends it
  // once the UPS reports its beeper
  auto initial = this->get_initial_state_with_restore_mode();
  if (initial.has_value()) {
    this->set_wanted(*initial);
  }
}

void UpsBeeperSwitch::dump_config() { LOG_SWITCH("", "UPS Beeper Switch", this); }

void UpsBeeperSwitch::set_wanted(bool enabled) {
  this->has_wanted_ = true;
  this->publish_state(enabled);
}

void UpsBeeperSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG_SWITCH, "Parent UPS HID component not set");
    return;
  }
  // Sends the setting now, and makes it the wanted one (set_wanted) even without a UPS
  if (state) {
    this->parent_->beeper_enable();
  } else {
    this->parent_->beeper_disable();
  }
}

}  // namespace ups_hid
}  // namespace esphome

#endif  // USE_SWITCH
