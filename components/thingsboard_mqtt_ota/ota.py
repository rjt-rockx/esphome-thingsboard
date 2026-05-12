import esphome.codegen as cg
from esphome.components.ota import BASE_OTA_SCHEMA, OTAComponent, ota_to_code
from esphome.components.thingsboard import ThingsBoardComponent
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import coroutine_with_priority

CODEOWNERS = ["@rjt-rockx"]
DEPENDENCIES = ["thingsboard", "thingsboard_mqtt"]
AUTO_LOAD = ["thingsboard_mqtt_ota"]

thingsboard_mqtt_ota_ns = cg.esphome_ns.namespace("thingsboard_mqtt_ota")
ThingsBoardMqttOtaComponent = thingsboard_mqtt_ota_ns.class_(
    "ThingsBoardMqttOtaComponent", OTAComponent
)

CONF_THINGSBOARD_ID = "thingsboard_id"
CONF_CHUNK_SIZE = "chunk_size"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ThingsBoardMqttOtaComponent),
            cv.Required(CONF_THINGSBOARD_ID): cv.use_id(ThingsBoardComponent),
            cv.Optional(CONF_CHUNK_SIZE, default=4096): cv.int_range(
                min=512, max=65536
            ),
        }
    )
    .extend(BASE_OTA_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


@coroutine_with_priority(52.0)
async def to_code(config):
    cg.add_define("USE_THINGSBOARD_MQTT_OTA")

    var = cg.new_Pvariable(config[CONF_ID])

    tb_component = await cg.get_variable(config[CONF_THINGSBOARD_ID])
    cg.add(var.set_thingsboard_component(tb_component))
    cg.add(var.set_chunk_size(config[CONF_CHUNK_SIZE]))

    await cg.register_component(var, config)
    await ota_to_code(var, config)
