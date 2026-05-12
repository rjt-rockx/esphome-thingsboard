"""ThingsBoard MQTT transport. See https://thingsboard.io/docs/reference/mqtt-api/."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import (
    add_idf_component,
    idf_version,
    include_builtin_idf_component,
)
from esphome.const import (
    CONF_BROKER,
    CONF_CLIENT_ID,
    CONF_PASSWORD,
    CONF_PORT,
    CONF_USERNAME,
)
from esphome.core import CORE, coroutine_with_priority

from .. import thingsboard as tb_core

CODEOWNERS = ["@rjt-rockx"]
DEPENDENCIES = ["wifi", "thingsboard"]

CONF_THINGSBOARD_ID = "thingsboard_id"
CONF_DEVICE_TOKEN = "device_token"
CONF_PROVISIONING = "provisioning"
CONF_PROVISIONING_KEY = "key"
CONF_PROVISIONING_SECRET = "secret"
CONF_CREDENTIALS = "credentials"
CONF_CREDENTIALS_TYPE = "type"
CONF_ACCESS_TOKEN = "access_token"
CONF_CERTIFICATE_PEM = "certificate_pem"
CONF_PRIVATE_KEY_PEM = "private_key_pem"

thingsboard_mqtt_ns = cg.esphome_ns.namespace("thingsboard_mqtt")

CREDENTIAL_ACCESS_TOKEN = "ACCESS_TOKEN"
CREDENTIAL_MQTT_BASIC = "MQTT_BASIC"
CREDENTIAL_X509 = "X509_CERTIFICATE"


def _credentials_schema(*, allow_token=True):
    schema = {
        cv.Optional(CONF_CREDENTIALS_TYPE, default=CREDENTIAL_ACCESS_TOKEN): cv.one_of(
            CREDENTIAL_ACCESS_TOKEN,
            CREDENTIAL_MQTT_BASIC,
            CREDENTIAL_X509,
            upper=True,
        ),
        cv.Optional(CONF_ACCESS_TOKEN): cv.string,
        cv.Optional(CONF_CLIENT_ID): cv.string,
        cv.Optional(CONF_USERNAME): cv.string,
        cv.Optional(CONF_PASSWORD): cv.string,
        cv.Optional(CONF_CERTIFICATE_PEM): cv.string,
        cv.Optional(CONF_PRIVATE_KEY_PEM): cv.string,
    }
    if not allow_token:
        schema.pop(cv.Optional(CONF_ACCESS_TOKEN), None)
    return cv.Schema(schema)


# See https://thingsboard.io/docs/user-guide/device-provisioning/
PROVISIONING_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_PROVISIONING_KEY): cv.string,
        cv.Required(CONF_PROVISIONING_SECRET): cv.string,
        cv.Optional(CONF_CREDENTIALS): _credentials_schema(),
    }
)

# If both device_token and provisioning are set, device_token is preferred;
# provisioning is the fallback on auth failure.
CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_THINGSBOARD_ID): cv.use_id(tb_core.ThingsBoardComponent),
            cv.Required(CONF_BROKER): cv.string,
            cv.Optional(CONF_PORT, default=1883): cv.port,
            cv.Optional(CONF_DEVICE_TOKEN): cv.string,
            cv.Optional(CONF_PROVISIONING): PROVISIONING_SCHEMA,
            cv.Optional(CONF_CREDENTIALS): _credentials_schema(allow_token=False),
        }
    ),
    cv.has_at_least_one_key(CONF_DEVICE_TOKEN, CONF_PROVISIONING, CONF_CREDENTIALS),
)


@coroutine_with_priority(0.9)
async def to_code(config):
    # ESP-IDF's mqtt component is excluded from the build by default; pull it in
    # so the ESP-MQTT client wrapper can include <mqtt_client.h>.
    if CORE.is_esp32:
        if idf_version() >= cv.Version(6, 0, 0):
            add_idf_component(name="espressif/mqtt", ref="1.0.0")
        else:
            include_builtin_idf_component("mqtt")

    tb = await cg.get_variable(config[CONF_THINGSBOARD_ID])

    cg.add(tb.set_mqtt_broker(config[CONF_BROKER]))
    cg.add(tb.set_mqtt_port(config[CONF_PORT]))

    if CONF_DEVICE_TOKEN in config:
        cg.add(tb.set_device_token(config[CONF_DEVICE_TOKEN]))

    if CONF_PROVISIONING in config:
        prov = config[CONF_PROVISIONING]
        cg.add(tb.set_provisioning_key(prov[CONF_PROVISIONING_KEY]))
        cg.add(tb.set_provisioning_secret(prov[CONF_PROVISIONING_SECRET]))
        # Server-generated ACCESS_TOKEN is the default when no credentials block
        # is present; only push device-supplied credentials when the YAML sets them.
        if CONF_CREDENTIALS in prov:
            creds = prov[CONF_CREDENTIALS]
            ctype = creds.get(CONF_CREDENTIALS_TYPE, CREDENTIAL_ACCESS_TOKEN)
            cg.add(tb.set_provisioning_credentials_type(ctype))
            if ctype == CREDENTIAL_ACCESS_TOKEN:
                if CONF_ACCESS_TOKEN in creds:
                    cg.add(
                        tb.set_provisioning_credentials_token(
                            creds[CONF_ACCESS_TOKEN]
                        )
                    )
            elif ctype == CREDENTIAL_MQTT_BASIC:
                if CONF_CLIENT_ID in creds:
                    cg.add(
                        tb.set_provisioning_credentials_client_id(
                            creds[CONF_CLIENT_ID]
                        )
                    )
                if CONF_USERNAME in creds:
                    cg.add(
                        tb.set_provisioning_credentials_username(
                            creds[CONF_USERNAME]
                        )
                    )
                if CONF_PASSWORD in creds:
                    cg.add(
                        tb.set_provisioning_credentials_password(
                            creds[CONF_PASSWORD]
                        )
                    )
            elif ctype == CREDENTIAL_X509:
                if CONF_CERTIFICATE_PEM in creds:
                    cg.add(
                        tb.set_provisioning_credentials_cert_pem(
                            creds[CONF_CERTIFICATE_PEM]
                        )
                    )

    # Signals to the core component's final-validator that an MQTT transport is registered.
    cg.add_define("USE_THINGSBOARD_MQTT_TRANSPORT")
