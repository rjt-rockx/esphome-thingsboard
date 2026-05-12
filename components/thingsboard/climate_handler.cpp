#include "climate_handler.h"

#ifdef USE_CLIMATE

#include "esphome/components/climate/climate_mode.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.climate";

RpcResult ClimateHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_climates(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Climate not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "control") {
    auto call = obj->make_call();
    if (params.containsKey("mode"))
      call.set_mode(params["mode"].as<std::string>());
    if (params.containsKey("target_temperature"))
      call.set_target_temperature(params["target_temperature"].as<float>());
    if (params.containsKey("target_temperature_low"))
      call.set_target_temperature_low(params["target_temperature_low"].as<float>());
    if (params.containsKey("target_temperature_high"))
      call.set_target_temperature_high(params["target_temperature_high"].as<float>());
    if (params.containsKey("target_humidity"))
      call.set_target_humidity(params["target_humidity"].as<float>());
    if (params.containsKey("fan_mode"))
      call.set_fan_mode(params["fan_mode"].as<std::string>());
    if (params.containsKey("swing_mode"))
      call.set_swing_mode(params["swing_mode"].as<std::string>());
    if (params.containsKey("preset"))
      call.set_preset(params["preset"].as<std::string>());
    call.perform();
  } else if (method == "set_mode") {
    if (!params.containsKey("mode")) return {ESP_ERR_INVALID_ARG, ""};
    auto call = obj->make_call();
    call.set_mode(params["mode"].as<std::string>());
    call.perform();
  } else if (method == "set_temperature") {
    auto call = obj->make_call();
    if (params.containsKey("target_temperature"))
      call.set_target_temperature(params["target_temperature"].as<float>());
    if (params.containsKey("target_temperature_low"))
      call.set_target_temperature_low(params["target_temperature_low"].as<float>());
    if (params.containsKey("target_temperature_high"))
      call.set_target_temperature_high(params["target_temperature_high"].as<float>());
    call.perform();
  } else if (method == "set_fan_mode") {
    if (!params.containsKey("fan_mode")) return {ESP_ERR_INVALID_ARG, ""};
    auto call = obj->make_call();
    call.set_fan_mode(params["fan_mode"].as<std::string>());
    call.perform();
  } else if (method == "set_swing_mode") {
    if (!params.containsKey("swing_mode")) return {ESP_ERR_INVALID_ARG, ""};
    auto call = obj->make_call();
    call.set_swing_mode(params["swing_mode"].as<std::string>());
    call.perform();
  } else if (method == "set_preset") {
    if (!params.containsKey("preset")) return {ESP_ERR_INVALID_ARG, ""};
    auto call = obj->make_call();
    call.set_preset(params["preset"].as<std::string>());
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown climate method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Climate %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, build_state_json(obj)};
}

void ClimateHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_climates()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      // Try parsing as JSON first for complex control
      bool parsed = json::parse_json(value, [obj](JsonObject root) -> bool {
        auto call = obj->make_call();
        if (root.containsKey("mode"))
          call.set_mode(root["mode"].as<std::string>());
        if (root.containsKey("target_temperature"))
          call.set_target_temperature(root["target_temperature"].as<float>());
        if (root.containsKey("fan_mode"))
          call.set_fan_mode(root["fan_mode"].as<std::string>());
        if (root.containsKey("preset"))
          call.set_preset(root["preset"].as<std::string>());
        call.perform();
        return true;
      });
      if (!parsed) {
        // Plain float value = set target temperature
        auto call = obj->make_call();
        call.set_target_temperature(std::stof(value));
        call.perform();
      }
    });
  }
}

size_t ClimateHandler::entity_count() const {
  return count_entities(App.get_climates());
}

void ClimateHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_climates()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void ClimateHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("set_mode");
  methods.add("set_temperature");
  methods.add("set_fan_mode");
  methods.add("set_swing_mode");
  methods.add("set_preset");
  domain["count"] = count;
}

void ClimateHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_climates()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();

    auto climate_traits = obj->get_traits();

    // Supported modes
    JsonArray modes = traits["supported_modes"].to<JsonArray>();
    for (auto mode : climate_traits.get_supported_modes()) {
      modes.add(LOG_STR_ARG(climate::climate_mode_to_string(mode)));
    }

    // Two-point target temperature
    traits["supports_two_point"] = climate_traits.has_feature_flags(climate::CLIMATE_SUPPORTS_TWO_POINT_TARGET_TEMPERATURE);
    traits["supports_target_humidity"] = climate_traits.has_feature_flags(climate::CLIMATE_SUPPORTS_TARGET_HUMIDITY);

    // Visual temperature range
    traits["visual_min_temperature"] = climate_traits.get_visual_min_temperature();
    traits["visual_max_temperature"] = climate_traits.get_visual_max_temperature();
    traits["visual_temperature_step"] = climate_traits.get_visual_target_temperature_step();

    // Fan modes
    if (climate_traits.get_supports_fan_modes()) {
      JsonArray fan_modes = traits["supported_fan_modes"].to<JsonArray>();
      for (auto fm : climate_traits.get_supported_fan_modes()) {
        fan_modes.add(LOG_STR_ARG(climate::climate_fan_mode_to_string(fm)));
      }
      for (const auto *cfm : climate_traits.get_supported_custom_fan_modes()) {
        fan_modes.add(cfm);
      }
    }

    // Swing modes
    if (climate_traits.get_supports_swing_modes()) {
      JsonArray swing_modes = traits["supported_swing_modes"].to<JsonArray>();
      for (auto sm : climate_traits.get_supported_swing_modes()) {
        swing_modes.add(LOG_STR_ARG(climate::climate_swing_mode_to_string(sm)));
      }
    }

    // Presets
    if (climate_traits.get_supports_presets()) {
      JsonArray presets = traits["supported_presets"].to<JsonArray>();
      for (auto p : climate_traits.get_supported_presets()) {
        presets.add(LOG_STR_ARG(climate::climate_preset_to_string(p)));
      }
      for (const auto *cp : climate_traits.get_supported_custom_presets()) {
        presets.add(cp);
      }
    }
  }
}

std::string ClimateHandler::build_state_json(climate::Climate *obj) {
  return json::build_json([obj](JsonObject root) {
    root["mode"] = LOG_STR_ARG(climate::climate_mode_to_string(obj->mode));
    root["action"] = LOG_STR_ARG(climate::climate_action_to_string(obj->action));
    if (!std::isnan(obj->current_temperature))
      root["current_temperature"] = obj->current_temperature;
    if (!std::isnan(obj->target_temperature))
      root["target_temperature"] = obj->target_temperature;
    if (!std::isnan(obj->target_temperature_low))
      root["target_temperature_low"] = obj->target_temperature_low;
    if (!std::isnan(obj->target_temperature_high))
      root["target_temperature_high"] = obj->target_temperature_high;
    if (obj->fan_mode.has_value())
      root["fan_mode"] = LOG_STR_ARG(climate::climate_fan_mode_to_string(*obj->fan_mode));
    if (obj->has_custom_fan_mode())
      root["custom_fan_mode"] = obj->get_custom_fan_mode();
    root["swing_mode"] = LOG_STR_ARG(climate::climate_swing_mode_to_string(obj->swing_mode));
    if (obj->preset.has_value())
      root["preset"] = LOG_STR_ARG(climate::climate_preset_to_string(*obj->preset));
    if (obj->has_custom_preset())
      root["custom_preset"] = obj->get_custom_preset();
    if (!std::isnan(obj->current_humidity))
      root["current_humidity"] = obj->current_humidity;
    if (!std::isnan(obj->target_humidity))
      root["target_humidity"] = obj->target_humidity;
  });
}

}  // namespace thingsboard
}  // namespace esphome

#endif
