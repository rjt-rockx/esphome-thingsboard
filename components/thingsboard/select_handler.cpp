#include "select_handler.h"

#ifdef USE_SELECT

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.select";

RpcResult SelectHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_selects(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Select not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "set" || method == "control") {
    if (!params.containsKey("option")) {
      ESP_LOGW(TAG, "Missing 'option' parameter for select.%s", method.c_str());
      return {ESP_ERR_INVALID_ARG, ""};
    }
    std::string option = params["option"].as<std::string>();
    auto call = obj->make_call();
    call.set_option(option);
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown select method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Select %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, build_state_json(obj)};
}

void SelectHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_selects()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      auto call = obj->make_call();
      call.set_option(value);
      call.perform();
    });
  }
}

size_t SelectHandler::entity_count() const {
  return count_entities(App.get_selects());
}

void SelectHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_selects()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void SelectHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("set");
  domain["count"] = count;
}

void SelectHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_selects()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    JsonArray options = traits["options"].to<JsonArray>();
    for (const auto &option : obj->traits.get_options()) {
      options.add(option);
    }
  }
}

std::string SelectHandler::build_state_json(select::Select *obj) {
  return json::build_json([obj](JsonObject root) {
    root["option"] = obj->state;
  });
}

}  // namespace thingsboard
}  // namespace esphome

#endif
