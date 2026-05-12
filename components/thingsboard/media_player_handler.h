#pragma once

#include "esphome/core/defines.h"

#ifdef USE_MEDIA_PLAYER

#include "domain_handler.h"
#include "esphome/components/media_player/media_player.h"

namespace esphome {
namespace thingsboard {

class MediaPlayerHandler : public DomainHandler {
 public:
  const char *domain() const override { return "media_player"; }
  RpcResult handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) override;
  void register_shared_attributes(register_fn reg) override;
  size_t entity_count() const override;
  void append_entity_ids(JsonArray arr) const override;
  void append_domain_info(JsonObject obj) const override;
  void append_entity_discovery(JsonArray arr) const override;
  void append_telemetry_fields(EntityBase *obj,
                               const TelemetryEmit &emit) override;

 protected:
  std::string build_state_json(media_player::MediaPlayer *obj);
};

}  // namespace thingsboard
}  // namespace esphome

#endif
