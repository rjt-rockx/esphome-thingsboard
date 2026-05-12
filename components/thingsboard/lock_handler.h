#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LOCK

#include "domain_handler.h"
#include "esphome/components/lock/lock.h"

namespace esphome {
namespace thingsboard {

class LockHandler : public DomainHandler {
 public:
  const char *domain() const override { return "lock"; }
  RpcResult handle_rpc(const std::string &method, const std::string &entity_id, JsonObject params) override;
  void register_shared_attributes(register_fn reg) override;
  size_t entity_count() const override;
  void append_entity_ids(JsonArray arr) const override;
  void append_domain_info(JsonObject obj) const override;
  void append_entity_discovery(JsonArray arr) const override;

 protected:
  static std::string build_state_json(lock::Lock *obj);
};

}  // namespace thingsboard
}  // namespace esphome

#endif
