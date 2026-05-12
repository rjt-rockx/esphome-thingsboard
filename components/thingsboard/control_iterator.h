#pragma once

#include "domain_handler.h"
#include "esphome/core/entity_base.h"
#include "esphome/components/json/json_util.h"
#include <map>
#include <string>
#include <functional>
#include <vector>

namespace esphome {

namespace thingsboard_http_ota {
class ThingsBoardHttpOtaComponent;
}

namespace thingsboard {

class ThingsBoardComponent;

/// Dispatcher that discovers controllable components via domain handlers and routes RPCs
class ControlIterator {
 public:
  ControlIterator(ThingsBoardComponent *parent);

  /// Discover all controllable components and register handlers
  void discover_controls();

  /// Handle RPC method calls (ESPHome REST API style: domain.method with JSON params)
  esp_err_t handle_rpc(const std::string &method, const std::string &params);

  /// Handle RPC method calls and return response data
  esp_err_t handle_rpc_with_response(const std::string &method, const std::string &params, std::string &response);

  /// Handle shared attribute updates
  void handle_shared_attributes(const std::map<std::string, std::string> &attributes);

  /// Register OTA component for handling firmware shared attributes
  void register_ota_component(thingsboard_http_ota::ThingsBoardHttpOtaComponent *ota_component) {
    this->ota_component_ = ota_component;
  }

  /// Find handler by domain name. ThingsBoardComponent uses this from the
  /// per-domain telemetry path (T10) to route on_*_update through the same
  /// `append_telemetry_fields` virtual that powers RPC responses.
  DomainHandler *find_handler(const std::string &domain);

 protected:
  ThingsBoardComponent *parent_;

  /// Domain handlers
  std::vector<DomainHandler *> handlers_;

  /// Shared attribute handlers
  std::map<std::string, std::function<void(const std::string&)>> attribute_handlers_;

  /// Register shared attribute handler for a component
  void register_attribute_handler(const std::string &key, std::function<void(const std::string&)> handler);

  /// Discovery response builders
  std::string get_discovery_help_json();
  std::string get_entities_list_json();
  std::string get_domains_info_json();
  std::string get_discovery_status_json();

 private:
  thingsboard_http_ota::ThingsBoardHttpOtaComponent *ota_component_{nullptr};
};

}  // namespace thingsboard
}  // namespace esphome
