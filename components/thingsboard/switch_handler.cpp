#include "switch_handler.h"

#ifdef USE_SWITCH

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.switch";

RpcResult SwitchHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_switches(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Switch not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "turn_on") {
    obj->turn_on();
  } else if (method == "turn_off") {
    obj->turn_off();
  } else if (method == "toggle") {
    obj->toggle();
  } else if (method == "control") {
    if (params["state"].is<const char*>()) {
      std::string state = params["state"].as<std::string>();
      if (state == "ON" || state == "true" || state == "1") {
        obj->turn_on();
      } else {
        obj->turn_off();
      }
    }
  } else {
    ESP_LOGW(TAG, "Unknown switch method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Switch %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, build_state_json(obj)};
}

void SwitchHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_switches()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      if (value == "ON" || value == "true" || value == "1") {
        obj->turn_on();
      } else if (value == "OFF" || value == "false" || value == "0") {
        obj->turn_off();
      }
    });
  }
}

size_t SwitchHandler::entity_count() const {
  return count_entities(App.get_switches());
}

void SwitchHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_switches()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void SwitchHandler::append_domain_info(JsonObject obj) const {
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

void SwitchHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_switches()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    entity["traits"].to<JsonObject>();
  }
}

std::string SwitchHandler::build_state_json(switch_::Switch *obj) {
  return json::build_json([obj](JsonObject root) {
    root["state"] = obj->state ? "ON" : "OFF";
  });
}

}  // namespace thingsboard
}  // namespace esphome

#endif
