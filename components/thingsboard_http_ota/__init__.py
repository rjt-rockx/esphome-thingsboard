import esphome.codegen as cg

CODEOWNERS = ["@rjt-rockx"]
DEPENDENCIES = ["thingsboard", "http_request"]

thingsboard_http_ota_ns = cg.esphome_ns.namespace("thingsboard_http_ota")
