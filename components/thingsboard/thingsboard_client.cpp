#include "thingsboard_client.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/controller_registry.h"
#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
#include "esphome/components/thingsboard_mqtt/thingsboard_mqtt_transport.h"
#endif
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif
#ifdef USE_NETWORK
#include "esphome/components/network/util.h"
#endif

namespace esphome {
namespace thingsboard {

static const char *TAG = "thingsboard";

namespace {

// JSON-encode a string into a `"…"` literal, escaping per RFC 8259.
// Used at batch-add time so a value containing `"`, `\`, or control chars
// can't break the outgoing payload.
std::string encode_json_string(const std::string &s) {
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
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<uint8_t>(c));
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
  return out;
}

}  // namespace

// Out-of-line so std::unique_ptr<ThingsBoardMQTT> sees the complete type here.
ThingsBoardComponent::ThingsBoardComponent() = default;
ThingsBoardComponent::~ThingsBoardComponent() = default;

void ThingsBoardComponent::initialize_component_() {
  ESP_LOGV(TAG, "Initializing ThingsBoard component");
  ESP_LOGV(TAG, "ThingsBoard component initialization completed");
}

void ThingsBoardComponent::setup() {
  ESP_LOGD(TAG, "Setting up ThingsBoard");

  ControllerRegistry::register_controller(this);

  this->initialize_component_();

  // Device name precedence: explicit non-empty > explicit empty (server
  // generates) > ESPHome device name.
  if (!this->device_name_explicitly_set_) {
    this->device_name_ = App.get_name();
    ESP_LOGD(TAG, "Device name not configured, using ESPHome device name: %s",
             this->device_name_.c_str());
  } else if (this->device_name_.empty()) {
    ESP_LOGD(TAG, "Device name explicitly set to empty, server will generate "
                  "device name");
  } else {
    ESP_LOGD(TAG, "Using configured device name: %s",
             this->device_name_.c_str());
  }

#if defined(USE_ESP32) && defined(USE_THINGSBOARD_MQTT_TRANSPORT)
  if (!this->mqtt_broker_.empty()) {
    this->setup_mqtt_();
  } else {
    ESP_LOGE(TAG, "MQTT broker not configured!");
    this->mark_failed();
    return;
  }
#elif !defined(USE_THINGSBOARD_MQTT_TRANSPORT) && !defined(USE_THINGSBOARD_HTTP_TRANSPORT)
  ESP_LOGE(TAG, "ThingsBoard MQTT only supported on ESP32");
  this->mark_failed();
  return;
#endif

  this->setup_called_ = true;

  this->load_device_token_();

  this->control_iterator_.discover_controls();
  ESP_LOGD(TAG, "Control iterator initialized for RPC handling");
}

void ThingsBoardComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ThingsBoard:");
  ESP_LOGCONFIG(TAG, "  MQTT Broker: %s:%d", this->mqtt_broker_.c_str(),
                this->mqtt_port_);
  ESP_LOGCONFIG(TAG, "  Device Name: %s", this->device_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Telemetry: Batched with deduplication");
  if (this->telemetry_interval_ > 0) {
    ESP_LOGCONFIG(TAG, "  Telemetry Interval: %ums", this->telemetry_interval_);
  } else {
    ESP_LOGCONFIG(TAG, "  Batch Delay: %ums", this->batch_delay_);
  }
  if (this->telemetry_throttle_ > 0) {
    ESP_LOGCONFIG(TAG, "  Telemetry Throttle: %ums", this->telemetry_throttle_);
  }
  ESP_LOGCONFIG(TAG, "  Periodic Sync: Every %us",
                this->all_telemetry_interval_ / 1000);
  if (this->rate_limits_.limits_received_) {
    ESP_LOGCONFIG(TAG, "  Rate Limits: maxPayload=%u, maxInflight=%u",
                  this->rate_limits_.max_payload_size_,
                  this->rate_limits_.max_inflight_messages_);
  } else {
    ESP_LOGCONFIG(TAG, "  Rate Limits: Not yet received");
  }
  if (!this->claim_secret_key_.empty()) {
    ESP_LOGCONFIG(TAG, "  Claim Secret Key: " LOG_SECRET("%s"),
                  this->claim_secret_key_.c_str());
  }
  if (this->claim_duration_ms_ > 0) {
    ESP_LOGCONFIG(TAG, "  Claim Duration: %ums", this->claim_duration_ms_);
  }
  if (!this->device_token_.empty()) {
    ESP_LOGCONFIG(TAG, "  Device Token: " LOG_SECRET("%s"),
                  this->device_token_.c_str());
  } else {
    ESP_LOGCONFIG(TAG, "  Device Token: Not configured");
  }
}

void ThingsBoardComponent::loop() {
  uint32_t now = millis();

#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
  if (this->mqtt_client_) {
    this->mqtt_client_->loop();
  }
#endif

  // Connection attempts are rate-limited so a failing endpoint doesn't busy-loop.
  if (this->setup_called_ && !this->is_connected() &&
      !this->provisioning_in_progress_ &&
      (now - this->last_connection_attempt_ >
       this->connection_retry_interval_)) {
#ifdef USE_WIFI
    if (wifi::global_wifi_component != nullptr &&
        wifi::global_wifi_component->is_connected()) {
      this->last_connection_attempt_ = now;

      if (!this->has_access_token() && this->device_token_.empty()) {
        this->load_device_token_();
      }

      if (!this->has_access_token() && this->device_token_.empty()) {
        ESP_LOGD(TAG,
                 "WiFi connected, starting ThingsBoard connection process");
        this->establish_connection_();
      } else if (!this->is_connected()) {
#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
        ESP_LOGD(TAG, "Have device token, attempting MQTT connection");
        this->connect_mqtt_();
#endif
      }
    }
#endif
  }

  if (this->provisioning_in_progress_) {
    this->check_provisioning_status_();
  }

  if (!this->initial_states_sent_ && this->initial_state_iterator_ &&
      !this->initial_state_iterator_->completed()) {
    this->process_initial_state_batch_();
  } else if (!this->initial_states_sent_ && this->initial_state_iterator_ &&
             this->initial_state_iterator_->completed()) {
    this->initial_states_sent_ = true;
    ESP_LOGD(TAG, "Initial state synchronization completed");
  }

  if (!this->pending_messages_.empty()) {
    uint32_t interval = this->telemetry_interval_ > 0
        ? this->telemetry_interval_
        : this->batch_delay_;
    if (now - this->last_batch_process_ >= interval) {
      this->process_batch_();
    }
  }

  if (this->is_connected() &&
      (now - this->last_all_telemetry_ > this->all_telemetry_interval_)) {
    this->last_all_telemetry_ = now;
    this->send_all_components_telemetry_();
  }
}

void ThingsBoardComponent::establish_connection_() {
  ESP_LOGD(TAG, "Establishing connection");

#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
  if (this->mqtt_client_ && !this->is_connected() &&
      (this->has_access_token() || !this->device_token_.empty())) {
    ESP_LOGD(TAG, "Have device token, attempting MQTT connection");
    this->connect_mqtt_();
    return;
  }
#endif

  if (!this->has_access_token() && this->device_token_.empty()) {
    if (this->provisioning_key_.empty() || this->provisioning_secret_.empty()) {
      ESP_LOGE(TAG,
               "No device token and no provisioning credentials available");
      return;
    }

    ESP_LOGD(TAG, "No device token, attempting provisioning");
    if (this->provision_device_()) {
      ESP_LOGI(TAG, "Device provisioning initiated");
    } else {
      ESP_LOGW(TAG, "Device provisioning failed, will retry later");
      return;
    }
  }
}

bool ThingsBoardComponent::send_telemetry(const std::string &data) {
  if (!this->is_connected()) {
    ESP_LOGW(TAG, "Cannot send telemetry: not connected");
    return false;
  }
  if (this->transport_ == nullptr) {
    ESP_LOGE(TAG, "No transport registered");
    return false;
  }
  return this->transport_->publish_telemetry(data);
}

bool ThingsBoardComponent::send_attributes(const std::string &data) {
  if (!this->is_connected()) {
    ESP_LOGW(TAG, "Cannot send attributes: not connected");
    return false;
  }
  if (this->transport_ == nullptr) {
    ESP_LOGE(TAG, "No transport registered");
    return false;
  }
  return this->transport_->publish_attributes(data);
}

bool ThingsBoardComponent::send_client_attributes(const std::string &data) {
  if (!this->is_connected()) {
    ESP_LOGW(TAG, "Cannot send client attributes: not connected");
    return false;
  }
  if (this->transport_ == nullptr) {
    ESP_LOGE(TAG, "No transport registered");
    return false;
  }
  return this->transport_->publish_client_attributes(data);
}

void ThingsBoardComponent::clear_device_token() { this->clear_device_token_(); }

void ThingsBoardComponent::set_device_token_persistent(
    const std::string &token) {
  if (token.empty()) {
    ESP_LOGW(TAG, "Refusing to persist empty device token");
    return;
  }
  this->save_device_token_(token);
  this->access_token_ = token;
  this->device_token_ = token;
#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
  if (this->mqtt_client_) {
    this->mqtt_client_->set_device_token(token);
    this->connect_mqtt_();
  }
#endif
}

std::string ThingsBoardComponent::get_device_mac_() {
  return get_mac_address_pretty();
}

void ThingsBoardComponent::send_device_metadata_() {
  if (!this->is_connected()) {
    return;
  }

  ESP_LOGD(TAG, "Sending device metadata as client-side attributes");

  // Keys follow ESPHome's domain.object_id telemetry naming scheme so
  // dashboards/rule chains treat metadata, sensor data, and OTA state
  // uniformly.
  std::string payload = json::build_json([this](JsonObject root) {
    root["device.name"] = App.get_name();

    if (!App.get_friendly_name().empty()) {
      root["device.friendly_name"] = App.get_friendly_name();
    }

#ifdef USE_AREAS
    const char *area = App.get_area();
    if (area != nullptr && strlen(area) > 0) {
      root["device.area"] = area;
    }
#endif

    root["device.mac_address"] = this->get_device_mac_();
    root["device.esphome_version"] = ESPHOME_VERSION;
    root["device.board"] = ESPHOME_BOARD;

#ifdef USE_ESP8266
    root["device.platform"] = "ESP8266";
#endif
#ifdef USE_ESP32
    root["device.platform"] = "ESP32";
#endif
#ifdef USE_RP2040
    root["device.platform"] = "RP2040";
#endif
#ifdef USE_LIBRETINY
    root["device.platform"] = lt_cpu_get_model_name();
#endif
#ifdef USE_BK72XX
    root["device.platform"] = "BK72XX";
#endif
#ifdef USE_LN882X
    root["device.platform"] = "LN882X";
#endif
#ifdef USE_NRF52
    root["device.platform"] = "NRF52";
#endif
#ifdef USE_RTL87XX
    root["device.platform"] = "RTL87XX";
#endif
#ifdef USE_HOST
    root["device.platform"] = "Host";
#endif

    // TB matches OTA packages against `current_fw_title` / `current_fw_version`
    // (https://thingsboard.io/docs/user-guide/ota-updates/#device-side-implementation).
#ifdef ESPHOME_PROJECT_NAME
    root["device.project_name"] = ESPHOME_PROJECT_NAME;
    root["current_fw_title"] = ESPHOME_PROJECT_NAME;
#endif
#ifdef ESPHOME_PROJECT_VERSION
    root["device.project_version"] = ESPHOME_PROJECT_VERSION;
    root["current_fw_version"] = ESPHOME_PROJECT_VERSION;
#endif

    char build_time_buf[esphome::Application::BUILD_TIME_STR_SIZE];
    App.get_build_time_string(build_time_buf);
    root["device.compilation_time"] = build_time_buf;

#if defined(USE_WIFI)
    root["device.network_type"] = "wifi";
#elif defined(USE_ETHERNET)
    root["device.network_type"] = "ethernet";
#elif defined(USE_MODEM)
    root["device.network_type"] = "modem";
#elif defined(USE_OPENTHREAD)
    root["device.network_type"] = "openthread";
#endif

#ifdef USE_NETWORK
    auto ips = network::get_ip_addresses();
    uint8_t index = 0;
    for (auto &ip : ips) {
      if (ip.is_set()) {
        std::string key = (index == 0)
                              ? "device.ip_address"
                              : "device.ip_address_" + esphome::to_string(index);
        char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
        ip.str_to(ip_buf);
        root[key] = ip_buf;
        index++;
        if (index >= 5)
          break;
      }
    }
#endif
  });

  if (!payload.empty() && this->transport_ != nullptr) {
    this->transport_->publish_client_attributes(payload);
    ESP_LOGD(TAG, "Device metadata sent successfully");
  } else {
    ESP_LOGW(TAG, "Failed to build or send device metadata");
  }
}

bool ThingsBoardComponent::load_device_token_() {
  ESPPreferenceObject pref =
      global_preferences->make_preference<char[128]>(fnv1_hash("tb_token"));
  char stored_token[128];
  if (pref.load(&stored_token) && strlen(stored_token) > 0) {
    this->access_token_ = std::string(stored_token);
    ESP_LOGI(TAG, "Loaded device token from preferences (%zu chars)",
             this->access_token_.length());
    return true;
  }
  ESP_LOGD(TAG, "No valid device token found in preferences");
  return false;
}

void ThingsBoardComponent::save_device_token_(const std::string &token) {
  ESPPreferenceObject pref =
      global_preferences->make_preference<char[128]>(fnv1_hash("tb_token"));
  char token_array[128];
  strncpy(token_array, token.c_str(), sizeof(token_array) - 1);
  token_array[sizeof(token_array) - 1] = '\0';
  pref.save(&token_array);
  ESP_LOGI(TAG, "Device token saved to preferences");
}

void ThingsBoardComponent::clear_device_token_() {
  ESPPreferenceObject pref =
      global_preferences->make_preference<char[128]>(fnv1_hash("tb_token"));
  char empty_token[128] = {0};
  pref.save(&empty_token);
  this->access_token_.clear();
  this->device_token_.clear();
  ESP_LOGI(TAG, "Device token cleared from preferences - will re-provision on "
                "next connection");
}

bool ThingsBoardComponent::provision_device_() {
  ESP_LOGD(TAG, "Starting device provisioning via HTTP");
  return this->provision_device_http_();
}

bool ThingsBoardComponent::provision_device_mqtt_() {
#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
  if (!this->mqtt_client_) {
    ESP_LOGD(TAG, "MQTT client not available for provisioning");
    return false;
  }

  // https://thingsboard.io/docs/reference/mqtt-api/#device-provisioning
  std::string payload = json::build_json([this](JsonObject root) {
    if (!this->device_name_.empty()) {
      root["deviceName"] = this->device_name_;
    }
    root["provisionDeviceKey"] = this->provisioning_key_;
    root["provisionDeviceSecret"] = this->provisioning_secret_;

    if (!this->provisioning_credentials_type_.empty()) {
      root["credentialsType"] = this->provisioning_credentials_type_;
      if (this->provisioning_credentials_type_ == "ACCESS_TOKEN") {
        if (!this->provisioning_credentials_token_.empty()) {
          root["token"] = this->provisioning_credentials_token_;
        }
      } else if (this->provisioning_credentials_type_ == "MQTT_BASIC") {
        if (!this->provisioning_credentials_client_id_.empty()) {
          root["clientId"] = this->provisioning_credentials_client_id_;
        }
        if (!this->provisioning_credentials_username_.empty()) {
          root["username"] = this->provisioning_credentials_username_;
        }
        if (!this->provisioning_credentials_password_.empty()) {
          root["password"] = this->provisioning_credentials_password_;
        }
      } else if (this->provisioning_credentials_type_ == "X509_CERTIFICATE") {
        if (!this->provisioning_credentials_cert_pem_.empty()) {
          root["hash"] = this->provisioning_credentials_cert_pem_;
        }
      }
    }
  });

  ESP_LOGD(TAG, "Provisioning payload: %s", payload.c_str());

  // Connect with username="provision"; the actual request is published from
  // on_mqtt_connect_ once the broker is ready.
  if (!this->mqtt_client_->connect_for_provisioning()) {
    ESP_LOGW(TAG, "Failed to connect for MQTT provisioning");
    return false;
  }

  this->provisioning_in_progress_ = true;
  this->provisioning_via_mqtt_ = true;
  this->provisioning_start_time_ = millis();
  return true;
#else
  return false;
#endif
}

bool ThingsBoardComponent::provision_device_http_() {
  if (this->get_parent() == nullptr) {
    ESP_LOGE(TAG, "HTTP request component not available");
    return false;
  }

  // https://thingsboard.io/docs/reference/http-api/#device-provisioning
  std::string provision_url = this->server_url_ + "/api/v1/provision";

  std::string payload = json::build_json([this](JsonObject root) {
    if (!this->device_name_.empty()) {
    root["deviceName"] = this->device_name_;
    }
    root["provisionDeviceKey"] = this->provisioning_key_;
    root["provisionDeviceSecret"] = this->provisioning_secret_;
  });

  ESP_LOGV(TAG, "Provisioning URL: %s", provision_url.c_str());
  ESP_LOGV(TAG, "Provisioning payload: %s", payload.c_str());

  std::vector<http_request::Header> headers;
  headers.push_back({"Content-Type", "application/json"});
  headers.push_back({"Connection", "close"});

  this->provision_container_ =
      this->get_parent()->post(provision_url, payload, headers);
  if (!this->provision_container_) {
    ESP_LOGE(TAG, "Failed to start provisioning request");
    return false;
  }

  this->provisioning_in_progress_ = true;
  this->provisioning_via_mqtt_ = false;
  this->provisioning_start_time_ = millis();

  ESP_LOGV(TAG, "HTTP provisioning request started");
  // Completes asynchronously via check_provisioning_status_().
  return false;
}

void ThingsBoardComponent::check_provisioning_status_() {
  // MQTT provisioning completes via callback.
  if (!this->provisioning_in_progress_ || this->provisioning_via_mqtt_ ||
      !this->provision_container_) {
    return;
  }

  if (millis() - this->provisioning_start_time_ > this->timeout_) {
    ESP_LOGW(TAG, "Provisioning request timed out after %dms", this->timeout_);
    this->provision_container_->end();
    this->provision_container_.reset();
    this->provisioning_in_progress_ = false;
    return;
  }

  if (this->provision_container_->status_code == 0) {
    return;
  }

  ESP_LOGV(TAG, "Provisioning response received (HTTP %d)",
           this->provision_container_->status_code);

  if (this->provision_container_->status_code != 200) {
    ESP_LOGW(TAG, "Provisioning failed (HTTP %d)",
             this->provision_container_->status_code);
    this->provision_container_->end();
    this->provision_container_.reset();
    this->provisioning_in_progress_ = false;
    return;
  }

  std::string response;
  char buffer[512];
  int bytes_read;
  while ((bytes_read = this->provision_container_->read(
              (uint8_t *)buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytes_read] = '\0';
    response += buffer;
  }
  this->provision_container_->end();
  this->provision_container_.reset();
  this->provisioning_in_progress_ = false;

  this->handle_provision_response_(response);
}

void ThingsBoardComponent::handle_provision_response_(
    const std::string &response) {
  ESP_LOGV(TAG, "Provisioning response: %s", response.c_str());

  // Per https://thingsboard.io/docs/reference/mqtt-api/#device-provisioning the
  // response uses either provisionDeviceStatus/accessToken or
  // status/credentialsValue depending on the TB version.
  bool parse_success = false;
  std::string credentials_value;
  std::string credentials_type;

  json::parse_json(response, [&](JsonObject root) -> bool {
    bool success = false;
    if (root["provisionDeviceStatus"].is<const char *>()) {
      success = (strcmp(root["provisionDeviceStatus"].as<const char *>(),
                        "SUCCESS") == 0);
    } else if (root["status"].is<const char *>()) {
      success = (strcmp(root["status"].as<const char *>(), "SUCCESS") == 0);
    }

    if (!success) {
      std::string error_msg = "Unknown error";
      if (root["errorMsg"].is<const char *>()) {
        error_msg = root["errorMsg"].as<std::string>();
      } else if (root["provisionDeviceErrorMsg"].is<const char *>()) {
        error_msg = root["provisionDeviceErrorMsg"].as<std::string>();
      }
      ESP_LOGW(TAG, "Provisioning failed: %s", error_msg.c_str());
      return false;
    }

    if (root["credentialsType"].is<const char *>()) {
      credentials_type = root["credentialsType"].as<std::string>();
      ESP_LOGD(TAG, "Credentials type: %s", credentials_type.c_str());
    }

    if (root["accessToken"].is<const char *>()) {
      credentials_value = root["accessToken"].as<std::string>();
    } else if (root["credentialsValue"].is<const char *>()) {
      credentials_value = root["credentialsValue"].as<std::string>();
    } else {
      ESP_LOGW(TAG, "No credentials value in provisioning response");
      return false;
    }

    if (credentials_value.empty()) {
      ESP_LOGW(TAG, "Empty credentials value in provisioning response");
      return false;
    }

    parse_success = true;
    return true;
  });

  if (!parse_success) {
    return;
  }

  this->access_token_ = credentials_value;
  this->save_device_token_(this->access_token_);
  this->provisioned_ = true;

  ESP_LOGI(TAG, "Device provisioned successfully with token: %s",
           this->access_token_.c_str());

  if (this->provisioning_via_mqtt_) {
#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
    ESP_LOGD(TAG, "Disconnecting from provisioning MQTT connection");
    if (this->mqtt_client_) {
      this->mqtt_client_->disconnect();
    }
    ESP_LOGD(TAG, "Reconnecting with access token");
    this->connect_mqtt_();
#endif
  }

  if (!credentials_type.empty()) {
    ESP_LOGD(TAG, "Credentials type: %s", credentials_type.c_str());
  }
}

std::string
ThingsBoardComponent::get_domain_scoped_id_(const std::string &domain,
                                            const std::string &object_id) {
  return domain + "." + object_id;
}

std::string
ThingsBoardComponent::get_domain_scoped_id_(const std::string &domain,
                                            const EntityBase *obj) {
  char buf[OBJECT_ID_MAX_LEN];
  return domain + "." + std::string(obj->get_object_id_to(buf));
}

#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
void ThingsBoardComponent::setup_mqtt_() {
  ESP_LOGD(TAG, "Setting up MQTT client");

  this->mqtt_client_ = std::make_unique<ThingsBoardMQTT>();
  this->register_transport(this->mqtt_client_.get());
  this->mqtt_client_->set_parent_component(this);
  this->mqtt_client_->set_broker(this->mqtt_broker_, this->mqtt_port_);
  this->mqtt_client_->set_client_id(this->device_name_);

  // Inbound data callbacks are wired by register_transport(); only the
  // MQTT-specific lifecycle hooks are wired here.
  this->mqtt_client_->set_on_connect_callback(
      [this]() { this->on_mqtt_connect_(); });
  this->mqtt_client_->set_on_disconnect_callback(
      [this]() { this->on_mqtt_disconnect_(); });
  this->mqtt_client_->set_on_auth_failure_callback(
      [this]() { this->on_mqtt_auth_failure_(); });

  if (!this->device_token_.empty()) {
    this->mqtt_client_->set_device_token(this->device_token_);
    if (!this->mqtt_client_->connect()) {
      ESP_LOGW(TAG, "Initial MQTT connection failed, will retry automatically");
    }
  } else {
    ESP_LOGD(TAG, "No device token yet, will connect after provisioning");
  }
}

void ThingsBoardComponent::connect_mqtt_() {
  if (!this->mqtt_client_) {
    ESP_LOGE(TAG, "MQTT client not initialized");
    return;
  }
  
  std::string token =
      this->device_token_.empty() ? this->access_token_ : this->device_token_;
  if (token.empty()) {
    ESP_LOGW(TAG, "No device token available for MQTT connection");
    return;
  }
  
  ESP_LOGD(TAG, "Connecting to MQTT with device token");
  this->mqtt_client_->set_device_token(token);
  if (!this->mqtt_client_->connect()) {
    ESP_LOGW(TAG, "MQTT connection failed, will retry automatically");
  }
}

void ThingsBoardComponent::on_mqtt_connect_() {
  ESP_LOGI(TAG, "Connected to ThingsBoard MQTT broker");
  this->status_clear_warning();

  if (this->provisioning_in_progress_ && this->provisioning_via_mqtt_) {
    ESP_LOGD(TAG, "Sending MQTT provisioning request");
    std::string payload = json::build_json([this](JsonObject root) {
      if (!this->device_name_.empty()) {
        root["deviceName"] = this->device_name_;
      }
      root["provisionDeviceKey"] = this->provisioning_key_;
      root["provisionDeviceSecret"] = this->provisioning_secret_;
    });

    if (this->transport_ == nullptr ||
        !this->transport_->publish_provision_request(payload)) {
      ESP_LOGE(TAG, "Failed to publish provisioning request");
      this->provisioning_in_progress_ = false;
      this->provision_device_http_();
    }
    // Don't subscribe to normal topics yet; wait for provisioning response.
    return;
  }

  // getSessionLimits is MQTT-only.
  if (!this->rate_limits_.limits_received_) {
    this->request_session_limits_();
  }
}

void ThingsBoardComponent::on_mqtt_disconnect_() {
  ESP_LOGW(TAG, "Disconnected from ThingsBoard MQTT broker");
}

void ThingsBoardComponent::on_mqtt_auth_failure_() {
  ESP_LOGW(TAG, "MQTT authentication failed - device token may be invalid");

  if (!this->provisioning_key_.empty() && !this->provisioning_secret_.empty()) {
    ESP_LOGI(TAG, "Authentication failed, attempting provisioning as fallback");
    this->clear_device_token_();
    if (this->mqtt_client_) {
      this->mqtt_client_->disconnect();
    }
    this->provision_device_();
  } else {
    ESP_LOGW(TAG, "No provisioning credentials available, cannot re-provision");
    ESP_LOGW(TAG, "Clearing stored device token");
    this->clear_device_token_();
    if (this->mqtt_client_) {
      this->mqtt_client_->disconnect();
    }
  }
}

#endif

void ThingsBoardComponent::dispatch_connected() {
  ESP_LOGI(TAG, "Connected to ThingsBoard");
  this->status_clear_warning();
  // Publish current_fw_title/version so TB can match against assigned OTA
  // packages.
  this->send_device_metadata_();
  if (!this->initial_states_sent_) {
    if (!this->initial_state_iterator_) {
      this->initial_state_iterator_ =
          std::make_unique<InitialStateIterator>(this);
    }
    this->initial_state_iterator_->begin(/*include_internal=*/true);
  }
  // Snapshot fw_*/sw_* attributes so a TB-side OTA assignment that pre-dated
  // this connection — or raced the shared-attr push window — still reaches the
  // OTA bridge.
  if (this->ota_transport_ != nullptr) {
    this->request_attributes(
        "{\"sharedKeys\":\"fw_title,fw_version,fw_size,fw_checksum,"
        "fw_checksum_algorithm,sw_title,sw_version,sw_size,sw_checksum,"
        "sw_checksum_algorithm\"}");
  }
  this->connect_trigger_.trigger();
}

void ThingsBoardComponent::dispatch_disconnected() {
  ESP_LOGW(TAG, "Disconnected from ThingsBoard");
  this->disconnect_trigger_.trigger();
}

void ThingsBoardComponent::dispatch_rpc_request(const std::string &request_id,
                                                const std::string &method,
                                                const std::string &params) {
  ESP_LOGD(TAG, "Received RPC request: %s with params: %s", method.c_str(),
           params.c_str());

  this->rpc_trigger_.trigger(method, params);

  std::string response;
  esp_err_t result = this->control_iterator_.handle_rpc_with_response(
      method, params, response);

  if (response.empty()) {
    response =
        "{\"success\":" + std::string(result == ESP_OK ? "true" : "false") +
        "}";
  }

  this->send_immediate_rpc_response_(request_id, response);
}

void ThingsBoardComponent::dispatch_shared_attributes(
    const std::map<std::string, std::string> &attributes) {
  ESP_LOGD(TAG, "Received shared attributes update");

  this->shared_attributes_trigger_.trigger(attributes);
  this->control_iterator_.handle_shared_attributes(attributes);
  this->maybe_advertise_ota_(attributes);
}

void ThingsBoardComponent::maybe_advertise_ota_(
    const std::map<std::string, std::string> &attributes) {
  // fw_* takes precedence over sw_* if both are present.
  if (this->ota_transport_ == nullptr) return;
  const char *prefix = nullptr;
  if (attributes.count("fw_title") && attributes.count("fw_version")) {
    prefix = "fw_";
  } else if (attributes.count("sw_title") && attributes.count("sw_version")) {
    prefix = "sw_";
  }
  if (prefix == nullptr) return;
  FirmwareInfo info;
  info.title = attributes.at(std::string(prefix) + "title");
  info.version = attributes.at(std::string(prefix) + "version");
  auto size_it = attributes.find(std::string(prefix) + "size");
  if (size_it != attributes.end()) {
    info.size = static_cast<size_t>(std::stoul(size_it->second));
  }
  auto sum_it = attributes.find(std::string(prefix) + "checksum");
  if (sum_it != attributes.end()) info.checksum = sum_it->second;
  auto alg_it = attributes.find(std::string(prefix) + "checksum_algorithm");
  if (alg_it != attributes.end()) info.checksum_algorithm = alg_it->second;
  ESP_LOGI(TAG, "%cOTA advertised: %s v%s",
           prefix[0] == 's' ? 'S' : ' ', info.title.c_str(),
           info.version.c_str());
  this->ota_transport_->on_firmware_advertised(info);
}

void ThingsBoardComponent::dispatch_rpc_response(
    const std::string &request_id, const std::string &response) {
  ESP_LOGD(TAG, "Received RPC response: request_id=%s", request_id.c_str());

  if (response.find("maxPayloadSize") != std::string::npos ||
      response.find("rateLimits") != std::string::npos) {
    this->handle_session_limits_response_(response);
  }

  this->rpc_response_trigger_.trigger(request_id, response);
}

void ThingsBoardComponent::dispatch_attribute_response(
    const std::string &request_id,
    const std::map<std::string, std::string> &attributes) {
  ESP_LOGD(TAG, "Received attribute response: request_id=%s",
           request_id.c_str());

  for (const auto &attr : attributes) {
    ESP_LOGD(TAG, "  Attribute: %s = %s", attr.first.c_str(),
             attr.second.c_str());
  }

  // Close the gap left by the TB shared-attr long-poll: it only delivers
  // deltas that land during an open poll window, so an OTA assignment pushed
  // between polls (or before connect) would be missed. The snapshot response
  // carries the same fw_*/sw_* shape and feeds the same OTA bridge.
  this->maybe_advertise_ota_(attributes);
}

void ThingsBoardComponent::dispatch_provision_response(
    const std::string &response_json) {
  this->handle_provision_response_(response_json);
}

void ThingsBoardComponent::send_single_telemetry_(const std::string &key,
                                                  float value) {
  if (!this->is_connected()) return;
  this->add_to_batch_(key, std::to_string(value), false);
}

void ThingsBoardComponent::send_single_telemetry_(const std::string &key,
                                                  const std::string &value) {
  if (!this->is_connected()) return;
  this->add_to_batch_(key, encode_json_string(value), false);
}

void ThingsBoardComponent::send_single_client_attribute_(const std::string &key,
                                                         bool value) {
  if (!this->is_connected()) return;
  this->add_to_batch_(key, value ? "true" : "false", true);
}

void ThingsBoardComponent::send_single_client_attribute_(const std::string &key,
                                                         float value) {
  if (!this->is_connected()) return;
  this->add_to_batch_(key, std::to_string(value), true);
}

void ThingsBoardComponent::send_single_client_attribute_(
    const std::string &key, const std::string &value) {
  if (!this->is_connected()) return;
  this->add_to_batch_(key, encode_json_string(value), true);
}

#ifdef USE_SENSOR
void ThingsBoardComponent::on_sensor_update(sensor::Sensor *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;
  float state = obj->state;

  this->send_single_telemetry_(this->get_domain_scoped_id_("sensor", obj), state);
}
#endif

#ifdef USE_BINARY_SENSOR
void ThingsBoardComponent::on_binary_sensor_update(
    binary_sensor::BinarySensor *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string scoped_id = this->get_domain_scoped_id_("binary_sensor", obj);
  this->send_single_telemetry_(scoped_id, obj->state ? 1.0f : 0.0f);
  this->send_single_client_attribute_(scoped_id, obj->state);
}
#endif

#ifdef USE_SWITCH
void ThingsBoardComponent::on_switch_update(switch_::Switch *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;
  bool state = obj->state;

  std::string scoped_id = this->get_domain_scoped_id_("switch", obj);
  this->send_single_telemetry_(scoped_id, state ? 1.0f : 0.0f);
  this->send_single_client_attribute_(scoped_id, state);
}
#endif

#ifdef USE_NUMBER
void ThingsBoardComponent::on_number_update(number::Number *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;
  float state = obj->state;

  ESP_LOGV(TAG, "Number '%s' updated: %.2f", obj->get_name().c_str(), state);

  std::string scoped_id = this->get_domain_scoped_id_("number", obj);
  this->send_single_telemetry_(scoped_id, state);
  this->send_single_client_attribute_(scoped_id, state);
}
#endif

#ifdef USE_SELECT
void ThingsBoardComponent::on_select_update(select::Select *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;
  const std::string &state = obj->state;
  size_t index = obj->active_index().value_or(0);

  ESP_LOGV(TAG, "Select '%s' updated: %s", obj->get_name().c_str(),
           state.c_str());

  std::string scoped_id = this->get_domain_scoped_id_("select", obj);
  this->send_single_telemetry_(scoped_id, static_cast<float>(index));
  this->send_single_client_attribute_(scoped_id, state);
}
#endif

#ifdef USE_TEXT_SENSOR
void ThingsBoardComponent::on_text_sensor_update(text_sensor::TextSensor *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;
  const std::string &state = obj->state;

  this->send_single_telemetry_(this->get_domain_scoped_id_("text_sensor", obj), state);
}
#endif

#ifdef USE_FAN
void ThingsBoardComponent::on_fan_update(fan::Fan *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  ESP_LOGV(TAG, "Fan '%s' updated: %s", obj->get_name().c_str(),
           obj->state ? "ON" : "OFF");

  std::string scoped_id = this->get_domain_scoped_id_("fan", obj);
  this->send_single_telemetry_(scoped_id, obj->state ? 1.0f : 0.0f);
  this->send_single_client_attribute_(scoped_id, obj->state);
}
#endif

#ifdef USE_LIGHT
void ThingsBoardComponent::on_light_update(light::LightState *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string scoped_id = this->get_domain_scoped_id_("light", obj);
  this->send_single_telemetry_(scoped_id, obj->remote_values.is_on() ? 1.0f : 0.0f);
  this->send_single_client_attribute_(scoped_id, obj->remote_values.is_on());
}
#endif

#ifdef USE_COVER
void ThingsBoardComponent::on_cover_update(cover::Cover *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  ESP_LOGV(TAG, "Cover '%s' updated: position=%.2f", obj->get_name().c_str(),
           obj->position);

  std::string scoped_id = this->get_domain_scoped_id_("cover", obj);
  this->send_single_telemetry_(scoped_id, obj->position);
  this->send_single_client_attribute_(scoped_id, obj->position);
}
#endif

#ifdef USE_CLIMATE
void ThingsBoardComponent::on_climate_update(climate::Climate *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  ESP_LOGV(TAG, "Climate '%s' updated: target=%.1f", obj->get_name().c_str(),
           obj->target_temperature);

  std::string scoped_id = this->get_domain_scoped_id_("climate", obj);
  this->send_single_telemetry_(scoped_id, obj->target_temperature);
  this->send_single_client_attribute_(scoped_id, obj->target_temperature);
}
#endif

#ifdef USE_TEXT
void ThingsBoardComponent::on_text_update(text::Text *obj,
                                          const std::string &state) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string scoped_id = this->get_domain_scoped_id_("text", obj);
  this->send_single_telemetry_(scoped_id, state);
  this->send_single_client_attribute_(scoped_id, state);
}
#endif

#ifdef USE_DATETIME_DATE
void ThingsBoardComponent::on_date_update(datetime::DateEntity *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string date_str =
      str_sprintf("%04d-%02d-%02d", obj->year, obj->month, obj->day);
  std::string scoped_id = this->get_domain_scoped_id_("date", obj);
  this->send_single_telemetry_(scoped_id, date_str);
  this->send_single_client_attribute_(scoped_id, date_str);
}
#endif

#ifdef USE_DATETIME_TIME
void ThingsBoardComponent::on_time_update(datetime::TimeEntity *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string time_str =
      str_sprintf("%02d:%02d:%02d", obj->hour, obj->minute, obj->second);
  std::string scoped_id = this->get_domain_scoped_id_("time", obj);
  this->send_single_telemetry_(scoped_id, time_str);
  this->send_single_client_attribute_(scoped_id, time_str);
}
#endif

#ifdef USE_DATETIME_DATETIME
void ThingsBoardComponent::on_datetime_update(datetime::DateTimeEntity *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string datetime_str =
      str_sprintf("%04d-%02d-%02d %02d:%02d:%02d", obj->year, obj->month,
                  obj->day, obj->hour, obj->minute, obj->second);
  std::string scoped_id = this->get_domain_scoped_id_("datetime", obj);
  this->send_single_telemetry_(scoped_id, datetime_str);
  this->send_single_client_attribute_(scoped_id, datetime_str);
}
#endif

#ifdef USE_LOCK
void ThingsBoardComponent::on_lock_update(lock::Lock *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  bool is_locked = (obj->state == lock::LOCK_STATE_LOCKED);
  std::string scoped_id = this->get_domain_scoped_id_("lock", obj);
  this->send_single_telemetry_(scoped_id, is_locked ? 1.0f : 0.0f);
  this->send_single_client_attribute_(scoped_id, is_locked);
}
#endif

#ifdef USE_VALVE
void ThingsBoardComponent::on_valve_update(valve::Valve *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string scoped_id = this->get_domain_scoped_id_("valve", obj);
  this->send_single_telemetry_(scoped_id, obj->position);
  this->send_single_client_attribute_(scoped_id, obj->position);
}
#endif

#ifdef USE_MEDIA_PLAYER
void ThingsBoardComponent::on_media_player_update(
    media_player::MediaPlayer *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string state_str;
  switch (obj->state) {
  case media_player::MEDIA_PLAYER_STATE_IDLE:
    state_str = "idle";
    break;
  case media_player::MEDIA_PLAYER_STATE_PLAYING:
    state_str = "playing";
    break;
  case media_player::MEDIA_PLAYER_STATE_PAUSED:
    state_str = "paused";
    break;
  default:
    state_str = "unknown";
    break;
  }
  std::string scoped_id = this->get_domain_scoped_id_("media_player", obj);
  this->send_single_telemetry_(scoped_id, state_str);
  this->send_single_client_attribute_(scoped_id, state_str);
}
#endif

#ifdef USE_ALARM_CONTROL_PANEL
void ThingsBoardComponent::on_alarm_control_panel_update(
    alarm_control_panel::AlarmControlPanel *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  std::string state_str;
  switch (obj->get_state()) {
  case alarm_control_panel::ACP_STATE_DISARMED:
    state_str = "disarmed";
    break;
  case alarm_control_panel::ACP_STATE_ARMED_HOME:
    state_str = "armed_home";
    break;
  case alarm_control_panel::ACP_STATE_ARMED_AWAY:
    state_str = "armed_away";
    break;
  case alarm_control_panel::ACP_STATE_ARMED_NIGHT:
    state_str = "armed_night";
    break;
  case alarm_control_panel::ACP_STATE_ARMED_VACATION:
    state_str = "armed_vacation";
    break;
  case alarm_control_panel::ACP_STATE_ARMED_CUSTOM_BYPASS:
    state_str = "armed_custom_bypass";
    break;
  case alarm_control_panel::ACP_STATE_PENDING:
    state_str = "pending";
    break;
  case alarm_control_panel::ACP_STATE_ARMING:
    state_str = "arming";
    break;
  case alarm_control_panel::ACP_STATE_DISARMING:
    state_str = "disarming";
    break;
  case alarm_control_panel::ACP_STATE_TRIGGERED:
    state_str = "triggered";
    break;
  default:
    state_str = "unknown";
    break;
  }
  std::string scoped_id = this->get_domain_scoped_id_("alarm_control_panel", obj);
  this->send_single_telemetry_(scoped_id, state_str);
  this->send_single_client_attribute_(scoped_id, state_str);
}
#endif

#ifdef USE_EVENT
void ThingsBoardComponent::on_event(event::Event *obj,
                                    const std::string &event_type) {
  if (obj->is_internal() || !this->is_connected())
    return;

  this->send_single_telemetry_(this->get_domain_scoped_id_("event", obj), event_type);
}
#endif

#ifdef USE_UPDATE
void ThingsBoardComponent::on_update(update::UpdateEntity *obj) {
  if (obj->is_internal() || !this->is_connected())
    return;

  bool update_available = obj->state == update::UPDATE_STATE_AVAILABLE;
  this->send_single_telemetry_(this->get_domain_scoped_id_("update", obj),
                               update_available ? 1.0f : 0.0f);
}
#endif

void ThingsBoardComponent::process_initial_state_batch_() {
  if (!this->is_connected() || !this->initial_state_iterator_) {
    ESP_LOGD(TAG,
             "process_initial_state_batch_: not connected or iterator null");
    return;
  }

  ESP_LOGD(TAG, "process_initial_state_batch_: starting, completed=%s",
           this->initial_state_iterator_->completed() ? "true" : "false");

  size_t count = 0;
  while (!this->initial_state_iterator_->completed() &&
         count < MAX_INITIAL_PER_BATCH) {
    ESP_LOGD(TAG, "process_initial_state_batch_: calling advance(), count=%zu",
             count);
    this->initial_state_iterator_->advance();
    count++;
    ESP_LOGD(TAG, "process_initial_state_batch_: after advance(), completed=%s",
             this->initial_state_iterator_->completed() ? "true" : "false");
  }

  ESP_LOGD(TAG, "Processed %zu entities in initial state batch, completed=%s",
           count,
           this->initial_state_iterator_->completed() ? "true" : "false");
}

void ThingsBoardComponent::send_all_components_telemetry_() {
  if (!this->is_connected()) {
    return;
  }

  ESP_LOGD(TAG, "Sending telemetry for all components");

#ifdef USE_SENSOR
  for (auto *obj : App.get_sensors()) {
    if (!obj->is_internal() && !std::isnan(obj->state)) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("sensor", obj), obj->state);
    }
  }
