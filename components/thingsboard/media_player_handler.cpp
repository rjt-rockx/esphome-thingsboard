#include "media_player_handler.h"

#ifdef USE_MEDIA_PLAYER

#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.media_player";

RpcResult MediaPlayerHandler::handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) {
  auto *obj = find_entity(App.get_media_players(), entity_id);
  if (obj == nullptr) {
    ESP_LOGW(TAG, "Media player not found: %s", entity_id.c_str());
    return {ESP_ERR_NOT_FOUND, ""};
  }

  if (method == "control") {
    auto call = obj->make_call();
    if (params.containsKey("command"))
      call.set_command(params["command"].as<std::string>());
    if (params.containsKey("volume"))
      call.set_volume(params["volume"].as<float>());
    if (params.containsKey("media_url"))
      call.set_media_url(params["media_url"].as<std::string>());
    if (params.containsKey("announcement"))
      call.set_announcement(params["announcement"].as<bool>());
    call.perform();
  } else if (method == "play") {
    auto call = obj->make_call();
    call.set_command(media_player::MEDIA_PLAYER_COMMAND_PLAY);
    if (params.containsKey("media_url"))
      call.set_media_url(params["media_url"].as<std::string>());
    call.perform();
  } else if (method == "pause") {
    auto call = obj->make_call();
    call.set_command(media_player::MEDIA_PLAYER_COMMAND_PAUSE);
    call.perform();
  } else if (method == "stop") {
    auto call = obj->make_call();
    call.set_command(media_player::MEDIA_PLAYER_COMMAND_STOP);
    call.perform();
  } else if (method == "set_volume") {
    if (!params.containsKey("volume")) return {ESP_ERR_INVALID_ARG, ""};
    auto call = obj->make_call();
    call.set_volume(params["volume"].as<float>());
    call.perform();
  } else if (method == "turn_on") {
    auto call = obj->make_call();
    call.set_command(media_player::MEDIA_PLAYER_COMMAND_TURN_ON);
    call.perform();
  } else if (method == "turn_off") {
    auto call = obj->make_call();
    call.set_command(media_player::MEDIA_PLAYER_COMMAND_TURN_OFF);
    call.perform();
  } else {
    ESP_LOGW(TAG, "Unknown media_player method: %s", method.c_str());
    return {ESP_ERR_NOT_SUPPORTED, ""};
  }

  ESP_LOGD(TAG, "Media player %s: %s", entity_id.c_str(), method.c_str());
  return {ESP_OK, this->build_state_json(obj)};
}

void MediaPlayerHandler::register_shared_attributes(register_fn reg) {
  for (auto *obj : App.get_media_players()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    reg(obj->get_object_id_to(buf), [obj](const std::string &value) {
      // Try JSON
      json::parse_json(value, [obj](JsonObject root) -> bool {
        auto call = obj->make_call();
        if (root.containsKey("command"))
          call.set_command(root["command"].as<std::string>());
        if (root.containsKey("volume"))
          call.set_volume(root["volume"].as<float>());
        if (root.containsKey("media_url"))
          call.set_media_url(root["media_url"].as<std::string>());
        call.perform();
        return true;
      });
    });
  }
}

size_t MediaPlayerHandler::entity_count() const {
  return count_entities(App.get_media_players());
}

void MediaPlayerHandler::append_entity_ids(JsonArray arr) const {
  for (auto *obj : App.get_media_players()) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    arr.add(obj->get_object_id_to(buf));
  }
}

void MediaPlayerHandler::append_domain_info(JsonObject obj) const {
  size_t count = this->entity_count();
  if (count == 0) return;
  JsonObject domain = obj[this->domain()].to<JsonObject>();
  JsonArray methods = domain["methods"].to<JsonArray>();
  methods.add("control");
  methods.add("play");
  methods.add("pause");
  methods.add("stop");
  methods.add("set_volume");
  methods.add("turn_on");
  methods.add("turn_off");
  domain["count"] = count;
}

void MediaPlayerHandler::append_entity_discovery(JsonArray arr) const {
  for (auto *obj : App.get_media_players()) {
    if (obj->is_internal()) continue;
    JsonObject entity = arr.add<JsonObject>();
    char buf[OBJECT_ID_MAX_LEN];
    entity["id"] = obj->get_object_id_to(buf);
    entity["name"] = obj->get_name();
    JsonObject traits = entity["traits"].to<JsonObject>();
    auto mp_traits = obj->get_traits();
    traits["supports_pause"] = mp_traits.get_supports_pause();
    traits["supports_turn_off_on"] = mp_traits.get_supports_turn_off_on();
  }
}

void MediaPlayerHandler::append_telemetry_fields(EntityBase *obj_base,
                                                 const TelemetryEmit &emit) {
  auto *obj = static_cast<media_player::MediaPlayer *>(obj_base);
  emit_field(emit, "state", media_player::media_player_state_to_string(obj->state));
  emit_field(emit, "volume", obj->volume);
  emit_field(emit, "muted", obj->is_muted());
}

std::string MediaPlayerHandler::build_state_json(media_player::MediaPlayer *obj) {
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
