import logging

import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIMEOUT, CONF_UPDATE_INTERVAL

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@tmisker"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor"]

CONF_EVBOX_SMARTGRID_ID = "evbox_smartgrid_id"
CONF_FALLBACK_CURRENT = "fallback_current"
CONF_MAX_CURRENT = "max_current"

evbox_smartgrid_ns = cg.esphome_ns.namespace("evbox_smartgrid")
EVBoxSmartGrid = evbox_smartgrid_ns.class_(
    "EVBoxSmartGrid", cg.PollingComponent, uart.UARTDevice
)


def _validate_timeout(config):
    # The ChargePoint applies the fallback current once the timeout expires, so a
    # single late frame must not be enough to trigger it.
    timeout_ms = config[CONF_TIMEOUT].total_milliseconds
    interval_ms = config[CONF_UPDATE_INTERVAL].total_milliseconds
    if timeout_ms < 2 * interval_ms:
        raise cv.Invalid(
            f"'{CONF_TIMEOUT}' must be at least twice '{CONF_UPDATE_INTERVAL}'"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EVBoxSmartGrid),
            cv.Optional(CONF_MAX_CURRENT, default=16.0): cv.float_range(
                min=0.0, max=80.0
            ),
            cv.Optional(CONF_FALLBACK_CURRENT, default=16.0): cv.float_range(
                min=0.0, max=80.0
            ),
            cv.Optional(CONF_TIMEOUT, default="60s"): cv.All(
                cv.positive_time_period_seconds,
                cv.Range(
                    min=cv.TimePeriod(seconds=10), max=cv.TimePeriod(seconds=65535)
                ),
            ),
        }
    )
    .extend(cv.polling_component_schema("15s"))
    .extend(uart.UART_DEVICE_SCHEMA),
    _validate_timeout,
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "evbox_smartgrid",
    baud_rate=38400,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    _LOGGER.warning(
        "evbox_smartgrid is experimental and untested on real chargers. "
        "Use it at your own risk; see the disclaimer in the README."
    )
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_max_current(config[CONF_MAX_CURRENT]))
    cg.add(var.set_fallback_current(config[CONF_FALLBACK_CURRENT]))
    cg.add(var.set_keepalive_timeout(config[CONF_TIMEOUT].total_seconds))