#endif

#ifdef USE_BINARY_SENSOR
  for (auto *obj : App.get_binary_sensors()) {
    if (!obj->is_internal() && obj->has_state()) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("binary_sensor", obj),
                                   obj->state ? 1.0f : 0.0f);
    }
  }
#endif

#ifdef USE_SWITCH
  for (auto *obj : App.get_switches()) {
    if (!obj->is_internal()) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("switch", obj),
                                   obj->state ? 1.0f : 0.0f);
    }
  }
#endif

#ifdef USE_NUMBER
  for (auto *obj : App.get_numbers()) {
    if (!obj->is_internal() && !std::isnan(obj->state)) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("number", obj), obj->state);
    }
  }
#endif

#ifdef USE_SELECT
  for (auto *obj : App.get_selects()) {
    if (!obj->is_internal() && obj->has_state()) {
      size_t index = obj->active_index().value_or(0);
      this->send_single_telemetry_(this->get_domain_scoped_id_("select", obj),
                                   static_cast<float>(index));
    }
  }
#endif

#ifdef USE_TEXT_SENSOR
  for (auto *obj : App.get_text_sensors()) {
    if (!obj->is_internal() && obj->has_state()) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("text_sensor", obj), obj->state);
    }
  }
#endif

#ifdef USE_FAN
  for (auto *obj : App.get_fans()) {
    if (!obj->is_internal()) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("fan", obj),
                                   obj->state ? 1.0f : 0.0f);
    }
  }
