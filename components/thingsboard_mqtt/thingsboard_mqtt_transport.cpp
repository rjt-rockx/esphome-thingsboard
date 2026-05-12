#include "thingsboard_mqtt_transport.h"
#include "esphome/components/thingsboard_mqtt_ota/thingsboard_mqtt_ota.h"

#ifdef USE_ESP32

#include <cstring>
#include <map>
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"

namespace esphome {
namespace thingsboard {

static const char *const TAG = "thingsboard.mqtt";

// ThingsBoard MQTT API topics
static const char *const TELEMETRY_TOPIC = "v1/devices/me/telemetry";
static const char *const ATTRIBUTES_TOPIC = "v1/devices/me/attributes";
static const char *const SHARED_ATTRIBUTES_TOPIC = "v1/devices/me/attributes";
static const char *const RPC_REQUEST_TOPIC = "v1/devices/me/rpc/request/+";
static const char *const RPC_RESPONSE_TOPIC_PREFIX = "v1/devices/me/rpc/response/";
static const char *const RPC_RESPONSE_SUB_TOPIC = "v1/devices/me/rpc/response/+";
static const char *const RPC_REQUEST_PUBLISH_TOPIC_PREFIX = "v1/devices/me/rpc/request/";
static const char *const ATTRIBUTE_REQUEST_TOPIC_PREFIX = "v1/devices/me/attributes/request/";
static const char *const ATTRIBUTE_RESPONSE_SUB_TOPIC = "v1/devices/me/attributes/response/+";
static const char *const PROVISION_TOPIC = "/provision";

bool ThingsBoardMQTT::tls_enabled_() const {
  return this->use_x509_ || !this->server_ca_pem_.empty();
}

void ThingsBoardMQTT::apply_broker_address_(esp_mqtt_client_config_t &cfg) const {
  // ESP-MQTT honours the URI scheme only when broker.address.uri is set; using
  // the discrete hostname/port/transport fields is the SDK-recommended path
  // (mirrors references/thingsboard-client-sdk/src/Espressif_MQTT_Client.h:263).
  cfg.broker.address.hostname = this->broker_host_.c_str();
  cfg.broker.address.port = this->broker_port_;
  cfg.broker.address.transport = this->tls_enabled_()
                                     ? MQTT_TRANSPORT_OVER_SSL
                                     : MQTT_TRANSPORT_OVER_TCP;
}

void ThingsBoardMQTT::apply_credentials_(esp_mqtt_client_config_t &cfg) const {
  if (this->use_x509_) {
    if (!this->server_ca_pem_.empty()) {
      cfg.broker.verification.certificate = this->server_ca_pem_.c_str();
    }
    cfg.credentials.authentication.certificate = this->client_cert_pem_.c_str();
    cfg.credentials.authentication.key = this->client_key_pem_.c_str();
    cfg.credentials.client_id =
        this->client_id_.empty() ? nullptr : this->client_id_.c_str();
  } else if (this->use_basic_) {
    if (!this->server_ca_pem_.empty()) {
      cfg.broker.verification.certificate = this->server_ca_pem_.c_str();
    }
    cfg.credentials.client_id = this->basic_client_id_.c_str();
    cfg.credentials.username = this->basic_username_.c_str();
    cfg.credentials.authentication.password = this->basic_password_.c_str();
  } else {
    if (!this->server_ca_pem_.empty()) {
      cfg.broker.verification.certificate = this->server_ca_pem_.c_str();
    }
    // ACCESS_TOKEN auth: device token is the MQTT username.
    cfg.credentials.username = this->device_token_.c_str();
    cfg.credentials.client_id =
        this->client_id_.empty() ? nullptr : this->client_id_.c_str();
  }
}

ThingsBoardMQTT::ThingsBoardMQTT() {}

ThingsBoardMQTT::~ThingsBoardMQTT() {
  // Destruction runs at shutdown from a non-mqtt task, so stop+destroy is safe here
  // (unlike from inside the mqtt task — see disconnect()).
  if (this->mqtt_client_ != nullptr) {
    esp_mqtt_client_stop(this->mqtt_client_);
    esp_mqtt_client_destroy(this->mqtt_client_);
    this->mqtt_client_ = nullptr;
  }
}

void ThingsBoardMQTT::dispatch_on_loop_(std::function<void()> &&func) {
  if (this->parent_ == nullptr) {
    // No scheduler anchor (test/provisioning paths before set_parent_component);
    // fall back to direct invocation — same behaviour as before this hop existed.
    func();
    return;
  }
  App.scheduler.set_timeout(this->parent_, "tb_mqtt_dispatch", 0,
                            std::move(func));
}

void ThingsBoardMQTT::set_broker(const std::string &host, uint16_t port) {
  this->broker_host_ = host;
  this->broker_port_ = port;
}

void ThingsBoardMQTT::set_device_token(const std::string &token) {
  this->device_token_ = token;
}

void ThingsBoardMQTT::set_client_id(const std::string &client_id) {
  this->client_id_ = client_id;
}

void ThingsBoardMQTT::set_username(const std::string &username) {
  this->username_ = username;
}

bool ThingsBoardMQTT::connect() {
  if (this->broker_host_.empty()) {
    ESP_LOGE(TAG, "MQTT broker host not set");
    return false;
  }
  // X.509 mTLS authenticates via client certificate, so a device token isn't
  // required. MQTT_BASIC has its own credentials. Only bare ACCESS_TOKEN auth
  // needs the token at this point.
  if (!this->use_x509_ && !this->use_basic_ && this->device_token_.empty()) {
    ESP_LOGE(TAG, "MQTT device token not set (ACCESS_TOKEN auth requires it)");
    return false;
  }

  if (this->mqtt_client_ != nullptr) {
    // ESP-MQTT runs its own reconnect loop; calling stop+destroy from arbitrary
    // contexts races with the mqtt task and panics. Re-push config if the token
    // has rotated so the next reconnect presents fresh credentials, then nudge.
    this->provisioning_mode_ = false;
    if (this->pushed_username_ != this->device_token_) {
      ESP_LOGI(TAG, "Device token rotated, pushing new MQTT credentials");
      esp_mqtt_client_config_t mqtt_cfg = {};
      this->apply_broker_address_(mqtt_cfg);
      this->apply_credentials_(mqtt_cfg);
      mqtt_cfg.network.timeout_ms = 10000;
      mqtt_cfg.session.keepalive = 60;
      mqtt_cfg.buffer.size = 4 * 1024;
      esp_err_t err = esp_mqtt_set_config(this->mqtt_client_, &mqtt_cfg);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_set_config failed: %s", esp_err_to_name(err));
        return false;
      }
      this->pushed_username_ = this->device_token_;
    }
    ESP_LOGD(TAG, "MQTT client already initialised, requesting reconnect");
    esp_mqtt_client_reconnect(this->mqtt_client_);
    return true;
  }

