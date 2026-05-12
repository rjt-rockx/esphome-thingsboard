#include "button_handler.h"

#ifdef USE_BUTTON

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.button";

RpcResult ButtonHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_buttons(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Button not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "press") {
    obj->press();
  } else {
    ESP_LOGW(TAG, "Unknown button method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Button %s: press", entity_id.c_str());
  return {ESP_OK, "{\"pressed\":true}"};
}

void ButtonHandler::register_shared_attributes(register_fn reg) {
  // Buttons are press-only, no shared attribute control
}

size_t ButtonHandler::entity_count() const {
  return count_entities(App.get_buttons());
}

void ButtonHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_buttons()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void ButtonHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("press");
  domain["count"] = count;
}

void ButtonHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_buttons()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    entity["traits"].to<JsonObject>();
  }
}

}  // namespace thingsboard
}  // namespace esphome

#endif