#endif

#ifdef USE_LIGHT
  for (auto *obj : App.get_lights()) {
    if (!obj->is_internal()) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("light", obj),
                                   obj->remote_values.is_on() ? 1.0f : 0.0f);
    }
  }
#endif

#ifdef USE_COVER
  for (auto *obj : App.get_covers()) {
    if (!obj->is_internal() && !std::isnan(obj->position)) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("cover", obj), obj->position);
    }
  }
#endif

#ifdef USE_CLIMATE
  for (auto *obj : App.get_climates()) {
    if (!obj->is_internal() && !std::isnan(obj->target_temperature)) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("climate", obj),
                                   obj->target_temperature);
    }
  }
#endif

#ifdef USE_TEXT
  for (auto *obj : App.get_texts()) {
    if (!obj->is_internal()) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("text", obj), obj->state);
    }
  }
#endif

#ifdef USE_DATETIME_DATE
  for (auto *obj : App.get_dates()) {
    if (!obj->is_internal()) {
      std::string date_str =
          str_sprintf("%04d-%02d-%02d", obj->year, obj->month, obj->day);
      this->send_single_telemetry_(this->get_domain_scoped_id_("date", obj), date_str);
    }
  }
#endif

#ifdef USE_DATETIME_TIME
  for (auto *obj : App.get_times()) {
    if (!obj->is_internal()) {
      std::string time_str =
          str_sprintf("%02d:%02d:%02d", obj->hour, obj->minute, obj->second);
      this->send_single_telemetry_(this->get_domain_scoped_id_("time", obj), time_str);
    }
  }