  this->provisioning_mode_ = false;

  ESP_LOGI(TAG, "Connecting to ThingsBoard MQTT broker: %s:%d (%s)",
           this->broker_host_.c_str(), this->broker_port_,
           this->tls_enabled_() ? "TLS" : "plaintext");
  ESP_LOGI(TAG, "Using device token: %s", this->device_token_.c_str());

  esp_mqtt_client_config_t mqtt_cfg = {};
  this->apply_broker_address_(mqtt_cfg);
  this->apply_credentials_(mqtt_cfg);
  mqtt_cfg.network.timeout_ms = 10000;
  mqtt_cfg.session.keepalive = 60;
  // Must fit a full OTA chunk (chunk_size=4096) plus topic+MQTT framing.
  // Smaller buffers cause ESP-MQTT to fragment chunks across events; we reject
  // fragmented chunks rather than reassemble (see MQTT_EVENT_DATA handler).
  mqtt_cfg.buffer.size = 8 * 1024;

  ESP_LOGD(TAG, "MQTT config: buffer_size=%d, timeout=%d, keepalive=%d, auth=%s, transport=%s",
           mqtt_cfg.buffer.size, mqtt_cfg.network.timeout_ms, mqtt_cfg.session.keepalive,
           this->use_x509_ ? "X509" : this->use_basic_ ? "MQTT_BASIC" : "ACCESS_TOKEN",
           this->tls_enabled_() ? "TLS" : "TCP");

  this->mqtt_client_ = esp_mqtt_client_init(&mqtt_cfg);
  if (this->mqtt_client_ == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize MQTT client");
    return false;
  }
  this->pushed_username_ = this->use_basic_ ? this->basic_username_ : this->device_token_;

