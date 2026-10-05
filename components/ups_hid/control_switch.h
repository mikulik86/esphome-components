#pragma once

#include "esphome/core/defines.h"

#ifdef USE_SWITCH

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "ups_hid.h"

namespace esphome {
namespace ups_hid {

// The wanted beeper setting, saved in flash. Some UPSes (Eaton 5E) enable the beeper again
// after being switched off and on; the component then sets it back after the next poll.
class UpsBeeperSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(UpsHidComponent *parent) { parent_ = parent; }
  void setup() override;
  void dump_config() override;

  // False until restored or set; with restore_mode DISABLED the UPS's own setting is taken over
  bool has_wanted() const { return has_wanted_; }
  void set_wanted(bool enabled);

 protected:
  void write_state(bool state) override;

  UpsHidComponent *parent_{nullptr};
  bool has_wanted_{false};
};

}  // namespace ups_hid
}  // namespace esphome

#endif  // USE_SWITCH