#endif

#ifdef USE_DATETIME_DATETIME
  for (auto *obj : App.get_datetimes()) {
    if (!obj->is_internal()) {
      std::string datetime_str =
          str_sprintf("%04d-%02d-%02d %02d:%02d:%02d", obj->year, obj->month,
                      obj->day, obj->hour, obj->minute, obj->second);
      this->send_single_telemetry_(this->get_domain_scoped_id_("datetime", obj), datetime_str);
    }
  }
#endif

#ifdef USE_LOCK
  for (auto *obj : App.get_locks()) {
    if (!obj->is_internal()) {
      bool is_locked = (obj->state == lock::LOCK_STATE_LOCKED);
      this->send_single_telemetry_(this->get_domain_scoped_id_("lock", obj),
                                   is_locked ? 1.0f : 0.0f);
    }
  }
#endif

#ifdef USE_VALVE
  for (auto *obj : App.get_valves()) {
    if (!obj->is_internal()) {
      this->send_single_telemetry_(this->get_domain_scoped_id_("valve", obj), obj->position);
    }
  }
#endif

#ifdef USE_MEDIA_PLAYER
  for (auto *obj : App.get_media_players()) {
    if (!obj->is_internal()) {
      std::string state_str;
      switch (obj->state) {
      case media_player::MEDIA_PLAYER_STATE_IDLE:
        state_str = "idle";
        break;
      case media_player::MEDIA_PLAYER_STATE_PLAYING:
        state_str = "playing";
        break;
      case media_player::MEDIA_PLAYER_STATE_PAUSED:
        state_str = "paused";
        break;
      default:
        state_str = "unknown";
        break;
      }
      this->send_single_telemetry_(this->get_domain_scoped_id_("media_player", obj), state_str);
    }
  }
