#include "fan_handler.h"

#ifdef USE_FAN

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.fan";

RpcResult FanHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_fans(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Fan not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "control") {
    auto call = obj->make_call();
    if (params.containsKey("state"))
      call.set_state(params["state"].as<bool>());
    if (params.containsKey("speed"))
      call.set_speed(params["speed"].as<int>());
    if (params.containsKey("oscillating"))
      call.set_oscillating(params["oscillating"].as<bool>());
    if (params.containsKey("direction")) {
      std::string dir = params["direction"].as<std::string>();
      call.set_direction(dir == "REVERSE" ? fan::FanDirection::REVERSE : fan::FanDirection::FORWARD);
    }
    if (params.containsKey("preset_mode"))
      call.set_preset_mode(params["preset_mode"].as<std::string>());
    call.perform();
  } else if (method == "turn_on") {
    auto call = obj->turn_on();
    if (params.containsKey("speed"))
      call.set_speed(params["speed"].as<int>());
    call.perform();
  } else if (method == "turn_off") {
    auto call = obj->turn_off();
    call.perform();
  } else if (method == "toggle") {
    auto call = obj->toggle();
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown fan method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Fan %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, build_state_json(obj)};
}

void FanHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_fans()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      if (value == "ON" || value == "true" || value == "1") {
        auto call = obj->turn_on();
        call.perform();
      } else if (value == "OFF" || value == "false" || value == "0") {
        auto call = obj->turn_off();
        call.perform();
      } else {
        // Try JSON
        json::parse_json(value, [obj](JsonObject root) -> bool {
          auto call = obj->make_call();
          if (root.containsKey("state"))
            call.set_state(root["state"].as<bool>());
          if (root.containsKey("speed"))
            call.set_speed(root["speed"].as<int>());
          call.perform();
          return true;
        });
      }
    });
  }
}

size_t FanHandler::entity_count() const {
  return count_entities(App.get_fans());
}

void FanHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_fans()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void FanHandler::append_domain_info(JsonObject obj) const {
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

void FanHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_fans()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    auto fan_traits = obj->get_traits();
    traits["supports_oscillation"] = fan_traits.supports_oscillation();
    traits["supports_speed"] = fan_traits.supports_speed();
    traits["supported_speed_count"] = fan_traits.supported_speed_count();
    traits["supports_direction"] = fan_traits.supports_direction();
    if (fan_traits.supports_preset_modes()) {
      JsonArray presets = traits["supported_preset_modes"].to<JsonArray>();
      for (const auto *pm : fan_traits.supported_preset_modes()) {
        presets.add(pm);
      }
    }
  }
}

std::string FanHandler::build_state_json(fan::Fan *obj) {
  return json::build_json([obj](JsonObject root) {
    root["state"] = obj->state ? "ON" : "OFF";
    root["speed"] = obj->speed;
    root["oscillating"] = obj->oscillating;
    root["direction"] = LOG_STR_ARG(fan::fan_direction_to_string(obj->direction));
    if (obj->has_preset_mode())
      root["preset_mode"] = obj->get_preset_mode();
  });
}

}  // namespace thingsboard
}  // namespace esphome

#endif
