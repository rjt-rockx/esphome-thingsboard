#include "control_iterator.h"
#include "thingsboard_client.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/json/json_util.h"

#ifdef USE_THINGSBOARD_HTTP_OTA
#include "../thingsboard_http_ota/thingsboard_http_ota.h"
#endif

#ifdef USE_SWITCH
#include "switch_handler.h"
#endif
#ifdef USE_LIGHT
#include "light_handler.h"
#endif
#ifdef USE_NUMBER
#include "number_handler.h"
#endif
#ifdef USE_BUTTON
#include "button_handler.h"
#endif
#ifdef USE_SELECT
#include "select_handler.h"
#endif
#ifdef USE_CLIMATE
#include "climate_handler.h"
#endif
#ifdef USE_FAN
#include "fan_handler.h"
#endif
#ifdef USE_COVER
#include "cover_handler.h"
#endif
#ifdef USE_VALVE
#include "valve_handler.h"
#endif
#ifdef USE_LOCK
#include "lock_handler.h"
#endif
#ifdef USE_MEDIA_PLAYER
#include "media_player_handler.h"
#endif
#ifdef USE_ALARM_CONTROL_PANEL
#include "alarm_handler.h"
#endif
#ifdef USE_TEXT
#include "text_handler.h"
#endif

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard.controls";

ControlIterator::ControlIterator(ThingsBoardComponent *parent) : parent_(parent) {}

void ControlIterator::discover_controls() {
  ESP_LOGI(TAG, "Discovering controllable components via domain handlers...");

  this->attribute_handlers_.clear();
  for (auto *handler : this->handlers_) {
    delete handler;
  }
  this->handlers_.clear();

#ifdef USE_SWITCH
  this->handlers_.push_back(new SwitchHandler());
#endif
#ifdef USE_LIGHT
  this->handlers_.push_back(new LightHandler());
#endif
#ifdef USE_NUMBER
  this->handlers_.push_back(new NumberHandler());
#endif
#ifdef USE_BUTTON
  this->handlers_.push_back(new ButtonHandler());
#endif
#ifdef USE_SELECT
  this->handlers_.push_back(new SelectHandler());
#endif
#ifdef USE_CLIMATE
  this->handlers_.push_back(new ClimateHandler());
#endif
#ifdef USE_FAN
  this->handlers_.push_back(new FanHandler());
#endif
#ifdef USE_COVER
  this->handlers_.push_back(new CoverHandler());
#endif
#ifdef USE_VALVE
  this->handlers_.push_back(new ValveHandler());
#endif
#ifdef USE_LOCK
  this->handlers_.push_back(new LockHandler());
#endif
#ifdef USE_MEDIA_PLAYER
  this->handlers_.push_back(new MediaPlayerHandler());
#endif
#ifdef USE_ALARM_CONTROL_PANEL
  this->handlers_.push_back(new AlarmHandler());
#endif
#ifdef USE_TEXT
  this->handlers_.push_back(new TextHandler());
#endif

  register_fn reg = [this](const std::string &key, std::function<void(const std::string &)> handler) {
    this->register_attribute_handler(key, handler);
  };

  size_t total_entities = 0;
  for (auto *handler : this->handlers_) {
    handler->register_shared_attributes(reg);
    size_t count = handler->entity_count();
    if (count > 0) {
      ESP_LOGD(TAG, "  %s: %zu entities", handler->domain(), count);
    }
    total_entities += count;
  }

  ESP_LOGI(TAG, "Discovered %zu entities across %zu domains, %zu attribute handlers",
           total_entities, this->handlers_.size(), this->attribute_handlers_.size());
}

esp_err_t ControlIterator::handle_rpc(const std::string &method, const std::string &params) {
  std::string response;
  return this->handle_rpc_with_response(method, params, response);
}

esp_err_t ControlIterator::handle_rpc_with_response(const std::string &method, const std::string &params, std::string &response) {
  ESP_LOGV(TAG, "Handling RPC: %s with params: %s", method.c_str(), params.c_str());

  size_t dot_pos = method.find('.');
  if (dot_pos == std::string::npos) {
    ESP_LOGW(TAG, "Invalid RPC method format, expected 'domain.method': %s", method.c_str());
    response = "{\"success\":false,\"error\":\"Invalid method format\"}";
    return ESP_ERR_INVALID_ARG;
  }

  std::string domain = method.substr(0, dot_pos);
  std::string method_name = method.substr(dot_pos + 1);

  if (domain == "discovery") {
    if (method_name == "help") {
      response = this->get_discovery_help_json();
    } else if (method_name == "entities") {
      response = this->get_entities_list_json();
    } else if (method_name == "domains") {
      response = this->get_domains_info_json();
    } else if (method_name == "status") {
      response = this->get_discovery_status_json();
    } else {
      response = "{\"error\":\"Unknown discovery method. Try: discovery.help\"}";
      return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "Discovery: %s", method_name.c_str());
    return ESP_OK;
  }

  DomainHandler *handler = this->find_handler(domain);
  if (handler == nullptr) {
    ESP_LOGW(TAG, "Unknown RPC domain: %s", domain.c_str());
    response = "{\"success\":false,\"error\":\"Unknown domain\"}";
    return ESP_ERR_NOT_SUPPORTED;
  }

  std::string entity_id;
  JsonDocument doc;

  if (!params.empty() && params != "{}") {
    doc = json::parse_json(params);
    JsonObject root = doc.as<JsonObject>();
    if (root.isNull()) {
      ESP_LOGW(TAG, "Failed to parse RPC params: %s", params.c_str());
      response = "{\"success\":false,\"error\":\"Invalid JSON params\"}";
      return ESP_ERR_INVALID_ARG;
    }
    if (root["entity_id"].is<const char*>()) {
      entity_id = root["entity_id"].as<std::string>();
    }

    RpcResult result = handler->handle_rpc(method_name, entity_id, root);
    if (result.err == ESP_OK) {
      if (!result.state_json.empty()) {
        response = json::build_json([&result](JsonObject root) {
          root["success"] = true;
          JsonDocument state_doc = json::parse_json(result.state_json);
          root["state"] = state_doc.as<JsonObject>();
        });
      } else {
        response = "{\"success\":true}";
      }
    } else {
      const char *err_msg = "Command failed";
      if (result.err == ESP_ERR_NOT_FOUND) err_msg = "Entity not found";
      else if (result.err == ESP_ERR_NOT_SUPPORTED) err_msg = "Unknown method";
      else if (result.err == ESP_ERR_INVALID_ARG) err_msg = "Invalid parameters";
      response = json::build_json([err_msg](JsonObject root) {
        root["success"] = false;
        root["error"] = err_msg;
      });
    }
    return result.err;
  }

  ESP_LOGW(TAG, "Missing parameters for RPC: %s", method.c_str());
  response = "{\"success\":false,\"error\":\"Missing parameters\"}";
  return ESP_ERR_INVALID_ARG;
}

