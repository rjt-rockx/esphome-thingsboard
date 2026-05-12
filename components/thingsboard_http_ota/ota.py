import esphome.codegen as cg
from esphome.components.http_request import HttpRequestComponent
from esphome.components.ota import BASE_OTA_SCHEMA, OTAComponent, ota_to_code
from esphome.components.thingsboard import ThingsBoardComponent
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import coroutine_with_priority

CODEOWNERS = ["@rjt-rockx"]
DEPENDENCIES = ["thingsboard", "http_request"]
AUTO_LOAD = ["thingsboard_http_ota"]

thingsboard_http_ota_ns = cg.esphome_ns.namespace("thingsboard_http_ota")
ThingsBoardHttpOtaComponent = thingsboard_http_ota_ns.class_(
    "ThingsBoardHttpOtaComponent", OTAComponent
)

CONF_THINGSBOARD_ID = "thingsboard_id"
CONF_HTTP_REQUEST_ID = "http_request_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ThingsBoardHttpOtaComponent),
            cv.Required(CONF_THINGSBOARD_ID): cv.use_id(ThingsBoardComponent),
            cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(HttpRequestComponent),
        }
    )
    .extend(BASE_OTA_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


@coroutine_with_priority(52.0)
async def to_code(config):
    cg.add_define("USE_THINGSBOARD_HTTP_OTA")

    var = cg.new_Pvariable(config[CONF_ID])

    tb_component = await cg.get_variable(config[CONF_THINGSBOARD_ID])
    cg.add(var.set_thingsboard_component(tb_component))

    http_component = await cg.get_variable(config[CONF_HTTP_REQUEST_ID])
    cg.add(var.set_parent(http_component))

    await cg.register_component(var, config)
    await ota_to_code(var, config)
