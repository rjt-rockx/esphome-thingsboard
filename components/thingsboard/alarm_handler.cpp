#include "alarm_handler.h"

#ifdef USE_ALARM_CONTROL_PANEL

#include "esphome/components/alarm_control_panel/alarm_control_panel_state.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.alarm";

RpcResult AlarmHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_alarm_control_panels(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Alarm control panel not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  optional<std::string> code;
  if (params.containsKey("code"))
    code = params["code"].as<std::string>();

  if (method == "control") {
    if (!params.containsKey("action")) return {ESP_ERR_INVALID_ARG, ""};
    std::string action = params["action"].as<std::string>();
    if (action == "arm_away") obj->arm_away(code);
    else if (action == "arm_home") obj->arm_home(code);
    else if (action == "arm_night") obj->arm_night(code);
    else if (action == "arm_vacation") obj->arm_vacation(code);
    else if (action == "arm_custom_bypass") obj->arm_custom_bypass(code);
    else if (action == "disarm") obj->disarm(code);
    else return {ESP_ERR_INVALID_ARG, ""};
  } else if (method == "arm_away") {
    obj->arm_away(code);
  } else if (method == "arm_home") {
    obj->arm_home(code);
  } else if (method == "arm_night") {
    obj->arm_night(code);
  } else if (method == "arm_vacation") {
    obj->arm_vacation(code);
  } else if (method == "arm_custom_bypass") {
    obj->arm_custom_bypass(code);
  } else if (method == "disarm") {
    obj->disarm(code);
  } else {
    ESP_LOGW(TAG, "Unknown alarm method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Alarm %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, this->build_state_json(obj)};
}

void AlarmHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_alarm_control_panels()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      if (value == "arm_away") obj->arm_away();
      else if (value == "arm_home") obj->arm_home();
      else if (value == "arm_night") obj->arm_night();
      else if (value == "disarm") obj->disarm();
    });
  }
}

size_t AlarmHandler::entity_count() const {
  return count_entities(App.get_alarm_control_panels());
}

void AlarmHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_alarm_control_panels()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void AlarmHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("arm_away");
  methods.add("arm_home");
  methods.add("arm_night");
  methods.add("arm_vacation");
  methods.add("arm_custom_bypass");
  methods.add("disarm");
  domain["count"] = count;
}

void AlarmHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_alarm_control_panels()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    traits["supported_features"] = obj->get_supported_features();
    traits["requires_code"] = obj->get_requires_code();
    traits["requires_code_to_arm"] = obj->get_requires_code_to_arm();
  }
}

void AlarmHandler::append_telemetry_fields(EntityBase *obj_base,
                                           const TelemetryEmit &emit) {
  auto *obj = static_cast<alarm_control_panel::AlarmControlPanel *>(obj_base);
  emit_field(emit, "state",
             LOG_STR_ARG(alarm_control_panel::alarm_control_panel_state_to_string(
                 obj->get_state())));
}

std::string AlarmHandler::build_state_json(alarm_control_panel::AlarmControlPanel *obj) {
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
