"""ESPHome Switch Platform for UPS HID Component"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import ups_hid_ns, UpsHidComponent, CONF_UPS_HID_ID

DEPENDENCIES = ["ups_hid"]

UpsBeeperSwitch = ups_hid_ns.class_("UpsBeeperSwitch", switch.Switch, cg.Component)

# The switch holds the wanted beeper setting and the component keeps the UPS on it, so it
# restores from flash, and starts on (the UPS default) when nothing is saved yet
CONFIG_SCHEMA = cv.typed_schema(
    {
        "beeper": switch.switch_schema(
            UpsBeeperSwitch,
            icon="mdi:volume-high",
            default_restore_mode="RESTORE_DEFAULT_ON",
        )
        .extend(
            {
                cv.GenerateID(CONF_UPS_HID_ID): cv.use_id(UpsHidComponent),
            }
        )
        .extend(cv.COMPONENT_SCHEMA),
    },
    lower=True,
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_UPS_HID_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_beeper_switch(var))
