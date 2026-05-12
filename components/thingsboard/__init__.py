import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import (
    CONF_ID,
    CONF_TIMEOUT,
)
from esphome.components import http_request
from esphome.core import CORE, coroutine_with_priority

CODEOWNERS = ["@rjt-rockx"]
DEPENDENCIES = ["wifi", "http_request"]
AUTO_LOAD = ["json"]

thingsboard_ns = cg.esphome_ns.namespace("thingsboard")
ThingsBoardComponent = thingsboard_ns.class_("ThingsBoardComponent", cg.Component)
ThingsBoardMQTT = thingsboard_ns.class_("ThingsBoardMQTT")
TBTransport = thingsboard_ns.class_("TBTransport")

CONF_SERVER_URL = "server_url"
CONF_DEVICE_NAME = "device_name"
CONF_TELEMETRY_INTERVAL = "telemetry_interval"
CONF_TELEMETRY_THROTTLE = "telemetry_throttle"
CONF_PERIODIC_SYNC_INTERVAL = "periodic_sync_interval"
CONF_ON_CONNECT = "on_connect"
CONF_ON_DISCONNECT = "on_disconnect"
CONF_ON_RPC = "on_rpc"
CONF_ON_SHARED_ATTRIBUTES = "on_shared_attributes"
CONF_ON_RPC_RESPONSE = "on_rpc_response"
CONF_HTTP_REQUEST_ID = "http_request_id"
CONF_DEVICE_TOKEN = "device_token"
CONF_CLAIM_SECRET_KEY = "claim_secret_key"
CONF_CLAIM_DURATION_MS = "claim_duration_ms"
CONF_OFFLINE_QUEUE_MAX = "offline_queue_max"
CONF_USE_CLIENT_TIMESTAMPS = "use_client_timestamps"


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ThingsBoardComponent),
            cv.Required(CONF_SERVER_URL): cv.url,
            # Empty string lets the ThingsBoard server generate the device name.
            cv.Optional(CONF_DEVICE_NAME): cv.string,
            # If a transport block also sets a token, the transport's token wins.
            cv.Optional(CONF_DEVICE_TOKEN): cv.string,
            cv.Optional(CONF_CLAIM_SECRET_KEY): cv.string,
            cv.Optional(CONF_CLAIM_DURATION_MS): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_TELEMETRY_INTERVAL
            ): cv.positive_time_period_milliseconds,  # 0/omit = 100ms internal default
            cv.Optional(
                CONF_TELEMETRY_THROTTLE
            ): cv.positive_time_period_milliseconds,  # 0/omit = no throttle
            cv.Optional(
                CONF_PERIODIC_SYNC_INTERVAL
            ): cv.positive_time_period_milliseconds,
            # Cap on the in-memory queue used to buffer telemetry across an
            # MQTT/HTTP disconnect. Oldest entries drop FIFO when the queue
            # overflows.
            cv.Optional(
                CONF_OFFLINE_QUEUE_MAX, default=200
            ): cv.positive_not_null_int,
            # Emit `[{"ts":<ms>,"values":{...}}]` so replays after a
            # disconnect carry the original capture time instead of TB's
            # server-receive time.
            cv.Optional(CONF_USE_CLIENT_TIMESTAMPS, default=False): cv.boolean,
            cv.Optional(
                CONF_TIMEOUT, default="10s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_HTTP_REQUEST_ID): cv.use_id(
                http_request.HttpRequestComponent
            ),
            cv.Optional(CONF_ON_CONNECT): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_DISCONNECT): automation.validate_automation(
                single=True
            ),
            cv.Optional(CONF_ON_RPC): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_SHARED_ATTRIBUTES): automation.validate_automation(
                single=True
            ),
            cv.Optional(CONF_ON_RPC_RESPONSE): automation.validate_automation(
                single=True
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
)