#endif

#ifdef USE_ALARM_CONTROL_PANEL
  for (auto *obj : App.get_alarm_control_panels()) {
    if (!obj->is_internal()) {
      std::string state_str;
      switch (obj->get_state()) {
      case alarm_control_panel::ACP_STATE_DISARMED:
        state_str = "disarmed";
        break;
      case alarm_control_panel::ACP_STATE_ARMED_HOME:
        state_str = "armed_home";
        break;
      case alarm_control_panel::ACP_STATE_ARMED_AWAY:
        state_str = "armed_away";
        break;
      case alarm_control_panel::ACP_STATE_ARMED_NIGHT:
        state_str = "armed_night";
        break;
      case alarm_control_panel::ACP_STATE_ARMED_VACATION:
        state_str = "armed_vacation";
        break;
      case alarm_control_panel::ACP_STATE_ARMED_CUSTOM_BYPASS:
        state_str = "armed_custom_bypass";
        break;
      case alarm_control_panel::ACP_STATE_PENDING:
        state_str = "pending";
        break;
      case alarm_control_panel::ACP_STATE_ARMING:
        state_str = "arming";
        break;
      case alarm_control_panel::ACP_STATE_DISARMING:
        state_str = "disarming";
        break;
      case alarm_control_panel::ACP_STATE_TRIGGERED:
        state_str = "triggered";
        break;
      default:
        state_str = "unknown";
        break;
      }
      this->send_single_telemetry_(this->get_domain_scoped_id_("alarm_control_panel", obj), state_str);
    }
  }
