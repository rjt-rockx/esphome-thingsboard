import esphome.codegen as cg

CODEOWNERS = ["@rjt-rockx"]
DEPENDENCIES = ["thingsboard", "thingsboard_mqtt"]
AUTO_LOAD = ["sha256"]

thingsboard_mqtt_ota_ns = cg.esphome_ns.namespace("thingsboard_mqtt_ota")