def _final_validate(config):
    """Enforce that the YAML configures exactly one transport sibling
    (`thingsboard_mqtt:` or `thingsboard_http:`)."""
    transports = []
    if "thingsboard_mqtt" in CORE.loaded_integrations:
        transports.append("thingsboard_mqtt")
    if "thingsboard_http" in CORE.loaded_integrations:
        transports.append("thingsboard_http")
    if len(transports) == 0:
        raise cv.Invalid(
            "thingsboard: requires exactly one transport — add a "
            "`thingsboard_mqtt:` or `thingsboard_http:` block to your YAML."
        )
    if len(transports) > 1:
        raise cv.Invalid(
            "thingsboard: configure exactly one transport, got "
            f"{', '.join(transports)}. Pick MQTT or HTTP, not both."
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


@coroutine_with_priority(1.0)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    CORE.register_controller()

    if CONF_HTTP_REQUEST_ID in config:
        await cg.register_parented(var, config[CONF_HTTP_REQUEST_ID])

    cg.add(var.set_server_url(config[CONF_SERVER_URL]))
    cg.add(var.set_timeout(config[CONF_TIMEOUT]))

    if CONF_DEVICE_TOKEN in config and config[CONF_DEVICE_TOKEN]:
        cg.add(var.set_device_token(config[CONF_DEVICE_TOKEN]))

    if CONF_DEVICE_NAME in config:
        cg.add(var.set_device_name(config[CONF_DEVICE_NAME]))

    if CONF_CLAIM_SECRET_KEY in config:
        cg.add(var.set_claim_secret_key(config[CONF_CLAIM_SECRET_KEY]))
    if CONF_CLAIM_DURATION_MS in config:
        cg.add(var.set_claim_duration_ms(config[CONF_CLAIM_DURATION_MS]))

    if CONF_TELEMETRY_INTERVAL in config:
        cg.add(var.set_telemetry_interval(config[CONF_TELEMETRY_INTERVAL]))
    if CONF_TELEMETRY_THROTTLE in config:
        cg.add(var.set_telemetry_throttle(config[CONF_TELEMETRY_THROTTLE]))
    if CONF_PERIODIC_SYNC_INTERVAL in config:
        cg.add(var.set_periodic_sync_interval(config[CONF_PERIODIC_SYNC_INTERVAL]))
    cg.add(var.set_offline_queue_max(config[CONF_OFFLINE_QUEUE_MAX]))
    cg.add(var.set_use_client_timestamps(config[CONF_USE_CLIENT_TIMESTAMPS]))

    if CONF_ON_CONNECT in config:
        await automation.build_automation(
            var.get_connect_trigger(), [], config[CONF_ON_CONNECT]
        )
    if CONF_ON_DISCONNECT in config:
        await automation.build_automation(
            var.get_disconnect_trigger(), [], config[CONF_ON_DISCONNECT]
        )
    if CONF_ON_RPC in config:
        await automation.build_automation(
            var.get_rpc_trigger(),
            [(cg.std_string, "method"), (cg.std_string, "params")],
            config[CONF_ON_RPC],
        )
    if CONF_ON_SHARED_ATTRIBUTES in config:
        std_map = cg.std_ns.class_("map").template(cg.std_string, cg.std_string)
        await automation.build_automation(
            var.get_shared_attributes_trigger(),
            [(std_map, "attributes")],
            config[CONF_ON_SHARED_ATTRIBUTES],
        )
    if CONF_ON_RPC_RESPONSE in config:
        await automation.build_automation(
            var.get_rpc_response_trigger(),
            [(cg.std_string, "request_id"), (cg.std_string, "response")],
            config[CONF_ON_RPC_RESPONSE],
        )


ThingsBoardSendTelemetryAction = thingsboard_ns.class_(
    "ThingsBoardSendTelemetryAction", automation.Action
)
ThingsBoardSendAttributesAction = thingsboard_ns.class_(
    "ThingsBoardSendAttributesAction", automation.Action
)
ThingsBoardClearTokenAction = thingsboard_ns.class_(
    "ThingsBoardClearTokenAction", automation.Action
)
ThingsBoardSetTokenAction = thingsboard_ns.class_(
    "ThingsBoardSetTokenAction", automation.Action
)
ThingsBoardClaimDeviceAction = thingsboard_ns.class_(
    "ThingsBoardClaimDeviceAction", automation.Action
)
ThingsBoardSendRpcRequestAction = thingsboard_ns.class_(
    "ThingsBoardSendRpcRequestAction", automation.Action
)
ThingsBoardRequestAttributesAction = thingsboard_ns.class_(
    "ThingsBoardRequestAttributesAction", automation.Action
)

CONF_DATA = "data"
CONF_METHOD = "method"
CONF_PARAMS = "params"
CONF_KEYS = "keys"
CONF_TOKEN = "token"


@automation.register_action(
    "thingsboard.send_telemetry",
    ThingsBoardSendTelemetryAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ThingsBoardComponent),
            cv.Required(CONF_DATA): cv.templatable(cv.string),
        }
    ),
)
async def thingsboard_send_telemetry_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    template_ = await cg.templatable(config[CONF_DATA], args, cg.std_string)
    cg.add(var.set_data(template_))

    return var