  esp_mqtt_client_register_event(this->mqtt_client_, MQTT_EVENT_ANY, mqtt_event_handler, this);

  esp_err_t err = esp_mqtt_client_start(this->mqtt_client_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
    esp_mqtt_client_destroy(this->mqtt_client_);
    this->mqtt_client_ = nullptr;
    return false;
  }

  return true;
}

bool ThingsBoardMQTT::connect_for_provisioning() {
  if (this->mqtt_client_ != nullptr) {
    ESP_LOGW(TAG, "MQTT client already exists, disconnecting first");
    disconnect();
  }

  if (this->broker_host_.empty()) {
    ESP_LOGE(TAG, "MQTT broker host not set");
    return false;
  }

  ESP_LOGI(TAG, "Connecting to ThingsBoard MQTT broker for provisioning: %s:%d (%s)",
           this->broker_host_.c_str(), this->broker_port_,
           this->tls_enabled_() ? "TLS" : "plaintext");

  this->provisioning_mode_ = true;

  esp_mqtt_client_config_t mqtt_cfg = {};
  this->apply_broker_address_(mqtt_cfg);
  // Provisioning is access-token-style (username="provision"); we still want the
  // server CA to apply if the YAML configured TLS for the production session.
  if (!this->server_ca_pem_.empty()) {
    mqtt_cfg.broker.verification.certificate = this->server_ca_pem_.c_str();
  }
  // Per TB MQTT API: username must be "provision" for the provisioning session.
  mqtt_cfg.credentials.username = "provision";
  mqtt_cfg.credentials.client_id = this->client_id_.empty() ? "provision" : this->client_id_.c_str();
  mqtt_cfg.network.timeout_ms = 10000;
  mqtt_cfg.session.keepalive = 60;
  mqtt_cfg.buffer.size = 4*1024;
  
  ESP_LOGD(TAG, "MQTT provisioning config: buffer_size=%d, timeout=%d, keepalive=%d", 
           mqtt_cfg.buffer.size, mqtt_cfg.network.timeout_ms, mqtt_cfg.session.keepalive);

  this->mqtt_client_ = esp_mqtt_client_init(&mqtt_cfg);
  if (this->mqtt_client_ == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize MQTT client for provisioning");
    this->provisioning_mode_ = false;
    return false;
  }
  this->pushed_username_ = "provision";

  esp_mqtt_client_register_event(this->mqtt_client_, MQTT_EVENT_ANY, mqtt_event_handler, this);
  
  esp_err_t err = esp_mqtt_client_start(this->mqtt_client_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start MQTT client for provisioning: %s", esp_err_to_name(err));
    esp_mqtt_client_destroy(this->mqtt_client_);
    this->mqtt_client_ = nullptr;
    this->provisioning_mode_ = false;
    return false;
  }

  return true;
}

void ThingsBoardMQTT::disconnect() {
  if (this->mqtt_client_ != nullptr) {
    ESP_LOGI(TAG, "Disconnecting from ThingsBoard MQTT broker");
    // esp_mqtt_client_disconnect is safe from any task (including the mqtt task
    // itself); stop+destroy is not. Keep the client alive so a later connect()
    // can reconnect on the same handle.
    esp_mqtt_client_disconnect(this->mqtt_client_);
  }
  this->connected_ = false;
  this->provisioning_mode_ = false;
}

bool ThingsBoardMQTT::is_connected() const {
  return this->connected_;
}

bool ThingsBoardMQTT::publish_telemetry(const std::string &payload) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot publish telemetry");
    return false;
  }

  ESP_LOGV(TAG, "Publishing telemetry (size: %zu bytes)", payload.length());
  int msg_id = esp_mqtt_client_publish(this->mqtt_client_, TELEMETRY_TOPIC, 
                                      payload.c_str(), payload.length(), 1, 0);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to publish telemetry (size: %zu bytes)", payload.length());
    return false;
  }

  ESP_LOGV(TAG, "Published telemetry (msg_id=%d, size: %zu bytes): %s", msg_id, payload.length(), payload.c_str());
  return true;
}

