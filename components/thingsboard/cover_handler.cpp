#include "cover_handler.h"

#ifdef USE_COVER

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.cover";

RpcResult CoverHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_covers(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Cover not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "control") {
    auto call = obj->make_call();
    if (params.containsKey("command"))
      call.set_command(params["command"].as<std::string>().c_str());
    if (params.containsKey("position"))
      call.set_position(params["position"].as<float>());
    if (params.containsKey("tilt"))
      call.set_tilt(params["tilt"].as<float>());
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
  } else if (method == "set_tilt") {
    if (!params.containsKey("tilt")) return {ESP_ERR_INVALID_ARG, ""};
    auto call = obj->make_call();
    call.set_tilt(params["tilt"].as<float>());
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown cover method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Cover %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, this->build_state_json(obj)};
}

void CoverHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_covers()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      // Try JSON first
      bool parsed = json::parse_json(value, [obj](JsonObject root) -> bool {
        auto call = obj->make_call();
        if (root.containsKey("position"))
          call.set_position(root["position"].as<float>());
        if (root.containsKey("tilt"))
          call.set_tilt(root["tilt"].as<float>());
        call.perform();
        return true;
      });
      if (!parsed) {
        // Plain float = position
        auto call = obj->make_call();
        call.set_position(std::stof(value));
        call.perform();
      }
    });
  }
}

size_t CoverHandler::entity_count() const {
  return count_entities(App.get_covers());
}

void CoverHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_covers()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void CoverHandler::append_domain_info(JsonObject obj) const {
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
  methods.add("set_tilt");
  domain["count"] = count;
}

void CoverHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_covers()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    auto cover_traits = obj->get_traits();
    traits["supports_position"] = cover_traits.get_supports_position();
    traits["supports_tilt"] = cover_traits.get_supports_tilt();
    traits["supports_stop"] = cover_traits.get_supports_stop();
    traits["supports_toggle"] = cover_traits.get_supports_toggle();
    traits["is_assumed_state"] = cover_traits.get_is_assumed_state();
  }
}

void CoverHandler::append_telemetry_fields(EntityBase *obj_base,
                                           const TelemetryEmit &emit) {
  auto *obj = static_cast<cover::Cover *>(obj_base);
  emit_field(emit, "position", obj->position);
  emit_field(emit, "tilt", obj->tilt);
  emit_field(emit, "current_operation",
             LOG_STR_ARG(cover::cover_operation_to_str(obj->current_operation)));
}

std::string CoverHandler::build_state_json(cover::Cover *obj) {
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
