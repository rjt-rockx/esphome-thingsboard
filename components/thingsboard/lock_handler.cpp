#include "lock_handler.h"

#ifdef USE_LOCK

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.lock";

RpcResult LockHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_locks(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Lock not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "control") {
    if (!params.containsKey("state")) return {ESP_ERR_INVALID_ARG, ""};
    auto call = obj->make_call();
    call.set_state(params["state"].as<std::string>());
    call.perform();
  } else if (method == "lock") {
    obj->lock();
  } else if (method == "unlock") {
    obj->unlock();
  } else if (method == "open") {
    obj->open();
  } else {
    ESP_LOGW(TAG, "Unknown lock method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Lock %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, this->build_state_json(obj)};
}

void LockHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_locks()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      if (value == "LOCKED" || value == "locked") {
        obj->lock();
      } else if (value == "UNLOCKED" || value == "unlocked") {
        obj->unlock();
      }
    });
  }
}

size_t LockHandler::entity_count() const {
  return count_entities(App.get_locks());
}

void LockHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_locks()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void LockHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("lock");
  methods.add("unlock");
  methods.add("open");
  domain["count"] = count;
}

void LockHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_locks()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    traits["supports_open"] = obj->traits.get_supports_open();
    traits["requires_code"] = obj->traits.get_requires_code();
    traits["assumed_state"] = obj->traits.get_assumed_state();
  }
}

void LockHandler::append_telemetry_fields(EntityBase *obj_base,
                                          const TelemetryEmit &emit) {
  auto *obj = static_cast<lock::Lock *>(obj_base);
  emit_field(emit, "state", LOG_STR_ARG(lock::lock_state_to_string(obj->state)));
}

std::string LockHandler::build_state_json(lock::Lock *obj) {
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
