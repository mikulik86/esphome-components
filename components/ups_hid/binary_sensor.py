import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_BATTERY_CHARGING,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_POWER,
)

from . import ups_hid_ns, UpsHidComponent, CONF_UPS_HID_ID

DEPENDENCIES = ["ups_hid"]

UpsHidBinarySensor = ups_hid_ns.class_(
    "UpsHidBinarySensor", binary_sensor.BinarySensor, cg.Component
)

BINARY_SENSOR_TYPES = {
    "online": {
        "device_class": DEVICE_CLASS_POWER,
    },
    # No device class: Home Assistant has none for "on battery", so it reads On / Off
    "on_battery": {},
    "low_battery": {
        "device_class": DEVICE_CLASS_BATTERY,
    },
    "fault": {
        "device_class": DEVICE_CLASS_PROBLEM,
    },
    "overload": {
        "device_class": DEVICE_CLASS_PROBLEM,
    },
    "replace_battery": {
        "device_class": DEVICE_CLASS_PROBLEM,
    },
    "charging": {
        "device_class": DEVICE_CLASS_BATTERY_CHARGING,
    },
}


def _binary_sensor_type_schema(type_defaults):
    return binary_sensor.binary_sensor_schema(
        UpsHidBinarySensor,
        device_class=type_defaults.get("device_class", cv.UNDEFINED),
    ).extend(
        {
            cv.GenerateID(CONF_UPS_HID_ID): cv.use_id(UpsHidComponent),
        }
    )


# One schema per type: the type's device class becomes a schema default that YAML
# can override. ESPHome reads it from the validated config (there is no runtime
# setter for the device class since 2026.9).
CONFIG_SCHEMA = cv.typed_schema(
    {
        sensor_type: _binary_sensor_type_schema(type_defaults)
        for sensor_type, type_defaults in BINARY_SENSOR_TYPES.items()
    },
    key=CONF_TYPE,
    lower=True,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_UPS_HID_ID])
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)

    sensor_type = config[CONF_TYPE]
    cg.add(var.set_sensor_type(sensor_type))
    cg.add(parent.register_binary_sensor(var, sensor_type))