#endif

#ifdef USE_UPDATE
  for (auto *obj : App.get_updates()) {
    if (!obj->is_internal()) {
      bool update_available = obj->state == update::UPDATE_STATE_AVAILABLE;
      this->send_single_telemetry_(this->get_domain_scoped_id_("update", obj),
                                   update_available ? 1.0f : 0.0f);
    }
  }
#endif

  ESP_LOGD(TAG, "Completed sending telemetry for all components");
}

void ThingsBoardComponent::request_session_limits_() {
  // getSessionLimits is MQTT-only per TB HTTP API spec.
#ifndef USE_THINGSBOARD_MQTT_TRANSPORT
  return;
#endif
  if (!this->is_connected()) {
    return;
  }

  static uint32_t request_counter = 0;
  std::string request_id = std::to_string(++request_counter);

  std::string method = "getSessionLimits";
  std::string params = "{}";

  ESP_LOGD(TAG, "Requesting session limits via RPC");
  if (this->transport_ != nullptr) {
    this->transport_->publish_rpc_request(request_id, method, params);
  }
}

void ThingsBoardComponent::handle_session_limits_response_(
    const std::string &response) {
  ESP_LOGD(TAG, "Received session limits response: %s", response.c_str());

  json::parse_json(response, [this](JsonObject root) -> bool {
    if (root["maxPayloadSize"].is<int>()) {
      this->rate_limits_.max_payload_size_ = root["maxPayloadSize"].as<int>();
    }
    if (root["maxInflightMessages"].is<int>()) {
      this->rate_limits_.max_inflight_messages_ =
          root["maxInflightMessages"].as<int>();
    }
    if (root["rateLimits"].is<JsonObject>()) {
      JsonObject rate_limits = root["rateLimits"];
      if (rate_limits["messages"].is<const char *>()) {
        this->rate_limits_.messages_rate_limit_ =
            rate_limits["messages"].as<std::string>();
      }
      if (rate_limits["telemetryMessages"].is<const char *>()) {
        this->rate_limits_.telemetry_messages_rate_limit_ =
            rate_limits["telemetryMessages"].as<std::string>();
      }
      if (rate_limits["telemetryDataPoints"].is<const char *>()) {
        this->rate_limits_.telemetry_data_points_rate_limit_ =
            rate_limits["telemetryDataPoints"].as<std::string>();
      }
    }

    this->rate_limits_.limits_received_ = true;
    ESP_LOGI(TAG, "Session limits: maxPayload=%u, maxInflight=%u",
             this->rate_limits_.max_payload_size_,
             this->rate_limits_.max_inflight_messages_);
    ESP_LOGD(TAG,
             "Rate limits: messages=%s, telemetryMsgs=%s, telemetryPoints=%s",
             this->rate_limits_.messages_rate_limit_.c_str(),
             this->rate_limits_.telemetry_messages_rate_limit_.c_str(),
             this->rate_limits_.telemetry_data_points_rate_limit_.c_str());
    return true;
  });
}

