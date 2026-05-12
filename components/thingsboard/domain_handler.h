#pragma once

#include "esphome/core/application.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <functional>

namespace esphome {
namespace thingsboard {

struct RpcResult {
  esp_err_t err;
  std::string state_json;
};

using register_fn = std::function<void(const std::string &key, std::function<void(const std::string &)>)>;

// Encodes a string into a JSON string literal (`"…"`) per RFC 8259.
inline std::string json_encode_string(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<uint8_t>(c));
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
  return out;
}

// Callback into the telemetry pipeline. `subkey` is appended to a domain-scoped
// id by the caller (e.g. handler emits "mode", caller writes
// "climate.living_room.mode"). `json_value` is a pre-encoded JSON literal that
// gets dropped into the outgoing payload verbatim.
using TelemetryEmit =
    std::function<void(const std::string &subkey, const std::string &json_value)>;

// Convenience wrappers that turn typed values into JSON literals before
// invoking `emit`. NaN floats are silently dropped (matches the omit-when-NaN
// guards already present in build_state_json).
inline void emit_field(const TelemetryEmit &emit, const char *subkey, float v) {
  if (std::isnan(v)) return;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  emit(subkey, buf);
}
inline void emit_field(const TelemetryEmit &emit, const char *subkey, int v) {
  emit(subkey, std::to_string(v));
}
inline void emit_field(const TelemetryEmit &emit, const char *subkey,
                       unsigned int v) {
  emit(subkey, std::to_string(v));
}
inline void emit_field(const TelemetryEmit &emit, const char *subkey, bool v) {
  emit(subkey, v ? "true" : "false");
}
inline void emit_field(const TelemetryEmit &emit, const char *subkey,
                       const char *v) {
  emit(subkey, json_encode_string(std::string(v)));
}
inline void emit_field(const TelemetryEmit &emit, const char *subkey,
                       const std::string &v) {
  emit(subkey, json_encode_string(v));
}

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

  // Emits the entity's full per-update state as flat key/value pairs. The
  // default no-op keeps single-state domains (lock, alarm) working through
  // the existing thin path; rich domains override to push every field a TB
  // dashboard might want to chart.
  virtual void append_telemetry_fields(EntityBase *obj,
                                       const TelemetryEmit &emit) {
    (void) obj;
    (void) emit;
  }
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