bool ThingsBoardMQTT::publish_attributes(const std::string &payload) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot publish attributes");
    return false;
  }

  int msg_id = esp_mqtt_client_publish(this->mqtt_client_, ATTRIBUTES_TOPIC,
                                      payload.c_str(), payload.length(), 1, 0);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to publish attributes");
    return false;
  }

  ESP_LOGV(TAG, "Published attributes (msg_id=%d): %s", msg_id, payload.c_str());
  return true;
}

bool ThingsBoardMQTT::publish_client_attributes(const std::string &payload) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot publish client attributes");
    return false;
  }

  int msg_id = esp_mqtt_client_publish(this->mqtt_client_, ATTRIBUTES_TOPIC,
                                      payload.c_str(), payload.length(), 1, 0);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to publish client attributes");
    return false;
  }

  ESP_LOGV(TAG, "Published client attributes (msg_id=%d): %s", msg_id, payload.c_str());
  return true;
}

bool ThingsBoardMQTT::publish_rpc_response(const std::string &request_id, const std::string &payload) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot publish RPC response");
    return false;
  }

  std::string topic = RPC_RESPONSE_TOPIC_PREFIX + request_id;
  int msg_id = esp_mqtt_client_publish(this->mqtt_client_, topic.c_str(), 
                                      payload.c_str(), payload.length(), 1, 0);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to publish RPC response to %s", topic.c_str());
    return false;
  }

  ESP_LOGV(TAG, "Published RPC response (msg_id=%d) to %s: %s", msg_id, topic.c_str(), payload.c_str());
  return true;
}

bool ThingsBoardMQTT::publish_rpc_request(const std::string &request_id, const std::string &method, const std::string &params) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot publish RPC request");
    return false;
  }

  std::string topic = RPC_REQUEST_PUBLISH_TOPIC_PREFIX + request_id;
  std::string payload = "{\"method\":\"" + method + "\",\"params\":" + params + "}";
  
  int msg_id = esp_mqtt_client_publish(this->mqtt_client_, topic.c_str(), 
                                      payload.c_str(), payload.length(), 1, 0);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to publish RPC request to %s", topic.c_str());
    return false;
  }

  ESP_LOGV(TAG, "Published RPC request (msg_id=%d) to %s: %s", msg_id, topic.c_str(), payload.c_str());
  return true;
}

bool ThingsBoardMQTT::publish_attribute_request(const std::string &request_id, const std::string &keys) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot publish attribute request");
    return false;
  }

  // `keys` is already a JSON object per the TBTransport contract
  // (e.g. `{"clientKeys":"a,b","sharedKeys":"c,d"}`); publish verbatim.
  std::string topic = ATTRIBUTE_REQUEST_TOPIC_PREFIX + request_id;
  int msg_id = esp_mqtt_client_publish(this->mqtt_client_, topic.c_str(),
                                       keys.c_str(), keys.length(), 1, 0);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to publish attribute request to %s", topic.c_str());
    return false;
  }

  ESP_LOGD(TAG, "Published attribute request (msg_id=%d) to %s: %s", msg_id, topic.c_str(), keys.c_str());
  return true;
}

bool ThingsBoardMQTT::publish_provision_request(const std::string &payload) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot publish provision request");
    return false;
  }

  ESP_LOGD(TAG, "Publishing provisioning request to %s", PROVISION_TOPIC);
  int msg_id = esp_mqtt_client_publish(this->mqtt_client_, PROVISION_TOPIC, 
                                      payload.c_str(), payload.length(), 1, 0);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to publish provisioning request to %s", PROVISION_TOPIC);
    return false;
  }

  ESP_LOGD(TAG, "Published provisioning request (msg_id=%d): %s", msg_id, payload.c_str());
  return true;
}

bool ThingsBoardMQTT::publish_claim(const std::string &payload) {
  return this->publish("v1/devices/me/claim", payload, 1, false);
}

bool ThingsBoardMQTT::publish(const std::string &topic, const std::string &payload, uint8_t qos, bool retain) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot publish to %s", topic.c_str());
    return false;
  }

  int msg_id = esp_mqtt_client_publish(this->mqtt_client_, topic.c_str(), 
                                      payload.c_str(), payload.length(), qos, retain ? 1 : 0);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to publish to %s", topic.c_str());
    return false;
  }

  ESP_LOGV(TAG, "Published to %s (msg_id=%d, qos=%d): %s", topic.c_str(), msg_id, qos, payload.c_str());
  return true;
}