@automation.register_action(
    "thingsboard.send_attributes",
    ThingsBoardSendAttributesAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ThingsBoardComponent),
            cv.Required(CONF_DATA): cv.templatable(cv.string),
        }
    ),
)
async def thingsboard_send_attributes_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    template_ = await cg.templatable(config[CONF_DATA], args, cg.std_string)
    cg.add(var.set_data(template_))

    return var


@automation.register_action(
    "thingsboard.clear_token",
    ThingsBoardClearTokenAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ThingsBoardComponent),
        }
    ),
)
async def thingsboard_clear_token_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "thingsboard.set_token",
    ThingsBoardSetTokenAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ThingsBoardComponent),
            cv.Required(CONF_TOKEN): cv.templatable(cv.string),
        }
    ),
)
async def thingsboard_set_token_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_TOKEN], args, cg.std_string)
    cg.add(var.set_token(template_))
    return var


@automation.register_action(
    "thingsboard.claim_device",
    ThingsBoardClaimDeviceAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ThingsBoardComponent),
            cv.Optional(CONF_CLAIM_SECRET_KEY): cv.templatable(cv.string),
            cv.Optional(CONF_CLAIM_DURATION_MS): cv.templatable(
                cv.positive_time_period_milliseconds
            ),
        }
    ),
)
async def thingsboard_claim_device_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    if CONF_CLAIM_SECRET_KEY in config:
        template_ = await cg.templatable(
            config[CONF_CLAIM_SECRET_KEY], args, cg.std_string
        )
        cg.add(var.set_secret_key(template_))
    if CONF_CLAIM_DURATION_MS in config:
        template_ = await cg.templatable(
            config[CONF_CLAIM_DURATION_MS], args, cg.uint32
        )
        cg.add(var.set_duration_ms(template_))

    return var


@automation.register_action(
    "thingsboard.send_rpc_request",
    ThingsBoardSendRpcRequestAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ThingsBoardComponent),
            cv.Required(CONF_METHOD): cv.templatable(cv.string),
            cv.Optional(CONF_PARAMS, default="{}"): cv.templatable(cv.string),
        }
    ),
)
async def thingsboard_send_rpc_request_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    method_template = await cg.templatable(config[CONF_METHOD], args, cg.std_string)
    cg.add(var.set_method(method_template))

    params_template = await cg.templatable(config[CONF_PARAMS], args, cg.std_string)
    cg.add(var.set_params(params_template))

    return var


@automation.register_action(
    "thingsboard.request_attributes",
    ThingsBoardRequestAttributesAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ThingsBoardComponent),
            cv.Required(CONF_KEYS): cv.templatable(cv.string),
        }
    ),
)
async def thingsboard_request_attributes_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    keys_template = await cg.templatable(config[CONF_KEYS], args, cg.std_string)
    cg.add(var.set_keys(keys_template))

    return var