bool ThingsBoardComponent::check_rate_limits_() {
  if (!this->rate_limits_.limits_received_) {
    return true;
  }

  const uint32_t now = millis();
  if (now - this->last_batch_process_ < this->batch_delay_) {
    return false;
  }

  return true;
}

void ThingsBoardComponent::add_to_batch_(const std::string &key,
                                         const std::string &value,
                                         bool is_attribute) {
  uint32_t now = millis();

  // Per-key throttle: telemetry only; throttle gates new entries, but an
  // already-pending value still gets refreshed below.
  bool throttled = false;
  if (!is_attribute && this->telemetry_throttle_ > 0) {
    auto throttle_it = this->last_key_send_.find(key);
    if (throttle_it != this->last_key_send_.end()) {
      uint32_t elapsed = now - throttle_it->second;
      throttled = (elapsed < this->telemetry_throttle_);
    }
    if (!throttled) {
      this->last_key_send_[key] = now;
    }
  }

  auto it = this->pending_messages_.find(key);
  if (it != this->pending_messages_.end()) {
    it->second.value = value;
    it->second.timestamp = now;
  } else if (!throttled) {
    PendingMessage msg;
    msg.key = key;
    msg.value = value;
    msg.timestamp = now;
    msg.is_attribute = is_attribute;
    this->pending_messages_[key] = msg;
  }

  if (this->last_batch_process_ == 0) {
    this->last_batch_process_ = now;
  }
}