void ThingsBoardMQTT::subscribe_rpc_requests() {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot subscribe to RPC requests");
    return;
  }

  if (this->rpc_requests_subscribed_) {
    ESP_LOGD(TAG, "Already subscribed to RPC requests");
    return;
  }

  int msg_id = esp_mqtt_client_subscribe(this->mqtt_client_, RPC_REQUEST_TOPIC, 1);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to subscribe to RPC requests");
  } else {
    ESP_LOGI(TAG, "Subscribed to RPC requests (msg_id=%d)", msg_id);
    this->rpc_requests_subscribed_ = true;
  }
}

void ThingsBoardMQTT::subscribe_rpc_responses() {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot subscribe to RPC responses");
    return;
  }

  if (this->rpc_responses_subscribed_) {
    ESP_LOGD(TAG, "Already subscribed to RPC responses");
    return;
  }

  int msg_id = esp_mqtt_client_subscribe(this->mqtt_client_, RPC_RESPONSE_SUB_TOPIC, 1);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to subscribe to RPC responses");
  } else {
    ESP_LOGI(TAG, "Subscribed to RPC responses (msg_id=%d)", msg_id);
    this->rpc_responses_subscribed_ = true;
  }
}

void ThingsBoardMQTT::subscribe_shared_attributes() {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot subscribe to shared attributes");
    return;
  }

  if (this->shared_attributes_subscribed_) {
    ESP_LOGD(TAG, "Already subscribed to shared attributes");
    return;
  }

  int msg_id = esp_mqtt_client_subscribe(this->mqtt_client_, SHARED_ATTRIBUTES_TOPIC, 1);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to subscribe to shared attributes");
  } else {
    ESP_LOGI(TAG, "Subscribed to shared attributes (msg_id=%d)", msg_id);
    this->shared_attributes_subscribed_ = true;
  }
}

void ThingsBoardMQTT::subscribe_attribute_responses() {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot subscribe to attribute responses");
    return;
  }

  if (this->attribute_responses_subscribed_) {
    ESP_LOGD(TAG, "Already subscribed to attribute responses");
    return;
  }

  int msg_id = esp_mqtt_client_subscribe(this->mqtt_client_, ATTRIBUTE_RESPONSE_SUB_TOPIC, 1);
  if (msg_id == -1) {
    ESP_LOGE(TAG, "Failed to subscribe to attribute responses");
  } else {
    ESP_LOGI(TAG, "Subscribed to attribute responses (msg_id=%d)", msg_id);
    this->attribute_responses_subscribed_ = true;
  }
}

void ThingsBoardMQTT::set_on_connect_callback(std::function<void()> callback) {
  this->on_connect_callback_ = std::move(callback);
}

void ThingsBoardMQTT::set_on_disconnect_callback(std::function<void()> callback) {
  this->on_disconnect_callback_ = std::move(callback);
}

void ThingsBoardMQTT::set_on_auth_failure_callback(std::function<void()> callback) {
  this->on_auth_failure_callback_ = std::move(callback);
}

void ThingsBoardMQTT::subscribe_raw(const std::string &topic, uint8_t qos) {
  if (this->mqtt_client_ == nullptr) {
    ESP_LOGW(TAG, "subscribe_raw(%s): client not connected", topic.c_str());
    return;
  }
  int msg_id = esp_mqtt_client_subscribe(this->mqtt_client_, topic.c_str(), qos);
  ESP_LOGD(TAG, "subscribe_raw(%s, qos=%u) -> msg_id=%d", topic.c_str(), qos, msg_id);
}

void ThingsBoardMQTT::loop() {
  // ESP-IDF MQTT client handles events on its own task; nothing to drive here.
}

void ThingsBoardMQTT::mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  auto *mqtt = static_cast<ThingsBoardMQTT *>(handler_args);
  mqtt->handle_mqtt_event(static_cast<esp_mqtt_event_handle_t>(event_data));
}

