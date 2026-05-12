"""ThingsBoard HTTP transport.

Implements the ThingsBoard HTTP device API
(<https://thingsboard.io/docs/reference/http-api/>): telemetry POST,
attribute upload, attribute long-poll, server-side RPC long-poll,
client-side RPC, claim, provisioning.

End firmware uses either ``thingsboard_mqtt:`` or ``thingsboard_http:``,
never both. The core component enforces this via FINAL_VALIDATE_SCHEMA.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import http_request
from esphome.const import CONF_ID
from esphome.core import coroutine_with_priority

from .. import thingsboard as tb_core

CODEOWNERS = ["@rjt-rockx"]
DEPENDENCIES = ["wifi", "thingsboard", "http_request"]

CONF_THINGSBOARD_ID = "thingsboard_id"
CONF_HTTP_REQUEST_ID = "http_request_id"
CONF_DEVICE_TOKEN = "device_token"
CONF_PROVISIONING = "provisioning"
CONF_PROVISIONING_KEY = "key"
CONF_PROVISIONING_SECRET = "secret"
CONF_POLL_INTERVAL = "poll_interval"
CONF_POLL_TIMEOUT = "poll_timeout"
CONF_CREDENTIALS = "credentials"
CONF_CREDENTIALS_TYPE = "type"
CONF_ACCESS_TOKEN = "access_token"

CREDENTIAL_ACCESS_TOKEN = "ACCESS_TOKEN"

thingsboard_http_ns = cg.esphome_ns.namespace("thingsboard_http")
ThingsBoardHttpTransport = thingsboard_http_ns.class_(
    "ThingsBoardHttpTransport", cg.Component
)


def _credentials_schema():
    return cv.Schema(
        {
            cv.Optional(
                CONF_CREDENTIALS_TYPE, default=CREDENTIAL_ACCESS_TOKEN
            ): cv.one_of(CREDENTIAL_ACCESS_TOKEN, upper=True),
            cv.Optional(CONF_ACCESS_TOKEN): cv.string,
        }
    )


PROVISIONING_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_PROVISIONING_KEY): cv.string,
        cv.Required(CONF_PROVISIONING_SECRET): cv.string,
        cv.Optional(CONF_CREDENTIALS): _credentials_schema(),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ThingsBoardHttpTransport),
            cv.GenerateID(CONF_THINGSBOARD_ID): cv.use_id(
                tb_core.ThingsBoardComponent
            ),
            cv.Optional(CONF_HTTP_REQUEST_ID): cv.use_id(
                http_request.HttpRequestComponent
            ),
            cv.Optional(CONF_DEVICE_TOKEN): cv.string,
            cv.Optional(CONF_PROVISIONING): PROVISIONING_SCHEMA,
            cv.Optional(
                CONF_POLL_INTERVAL, default="5s"
            ): cv.positive_time_period_milliseconds,
            # Server-side long-poll window for /rpc and /attributes/updates.
            # 0 = short-poll (TB returns immediately, no server-side wait).
            # The synchronous esp_http_client call blocks loopTask, so any
            # non-zero value here MUST be < ESP_TASK_WDT_TIMEOUT_S (5s default).
            cv.Optional(
                CONF_POLL_TIMEOUT, default="0s"
            ): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.has_at_least_one_key(CONF_DEVICE_TOKEN, CONF_PROVISIONING),
)


@coroutine_with_priority(0.9)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    tb = await cg.get_variable(config[CONF_THINGSBOARD_ID])
    cg.add(var.set_parent_thingsboard(tb))

    if CONF_HTTP_REQUEST_ID in config:
        http = await cg.get_variable(config[CONF_HTTP_REQUEST_ID])
        cg.add(var.set_http(http))

    if CONF_DEVICE_TOKEN in config:
        cg.add(var.set_device_token(config[CONF_DEVICE_TOKEN]))
        cg.add(tb.set_device_token(config[CONF_DEVICE_TOKEN]))

    if CONF_PROVISIONING in config:
        prov = config[CONF_PROVISIONING]
        cg.add(tb.set_provisioning_key(prov[CONF_PROVISIONING_KEY]))
        cg.add(tb.set_provisioning_secret(prov[CONF_PROVISIONING_SECRET]))
        if CONF_CREDENTIALS in prov:
            creds = prov[CONF_CREDENTIALS]
            ctype = creds.get(CONF_CREDENTIALS_TYPE, CREDENTIAL_ACCESS_TOKEN)
            cg.add(tb.set_provisioning_credentials_type(ctype))
            if CONF_ACCESS_TOKEN in creds:
                cg.add(
                    tb.set_provisioning_credentials_token(
                        creds[CONF_ACCESS_TOKEN]
                    )
                )

    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_poll_timeout_ms(config[CONF_POLL_TIMEOUT]))

    cg.add_define("USE_THINGSBOARD_HTTP_TRANSPORT")
