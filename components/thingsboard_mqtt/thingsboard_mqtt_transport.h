#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include <string>
#include <functional>
#include <map>
#include <mqtt_client.h>
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/thingsboard/transport.h"

namespace esphome {
namespace thingsboard_mqtt_ota {
class ThingsBoardMqttOtaComponent;
}

namespace thingsboard {

class ThingsBoardMQTT : public TBTransport {
 public:
  ThingsBoardMQTT();
  ~ThingsBoardMQTT() override;

  // Parent owns the lifetime of this transport and provides a scheduler
  // anchor for deferring ESP-MQTT-task callbacks onto the main loop. Must be
  // set before connect().
  void set_parent_component(Component *parent) { this->parent_ = parent; }

  void set_broker(const std::string &host, uint16_t port);
  void set_device_token(const std::string &token);
  void set_client_id(const std::string &client_id);
  void set_username(const std::string &username); // For provisioning (set to "provision")

  // MQTT_BASIC credentials (client_id + username + password).
  void set_basic_credentials(const std::string &client_id,
                             const std::string &username,
                             const std::string &password) {
    this->basic_client_id_ = client_id;
    this->basic_username_ = username;
    this->basic_password_ = password;
    this->use_basic_ = true;
  }

  // X.509 mTLS client certificate (PEM) + private key (PEM).
  // Server CA bundle is provided via set_server_ca().
  void set_client_certificate(const std::string &cert_pem,
                              const std::string &key_pem) {
    this->client_cert_pem_ = cert_pem;
    this->client_key_pem_ = key_pem;
    this->use_x509_ = true;
  }
  void set_server_ca(const std::string &ca_pem) {
    this->server_ca_pem_ = ca_pem;
  }

  bool connect();
  bool connect_for_provisioning(); // Connect with username="provision" for device provisioning
  void disconnect();
  bool is_connected() const override;

  bool publish_telemetry(const std::string &payload) override;
  bool publish_attributes(const std::string &payload) override;
  bool publish_client_attributes(const std::string &payload) override;
  bool publish_rpc_response(const std::string &request_id, const std::string &payload) override;
  bool publish_rpc_request(const std::string &request_id, const std::string &method, const std::string &params) override;
  bool publish_attribute_request(const std::string &request_id, const std::string &keys) override;
  bool publish_provision_request(const std::string &payload) override; // Publish to /provision topic
  bool publish_claim(const std::string &payload) override;              // Publish to v1/devices/me/claim
  bool publish(const std::string &topic, const std::string &payload, uint8_t qos = 1, bool retain = false);
  void subscribe_rpc_requests();
  void subscribe_rpc_responses(); // For client-side RPC responses
  void subscribe_shared_attributes();
  void subscribe_attribute_responses(); // For attribute request responses

  // Data-plane callbacks (rpc, shared attrs, attr response, provision response)
  // are inherited from TBTransport.
  void set_on_connect_callback(std::function<void()> callback);
  void set_on_disconnect_callback(std::function<void()> callback);
  void set_on_auth_failure_callback(std::function<void()> callback);

  void subscribe_raw(const std::string &topic, uint8_t qos = 1);

  void set_ota_handler(thingsboard_mqtt_ota::ThingsBoardMqttOtaComponent *h) {
    this->ota_handler_ = h;
  }

  void loop();

 private:
  static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
  void handle_mqtt_event(esp_mqtt_event_handle_t event);
  void handle_mqtt_error(esp_mqtt_event_handle_t event);
  void parse_rpc_request(const char *topic, const char *payload);
  void parse_rpc_response(const char *topic, const char *payload);
  void parse_shared_attributes(const char *topic, const char *payload);
  void parse_attribute_response(const char *topic, const char *payload);

  // Schedules `func` for the next main-loop tick via App.scheduler. Used to
  // hop user-callback dispatch off the ESP-MQTT event task — everything
  // downstream of the callback assumes the ESPHome loop thread.
  void dispatch_on_loop_(std::function<void()> &&func);

  Component *parent_{nullptr};

  std::string broker_host_;
  uint16_t broker_port_{1883};
  std::string device_token_;
  std::string client_id_;
  std::string username_; // For provisioning (set to "provision")
  std::string broker_uri_;  // Store the URI to prevent lifetime issues

  esp_mqtt_client_handle_t mqtt_client_{nullptr};
  bool connected_{false};
  bool provisioning_mode_{false};
  // Username last pushed into esp-mqtt config — used to detect credential rotation
  // and re-push the config before requesting reconnect.
  std::string pushed_username_;

  bool use_basic_{false};
  bool use_x509_{false};
  std::string basic_client_id_;
  std::string basic_username_;
  std::string basic_password_;
  std::string client_cert_pem_;
  std::string client_key_pem_;
  std::string server_ca_pem_;

  std::function<void()> on_connect_callback_;
  std::function<void()> on_disconnect_callback_;
  std::function<void()> on_auth_failure_callback_;

  thingsboard_mqtt_ota::ThingsBoardMqttOtaComponent *ota_handler_{nullptr};

  bool rpc_requests_subscribed_{false};
  bool rpc_responses_subscribed_{false};
  bool shared_attributes_subscribed_{false};
  bool attribute_responses_subscribed_{false};
};

}  // namespace thingsboard
}  // namespace esphome

#endif  // USE_ESP32