void ThingsBoardMQTT::handle_mqtt_event(esp_mqtt_event_handle_t event) {
  switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Connected to ThingsBoard MQTT broker");
      this->connected_ = true;
      this->rpc_requests_subscribed_ = false;
      this->rpc_responses_subscribed_ = false;
      this->shared_attributes_subscribed_ = false;
      this->attribute_responses_subscribed_ = false;

      // Subscribe before notifying the core: TCP ordering ensures these
      // SUBSCRIBE packets reach TB before any publish from on_connected_,
      // so an attribute request published immediately afterwards won't lose
      // its response.
      if (!this->provisioning_mode_) {
        this->subscribe_rpc_requests();
        this->subscribe_rpc_responses();
        this->subscribe_shared_attributes();
        this->subscribe_attribute_responses();
      }

      if (this->on_connect_callback_ || this->on_connected_) {
        auto on_connect = this->on_connect_callback_;
        auto on_connected = this->on_connected_;
        this->dispatch_on_loop_([on_connect, on_connected]() {
          if (on_connect) on_connect();
          if (on_connected) on_connected();
        });
      }
      break;

    case MQTT_EVENT_DISCONNECTED:
      ESP_LOGW(TAG, "Disconnected from ThingsBoard MQTT broker");
      this->connected_ = false;
      this->rpc_requests_subscribed_ = false;
      this->rpc_responses_subscribed_ = false;
      this->shared_attributes_subscribed_ = false;
      this->attribute_responses_subscribed_ = false;
      if (this->on_disconnect_callback_ || this->on_disconnected_) {
        auto on_disconnect = this->on_disconnect_callback_;
        auto on_disconnected = this->on_disconnected_;
        this->dispatch_on_loop_([on_disconnect, on_disconnected]() {
          if (on_disconnect) on_disconnect();
          if (on_disconnected) on_disconnected();
        });
      }
      break;

    case MQTT_EVENT_SUBSCRIBED:
      ESP_LOGD(TAG, "Successfully subscribed to topic (msg_id=%d)", event->msg_id);
      break;

    case MQTT_EVENT_UNSUBSCRIBED:
      ESP_LOGD(TAG, "Successfully unsubscribed (msg_id=%d)", event->msg_id);
      break;

    case MQTT_EVENT_PUBLISHED:
      ESP_LOGV(TAG, "Message published successfully (msg_id=%d)", event->msg_id);
      break;

    case MQTT_EVENT_DATA: {
      ESP_LOGD(TAG, "Received MQTT message on topic: %.*s", event->topic_len, event->topic);

      {
        char topic[event->topic_len + 1];
        strncpy(topic, event->topic, event->topic_len);
        topic[event->topic_len] = '\0';

        // OTA chunks travel as binary payloads on `v2/fw/response/{rid}/chunk/{idx}`;
        // route them before the null-terminated payload copy used by JSON paths.
        // ESP-MQTT splits messages larger than buffer.size across events — earlier
        // fragments are not preserved, so we only dispatch when the payload is whole.
        if (strncmp(topic, "v2/fw/response/", 15) == 0) {
          if (this->ota_handler_ != nullptr &&
              event->current_data_offset + event->data_len ==
                  event->total_data_len) {
            const char *suffix = topic + 15;
            uint32_t rid = 0;
            uint32_t idx = 0;
            const char *p = suffix;
            while (*p && *p != '/') { rid = rid * 10 + (*p - '0'); p++; }
            if (*p == '/') p++;
            if (strncmp(p, "chunk/", 6) == 0) {
              p += 6;
              while (*p) { idx = idx * 10 + (*p - '0'); p++; }
              // total_data_len > buffer.size would mean we lost an earlier
              // fragment; fail loud rather than write a truncated image.
              if (event->current_data_offset != 0) {
                ESP_LOGE(TAG,
                         "OTA chunk %u fragmented (offset=%d, total=%d) — "
                         "buffer too small",
                         idx, event->current_data_offset,
                         event->total_data_len);
                break;
              }
              this->ota_handler_->on_chunk_received(
                  rid, idx, reinterpret_cast<const uint8_t *>(event->data),
                  static_cast<size_t>(event->total_data_len));
            }
          }
          break;
        }

        char payload[event->data_len + 1];
        strncpy(payload, event->data, event->data_len);
        payload[event->data_len] = '\0';

        if (strcmp(topic, PROVISION_TOPIC) == 0) {
          ESP_LOGD(TAG, "Received provisioning response: %s", payload);
          if (this->on_provision_response_) {
            auto cb = this->on_provision_response_;
            std::string resp(payload);
            this->dispatch_on_loop_(
                [cb, resp]() { cb(resp); });
          }
        } else if (strncmp(topic, "v1/devices/me/rpc/request/", 26) == 0) {
          this->parse_rpc_request(topic, payload);
        } else if (strncmp(topic, "v1/devices/me/rpc/response/", 28) == 0) {
          this->parse_rpc_response(topic, payload);
        } else if (strcmp(topic, "v1/devices/me/attributes") == 0) {
          this->parse_shared_attributes(topic, payload);
        } else if (strncmp(topic, "v1/devices/me/attributes/response/", 36) == 0) {
          this->parse_attribute_response(topic, payload);
        }
      }
      break;
    }

    case MQTT_EVENT_ERROR:
      ESP_LOGE(TAG, "MQTT error occurred");
      this->handle_mqtt_error(event);
      break;

    default:
      ESP_LOGD(TAG, "Unhandled MQTT event: %d", event->event_id);
      break;
  }
}

