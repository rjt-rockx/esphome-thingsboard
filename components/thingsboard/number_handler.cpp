#include "number_handler.h"

#ifdef USE_NUMBER

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.number";

RpcResult NumberHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_numbers(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Number not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "set" || method == "control") {
    if (!params["value"].is<float>()) {
      ESP_LOGW(TAG, "Missing 'value' parameter for number.%s", method.c_str());
      return {ESP_ERR_INVALID_ARG, ""};
    }
    float value = params["value"].as<float>();
    auto call = obj->make_call();
    call.set_value(value);
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown number method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Number %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, build_state_json(obj)};
}

void NumberHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_numbers()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      auto call = obj->make_call();
      call.set_value(std::stof(value));
      call.perform();
    });
  }
}

size_t NumberHandler::entity_count() const {
  return count_entities(App.get_numbers());
}

void NumberHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_numbers()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void NumberHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("set");
  domain["count"] = count;
}

void NumberHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_numbers()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    traits["min_value"] = obj->traits.get_min_value();
    traits["max_value"] = obj->traits.get_max_value();
    traits["step"] = obj->traits.get_step();
  }
}

std::string NumberHandler::build_state_json(number::Number *obj) {
  return json::build_json([obj](JsonObject root) {
    if (!std::isnan(obj->state)) {
      root["value"] = obj->state;
    }
  });
}

}  // namespace thingsboard
}  // namespace esphome

#endif