void ControlIterator::handle_shared_attributes(const std::map<std::string, std::string> &attributes) {
#ifdef USE_THINGSBOARD_HTTP_OTA
  bool has_firmware_attrs = false;
  for (const auto &attr : attributes) {
    if (attr.first.find("fw_") == 0) {
      has_firmware_attrs = true;
      break;
    }
  }

  // Only fire the HTTPS path when it's the active OTA transport; when the MQTT
  // OTA component is active it takes over via ota_transport_ and the chunked
  // path runs from dispatch_shared_attributes instead.
  if (has_firmware_attrs && this->ota_component_ &&
      this->parent_->get_ota_transport() ==
          static_cast<thingsboard::TBOTATransport *>(this->ota_component_)) {
    ESP_LOGD(TAG, "Forwarding firmware attributes to OTA component");
    this->ota_component_->handle_firmware_attributes(attributes);
  }
#endif

  for (const auto &attr : attributes) {
    auto it = this->attribute_handlers_.find(attr.first);
    if (it != this->attribute_handlers_.end()) {
      ESP_LOGD(TAG, "Processing shared attribute: %s = %s", attr.first.c_str(), attr.second.c_str());
      it->second(attr.second);
    }
  }
}

void ControlIterator::register_attribute_handler(const std::string &key, std::function<void(const std::string&)> handler) {
  this->attribute_handlers_[key] = handler;
}

DomainHandler *ControlIterator::find_handler(const std::string &domain) {
  for (auto *handler : this->handlers_) {
    if (domain == handler->domain()) {
      return handler;
    }
  }
  return nullptr;
}

std::string ControlIterator::get_discovery_help_json() {
  return json::build_json([this](JsonObject root) {
    JsonObject commands = root["commands"].to<JsonObject>();
    commands["discovery.help"] = "Show this help message";
    commands["discovery.entities"] = "List all controllable entities with their IDs and traits";
    commands["discovery.domains"] = "Show available domains and their methods";
    commands["discovery.status"] = "Show discovery status and entity counts";

    JsonObject usage = root["usage"].to<JsonObject>();
    usage["format"] = "domain.method";
    JsonArray examples = usage["examples"].to<JsonArray>();
    examples.add("discovery.entities");

    for (auto *handler : this->handlers_) {
      if (handler->entity_count() > 0) {
        std::string domain_str = handler->domain();
        if (domain_str == "switch")
          examples.add("switch.turn_on {\"entity_id\": \"my_switch\"}");
        else if (domain_str == "climate")
          examples.add("climate.control {\"entity_id\": \"thermostat\", \"target_temperature\": 22}");
        else if (domain_str == "light")
          examples.add("light.turn_on {\"entity_id\": \"status_led\", \"brightness\": 128}");
        else if (domain_str == "fan")
          examples.add("fan.turn_on {\"entity_id\": \"my_fan\", \"speed\": 3}");
      }
    }
  });
}

std::string ControlIterator::get_entities_list_json() {
  return json::build_json([this](JsonObject root) {
    JsonObject entities = root["entities"].to<JsonObject>();
    for (auto *handler : this->handlers_) {
      if (handler->entity_count() == 0) continue;
      JsonArray domain_arr = entities[handler->domain()].to<JsonArray>();
      handler->append_entity_discovery(domain_arr);
    }
  });
}

std::string ControlIterator::get_domains_info_json() {
  return json::build_json([this](JsonObject root) {
    JsonObject domains = root["domains"].to<JsonObject>();
    for (auto *handler : this->handlers_) {
      handler->append_domain_info(domains);
    }

    JsonObject discovery = domains["discovery"].to<JsonObject>();
    JsonArray methods = discovery["methods"].to<JsonArray>();
    methods.add("help");
    methods.add("entities");
    methods.add("domains");
    methods.add("status");
    discovery["count"] = 4;
  });
}

std::string ControlIterator::get_discovery_status_json() {
  return json::build_json([this](JsonObject root) {
    root["status"] = "ready";
    size_t total = 0;
    JsonObject counts = root["counts"].to<JsonObject>();
    for (auto *handler : this->handlers_) {
      size_t count = handler->entity_count();
      if (count > 0) {
        counts[handler->domain()] = count;
        total += count;
      }
    }
    root["total_entities"] = total;
  });
}

}  // namespace thingsboard
}  // namespace esphome