void ThingsBoardMQTT::parse_rpc_request(const char *topic, const char *payload) {
  if (!this->on_rpc_request_) {
    return;
  }

  const char *request_prefix = "v1/devices/me/rpc/request/";
  if (strncmp(topic, request_prefix, strlen(request_prefix)) != 0) {
    ESP_LOGW(TAG, "Invalid RPC request topic: %s", topic);
    return;
  }

  std::string request_id = topic + strlen(request_prefix);

  json::parse_json(payload, [this, &request_id](JsonObject root) -> bool {
    if (!root["method"].is<std::string>()) {
      ESP_LOGW(TAG, "RPC request missing 'method' field");
      return false;
    }

    std::string method = root["method"].as<std::string>();
    std::string params = "";
    
    if (root["params"].is<JsonObject>()) {
      JsonObject params_obj = root["params"];
      json::json_build_t params_builder = [&params_obj](JsonObject obj) {
        for (JsonPair p : params_obj) {
          obj[p.key()] = p.value();
        }
      };
      params = json::build_json(params_builder);
    }

    ESP_LOGD(TAG, "RPC request: id=%s, method=%s, params=%s",
             request_id.c_str(), method.c_str(), params.c_str());

    auto cb = this->on_rpc_request_;
    std::string rid = request_id, m = method, p = params;
    this->dispatch_on_loop_(
        [cb, rid, m, p]() { cb(rid, m, p); });
    return true;
  });
}

void ThingsBoardMQTT::parse_rpc_response(const char *topic, const char *payload) {
  if (!this->on_rpc_response_) {
    return;
  }

  const char *response_prefix = "v1/devices/me/rpc/response/";
  if (strncmp(topic, response_prefix, strlen(response_prefix)) != 0) {
    ESP_LOGW(TAG, "Invalid RPC response topic: %s", topic);
    return;
  }

  std::string request_id = topic + strlen(response_prefix);
  ESP_LOGD(TAG, "RPC response: id=%s, payload=%s", request_id.c_str(), payload);

  auto cb = this->on_rpc_response_;
  std::string rid = request_id, resp(payload);
  this->dispatch_on_loop_([cb, rid, resp]() { cb(rid, resp); });
}

void ThingsBoardMQTT::parse_shared_attributes(const char *topic, const char *payload) {
  if (!this->on_shared_attributes_) {
    return;
  }

  ESP_LOGD(TAG, "Received shared attributes: %s", payload);

  std::map<std::string, std::string> attributes;

  json::parse_json(payload, [&attributes](JsonObject root) -> bool {
    for (JsonPair p : root) {
      std::string key = p.key().c_str();
      std::string value;

      if (p.value().is<const char*>()) {
        value = p.value().as<const char*>();
      } else if (p.value().is<bool>()) {
        value = p.value().as<bool>() ? "true" : "false";
      } else if (p.value().is<int>()) {
        value = std::to_string(p.value().as<int>());
      } else if (p.value().is<float>()) {
        value = std::to_string(p.value().as<float>());
      } else {
        if (p.value().is<const char*>()) {
          value = p.value().as<const char*>();
        } else {
          value = "unknown";
        }
      }

      attributes[key] = value;
      ESP_LOGV(TAG, "Shared attribute: %s = %s", key.c_str(), value.c_str());
    }
    return true;
  });

  if (!attributes.empty()) {
    auto cb = this->on_shared_attributes_;
    this->dispatch_on_loop_([cb, attributes]() { cb(attributes); });
  }
}

