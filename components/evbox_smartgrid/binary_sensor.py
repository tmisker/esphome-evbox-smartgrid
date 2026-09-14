import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_EVBOX_SMARTGRID_ID, EVBoxSmartGrid

DEPENDENCIES = ["evbox_smartgrid"]

CONF_RESPONDING = "responding"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_EVBOX_SMARTGRID_ID): cv.use_id(EVBoxSmartGrid),
        cv.Optional(CONF_RESPONDING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_EVBOX_SMARTGRID_ID])
    if (conf := config.get(CONF_RESPONDING)) is not None:
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(parent.set_responding_binary_sensor(sens))
