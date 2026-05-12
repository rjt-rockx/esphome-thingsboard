#include "valve_handler.h"

#ifdef USE_VALVE

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.valve";

RpcResult ValveHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_valves(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Valve not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "control") {
    auto call = obj->make_call();
    if (params.containsKey("command"))
      call.set_command(params["command"].as<std::string>().c_str());
    if (params.containsKey("position"))
      call.set_position(params["position"].as<float>());
    call.perform();
  } else if (method == "open") {
    auto call = obj->make_call();
    call.set_command_open();
    call.perform();
  } else if (method == "close") {
    auto call = obj->make_call();
    call.set_command_close();
    call.perform();
  } else if (method == "stop") {
    auto call = obj->make_call();
    call.set_command_stop();
    call.perform();
  } else if (method == "toggle") {
    auto call = obj->make_call();
    call.set_command_toggle();
    call.perform();
  } else if (method == "set_position") {
    if (!params.containsKey("position")) return {ESP_ERR_INVALID_ARG, ""};
    auto call = obj->make_call();
    call.set_position(params["position"].as<float>());
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown valve method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Valve %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, this->build_state_json(obj)};
}

void ValveHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_valves()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      auto call = obj->make_call();
      call.set_position(std::stof(value));
      call.perform();
    });
  }
}

size_t ValveHandler::entity_count() const {
  return count_entities(App.get_valves());
}

void ValveHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_valves()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void ValveHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("open");
  methods.add("close");
  methods.add("stop");
  methods.add("toggle");
  methods.add("set_position");
  domain["count"] = count;
}

void ValveHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_valves()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    auto valve_traits = obj->get_traits();
    traits["supports_position"] = valve_traits.get_supports_position();
    traits["supports_stop"] = valve_traits.get_supports_stop();
    traits["supports_toggle"] = valve_traits.get_supports_toggle();
    traits["is_assumed_state"] = valve_traits.get_is_assumed_state();
  }
}

void ValveHandler::append_telemetry_fields(EntityBase *obj_base,
                                           const TelemetryEmit &emit) {
  auto *obj = static_cast<valve::Valve *>(obj_base);
  emit_field(emit, "position", obj->position);
  emit_field(emit, "current_operation",
             LOG_STR_ARG(valve::valve_operation_to_str(obj->current_operation)));
}

std::string ValveHandler::build_state_json(valve::Valve *obj) {
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
