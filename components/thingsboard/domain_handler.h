#pragma once

#include "esphome/core/application.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"
#include <string>
#include <functional>

namespace esphome {
namespace thingsboard {

struct RpcResult {
  esp_err_t err;
  std::string state_json;
};

using register_fn = std::function<void(const std::string &key, std::function<void(const std::string &)>)>;

class DomainHandler {
 public:
  virtual ~DomainHandler() = default;

  virtual const char *domain() const = 0;

  virtual RpcResult handle_rpc(const std::string &method,
                               const std::string &entity_id,
                               JsonObject params) = 0;

  virtual void register_shared_attributes(register_fn reg) = 0;

  virtual size_t entity_count() const = 0;

  virtual void append_entity_ids(JsonArray arr) const = 0;

  virtual void append_domain_info(JsonObject obj) const = 0;

  virtual void append_entity_discovery(JsonArray arr) const = 0;
};

/// Find a non-internal entity by object_id in the given container
template<typename Container>
typename Container::value_type find_entity(const Container &entities, const std::string &object_id) {
  for (auto *obj : entities) {
    if (obj->is_internal()) continue;
    char buf[OBJECT_ID_MAX_LEN];
    if (obj->get_object_id_to(buf) == object_id) {
      return obj;
    }
  }
  return nullptr;
}

/// Count non-internal entities in the given container
template<typename Container>
size_t count_entities(const Container &entities) {
  size_t count = 0;
  for (auto *obj : entities) {
    if (!obj->is_internal()) count++;
  }
  return count;
}

}  // namespace thingsboard
}  // namespace esphome
