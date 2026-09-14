import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER_FACTOR,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_KILOWATT_HOURS,
    UNIT_SECOND,
)

from . import CONF_EVBOX_SMARTGRID_ID, EVBoxSmartGrid

DEPENDENCIES = ["evbox_smartgrid"]

CONF_CONNECTION_MAX_CURRENT = "connection_max_current"
CONF_ENERGY = "energy"
CONF_MINIMUM_CURRENT = "minimum_current"
CONF_MINIMUM_INTERVAL = "minimum_interval"

PHASES = (1, 2, 3)


def _current_schema(**kwargs):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
        **kwargs,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_EVBOX_SMARTGRID_ID): cv.use_id(EVBoxSmartGrid),
        **{cv.Optional(f"current_l{phase}"): _current_schema() for phase in PHASES},
        **{
            cv.Optional(f"power_factor_l{phase}"): sensor.sensor_schema(
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_POWER_FACTOR,
                state_class=STATE_CLASS_MEASUREMENT,
            )
            for phase in PHASES
        },
        cv.Optional(CONF_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_KILOWATT_HOURS,
            accuracy_decimals=3,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_MINIMUM_CURRENT): _current_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_CONNECTION_MAX_CURRENT): _current_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_MINIMUM_INTERVAL): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DURATION,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_EVBOX_SMARTGRID_ID])
    for index, phase in enumerate(PHASES):
        if (conf := config.get(f"current_l{phase}")) is not None:
            sens = await sensor.new_sensor(conf)
            cg.add(parent.set_current_sensor(index, sens))
        if (conf := config.get(f"power_factor_l{phase}")) is not None:
            sens = await sensor.new_sensor(conf)
            cg.add(parent.set_power_factor_sensor(index, sens))
    for key in (
        CONF_ENERGY,
        CONF_MINIMUM_CURRENT,
        CONF_CONNECTION_MAX_CURRENT,
        CONF_MINIMUM_INTERVAL,
    ):
        if (conf := config.get(key)) is not None:
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(parent, f"set_{key}_sensor")(sens))
