import esphome.codegen as cg

CODEOWNERS = ["@rjt-rockx"]
DEPENDENCIES = ["thingsboard", "thingsboard_mqtt"]

thingsboard_mqtt_ota_ns = cg.esphome_ns.namespace("thingsboard_mqtt_ota")
