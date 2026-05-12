#include "light_handler.h"

#ifdef USE_LIGHT

#include "esphome/components/light/light_traits.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.light";

RpcResult LightHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_lights(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Light not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "turn_on" || method == "control") {
    auto call = obj->turn_on();
    if (params["brightness"].is<float>()) {
      float brightness = params["brightness"].as<float>() / 255.0f;
      call.set_brightness(brightness);
    }
    if (params["r"].is<float>() && params["g"].is<float>() && params["b"].is<float>()) {
      call.set_red(params["r"].as<float>() / 255.0f);
      call.set_green(params["g"].as<float>() / 255.0f);
      call.set_blue(params["b"].as<float>() / 255.0f);
    }
    if (params["color_temp"].is<float>()) {
      call.set_color_temperature(params["color_temp"].as<float>());
    }
    if (params["effect"].is<const char*>()) {
      call.set_effect(params["effect"].as<std::string>());
    }
    if (params["state"].is<const char*>()) {
      std::string state = params["state"].as<std::string>();
      if (state == "OFF" || state == "false" || state == "0") {
        call = obj->turn_off();
      }
    }
    call.perform();
  } else if (method == "turn_off") {
    auto call = obj->turn_off();
    call.perform();
  } else if (method == "toggle") {
    auto call = obj->toggle();
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown light method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Light %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, this->build_state_json(obj)};
}

void LightHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_lights()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      if (value == "ON" || value == "true" || value == "1") {
        auto call = obj->turn_on();
        call.perform();
      } else if (value == "OFF" || value == "false" || value == "0") {
        auto call = obj->turn_off();
        call.perform();
      }
    });
  }
}

size_t LightHandler::entity_count() const {
  return count_entities(App.get_lights());
}

void LightHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_lights()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void LightHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("turn_on");
  methods.add("turn_off");
  methods.add("toggle");
  domain["count"] = count;
}

void LightHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_lights()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    auto light_traits = obj->get_traits();
    JsonArray color_modes = traits["supported_color_modes"].to<JsonArray>();
    for (auto mode : light_traits.get_supported_color_modes()) {
      color_modes.add(static_cast<uint8_t>(mode));
    }
    traits["min_mireds"] = light_traits.get_min_mireds();
    traits["max_mireds"] = light_traits.get_max_mireds();
  }
}

void LightHandler::append_telemetry_fields(EntityBase *obj_base,
                                           const TelemetryEmit &emit) {
  auto *obj = static_cast<light::LightState *>(obj_base);
  emit_field(emit, "state", obj->remote_values.is_on() ? "ON" : "OFF");
  emit_field(emit, "brightness",
             static_cast<int>(obj->remote_values.get_brightness() * 255));
}

std::string LightHandler::build_state_json(light::LightState *obj) {
  return json::build_json([this, obj](JsonObject root) {
    this->append_telemetry_fields(
        obj, [&root](const std::string &k, const std::string &v) {
          root[k] = serialized(v);
        });
  });
}

}  // namespace thingsboard
}  // namespace esphome

#endif