void ThingsBoardComponent::process_batch_() {
  if (this->pending_messages_.empty()) {
    return;
  }

  if (!this->check_rate_limits_()) {
    return;
  }

  if (!this->is_connected()) {
    this->clear_batch_();
    return;
  }

  size_t telemetry_count = 0;
  size_t attribute_count = 0;

  std::string telemetry_payload = json::build_json([&](JsonObject root) {
    for (const auto &kv : this->pending_messages_) {
      const auto &msg = kv.second;
      if (msg.is_attribute) continue;
      root[msg.key] = serialized(msg.value);
      telemetry_count++;
    }
  });

  std::string attributes_payload = json::build_json([&](JsonObject root) {
    for (const auto &kv : this->pending_messages_) {
      const auto &msg = kv.second;
      if (!msg.is_attribute) continue;
      root[msg.key] = serialized(msg.value);
      attribute_count++;
    }
  });

  this->pending_messages_.clear();

  if (telemetry_count > 0 && this->transport_ != nullptr) {
    this->transport_->publish_telemetry(telemetry_payload);
    ESP_LOGD(TAG, "Sent batched telemetry: %zu keys", telemetry_count);
  }

  if (attribute_count > 0 && this->transport_ != nullptr) {
    this->transport_->publish_client_attributes(attributes_payload);
    ESP_LOGV(TAG, "Sent batched attributes: %s", attributes_payload.c_str());
  }

  this->last_batch_process_ = millis();
}

void ThingsBoardComponent::clear_batch_() {
  this->pending_messages_.clear();
  this->last_batch_process_ = 0;
}

void ThingsBoardComponent::send_status_telemetry_(
    const std::string &status, const std::string &error_code) {
  if (!this->is_connected() || this->transport_ == nullptr) {
    return;
  }

  // domain.object_id keys match component telemetry naming.
  std::string payload = json::build_json([&](JsonObject root) {
    root["device.status"] = status;
    if (!error_code.empty()) {
      root["device.error_code"] = error_code;
    }
  });
  this->transport_->publish_telemetry(payload);
}

void ThingsBoardComponent::send_warning_status_(const std::string &message) {
  this->status_set_warning();
  this->send_status_telemetry_("warning", message);
  ESP_LOGW(TAG, "Warning status sent: %s", message.c_str());
}

void ThingsBoardComponent::send_error_status_(const std::string &message,
                                              const std::string &error_code) {
  this->status_set_error();
  std::string code = error_code.empty() ? message : error_code;
  this->send_status_telemetry_("error", code);
  ESP_LOGE(TAG, "Error status sent: %s (code: %s)", message.c_str(),
           code.c_str());
}

void ThingsBoardComponent::send_immediate_telemetry_(const std::string &key,
                                                     const std::string &value) {
#ifdef USE_THINGSBOARD_MQTT_TRANSPORT
  if (!this->is_connected() || this->transport_ == nullptr) {
    return;
  }

  std::string payload = "{\"" + key + "\":" + value + "}";
  this->transport_->publish_telemetry(payload);
#endif
}

void ThingsBoardComponent::send_immediate_rpc_response_(
    const std::string &request_id, const std::string &response) {
  if (!this->is_connected() || this->transport_ == nullptr) {
    return;
  }

  this->transport_->publish_rpc_response(request_id, response);
}

bool ThingsBoardComponent::claim_device(const std::string &secret_key,
                                        uint32_t duration_ms) {
  if (!this->is_connected()) {
    ESP_LOGW(TAG, "Cannot claim device: not connected");
    return false;
  }

  this->claim_device_(secret_key, duration_ms);
  return true;
}

void ThingsBoardComponent::claim_device_(const std::string &secret_key,
                                         uint32_t duration_ms) {
  // Per https://thingsboard.io/docs/reference/mqtt-api/#claiming-devices both
  // fields are optional; when omitted TB uses defaults.
  std::string payload =
      json::build_json([&](JsonObject root) {
        if (!secret_key.empty()) {
          root["secretKey"] = secret_key;
        }
        if (duration_ms > 0) {
          root["durationMs"] = duration_ms;
        }
      });

  ESP_LOGI(TAG, "Claiming device via transport");
  bool success = this->transport_ != nullptr &&
                 this->transport_->publish_claim(payload);
  if (success) {
    ESP_LOGD(TAG, "Device claim request sent: %s", payload.c_str());
  } else {
    ESP_LOGE(TAG, "Failed to send device claim request");
  }
}

bool ThingsBoardComponent::send_rpc_request(const std::string &method,
                                            const std::string &params) {
  if (!this->is_connected() || this->transport_ == nullptr) {
    ESP_LOGW(TAG, "Cannot send RPC request: not connected");
    return false;
  }

  std::string request_id = std::to_string(++this->rpc_request_counter_);
  ESP_LOGD(TAG, "Sending client-side RPC: method=%s, request_id=%s",
           method.c_str(), request_id.c_str());

  return this->transport_->publish_rpc_request(request_id, method, params);
}

bool ThingsBoardComponent::request_attributes(const std::string &keys) {
  if (!this->is_connected() || this->transport_ == nullptr) {
    ESP_LOGW(TAG, "Cannot request attributes: not connected");
    return false;
  }

  std::string request_id = std::to_string(++this->attribute_request_counter_);
  ESP_LOGD(TAG, "Requesting attributes: keys=%s, request_id=%s", keys.c_str(),
           request_id.c_str());

  return this->transport_->publish_attribute_request(request_id, keys);
}

} // namespace thingsboard
} // namespace esphome