void ThingsBoardMQTT::parse_attribute_response(const char *topic, const char *payload) {
  if (!this->on_attribute_response_) {
    return;
  }

  const char *response_prefix = "v1/devices/me/attributes/response/";
  if (strncmp(topic, response_prefix, strlen(response_prefix)) != 0) {
    ESP_LOGW(TAG, "Invalid attribute response topic: %s", topic);
    return;
  }

  std::string request_id = topic + strlen(response_prefix);
  ESP_LOGD(TAG, "Attribute response: id=%s, payload=%s", request_id.c_str(), payload);

  std::map<std::string, std::string> attributes;

  json::parse_json(payload, [&attributes](JsonObject root) -> bool {
    if (root["shared"].is<JsonObject>()) {
      JsonObject shared = root["shared"];
      for (JsonPair p : shared) {
        std::string key = p.key().c_str();
        std::string value;
        if (p.value().is<const char*>()) {
          value = p.value().as<const char*>();
        } else if (p.value().is<bool>()) {
          value = p.value().as<bool>() ? "true" : "false";
        } else if (p.value().is<int>()) {
          value = std::to_string(p.value().as<int>());
        } else if (p.value().is<float>()) {
          value = std::to_string(p.value().as<float>());
        } else {
          value = "unknown";
        }
        attributes[key] = value;
      }
    }
    // Flat (non-nested) attribute responses.
    for (JsonPair p : root) {
      if (strcmp(p.key().c_str(), "shared") != 0 && 
          strcmp(p.key().c_str(), "client") != 0 && 
          strcmp(p.key().c_str(), "server") != 0) {
        std::string key = p.key().c_str();
        std::string value;
        if (p.value().is<const char*>()) {
          value = p.value().as<const char*>();
        } else if (p.value().is<bool>()) {
          value = p.value().as<bool>() ? "true" : "false";
        } else if (p.value().is<int>()) {
          value = std::to_string(p.value().as<int>());
        } else if (p.value().is<float>()) {
          value = std::to_string(p.value().as<float>());
        } else {
          value = "unknown";
        }
        attributes[key] = value;
      }
    }
    return true;
  });

  if (!attributes.empty()) {
    auto cb = this->on_attribute_response_;
    std::string rid = request_id;
    this->dispatch_on_loop_([cb, rid, attributes]() { cb(rid, attributes); });
  }
}

void ThingsBoardMQTT::handle_mqtt_error(esp_mqtt_event_handle_t event) {
  if (event->error_handle == nullptr) {
    ESP_LOGE(TAG, "MQTT error occurred but no error details available");
    return;
  }

  ESP_LOGE(TAG, "MQTT error type: %d", event->error_handle->error_type);

  switch (event->error_handle->error_type) {
    case MQTT_ERROR_TYPE_TCP_TRANSPORT:
      ESP_LOGE(TAG, "TCP transport error - connection issues");
      if (event->error_handle->esp_tls_last_esp_err != 0) {
        ESP_LOGE(TAG, "ESP-TLS error: %s", esp_err_to_name(event->error_handle->esp_tls_last_esp_err));
      }
      if (event->error_handle->esp_transport_sock_errno != 0) {
        ESP_LOGE(TAG, "Socket error: %s", strerror(event->error_handle->esp_transport_sock_errno));
      }
      break;

    case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
      // Includes MQTT CONNACK auth failures (0x04, 0x05).
      ESP_LOGE(TAG, "Connection refused by broker");
      this->connected_ = false;
      if (this->on_auth_failure_callback_) {
        ESP_LOGW(TAG, "Triggering authentication failure callback - device token may be invalid");
        auto cb = this->on_auth_failure_callback_;
        this->dispatch_on_loop_([cb]() { cb(); });
      }
      break;

    case MQTT_ERROR_TYPE_NONE:
      ESP_LOGE(TAG, "MQTT error type NONE - unexpected");
      break;
      
    default:
      ESP_LOGE(TAG, "Unknown MQTT error type: %d", event->error_handle->error_type);
      break;
  }
}

}  // namespace thingsboard
}  // namespace esphome

#endif  // USE_ESP32
