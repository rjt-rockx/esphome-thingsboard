#pragma once

#include "esphome/core/defines.h"

#ifdef USE_VALVE

#include "domain_handler.h"
#include "esphome/components/valve/valve.h"

namespace esphome {
namespace thingsboard {

class ValveHandler : public DomainHandler {
 public:
  const char *domain() const override { return "valve"; }
  RpcResult handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) override;
  void register_shared_attributes(register_fn reg) override;
  size_t entity_count() const override;
  void append_entity_ids(JsonArray arr) const override;
  void append_domain_info(JsonObject obj) const override;
  void append_entity_discovery(JsonArray arr) const override;
  void append_telemetry_fields(EntityBase *obj,
                               const TelemetryEmit &emit) override;

 protected:
  std::string build_state_json(valve::Valve *obj);
};

}  // namespace thingsboard
}  // namespace esphome

#endif
