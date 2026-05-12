#include "text_handler.h"

#ifdef USE_TEXT

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.text";

RpcResult TextHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_texts(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Text not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "set" || method == "control") {
    if (!params.containsKey("value")) {
      ESP_LOGW(TAG, "Missing 'value' parameter for text.%s", method.c_str());
      return {ESP_ERR_INVALID_ARG, ""};
    }
    auto call = obj->make_call();
    call.set_value(params["value"].as<std::string>());
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown text method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Text %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, build_state_json(obj)};
}

void TextHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_texts()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      auto call = obj->make_call();
      call.set_value(value);
      call.perform();
    });
  }
}

size_t TextHandler::entity_count() const {
  return count_entities(App.get_texts());
}

void TextHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_texts()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void TextHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("set");
  domain["count"] = count;
}

void TextHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_texts()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    traits["min_length"] = obj->traits.get_min_length();
    traits["max_length"] = obj->traits.get_max_length();
    std::string pattern = obj->traits.get_pattern();
    if (!pattern.empty())
      traits["pattern"] = pattern;
    traits["mode"] = static_cast<uint8_t>(obj->traits.get_mode());
  }
}

std::string TextHandler::build_state_json(text::Text *obj) {
  return json::build_json([obj](JsonObject root) {
    root["value"] = obj->state;
  });
}

}  // namespace thingsboard
}  // namespace esphome

#endif
