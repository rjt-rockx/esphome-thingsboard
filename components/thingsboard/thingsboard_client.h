#pragma once

#include "control_iterator.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/component_iterator.h"
#include "esphome/core/controller.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/util.h"
#include "esphome/core/version.h"
#include "esphome/core/defines.h"
#include "transport.h"
#ifdef USE_NETWORK
#include "esphome/components/network/util.h"
#endif
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace esphome {

namespace thingsboard_http_ota {
class ThingsBoardHttpOtaComponent;
}

namespace thingsboard {

// Full header is included only in the .cpp so MQTT internals stay out of the
// public header.
class ThingsBoardMQTT;

class ThingsBoardComponent
    : public Component,
      public Controller,
      public Parented<http_request::HttpRequestComponent> {
public:
  ThingsBoardComponent();
  ~ThingsBoardComponent();

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::WIFI - 1.0f;
  }

  void set_server_url(const std::string &server_url) {
    server_url_ = server_url;
  }
  void set_device_name(const std::string &device_name) {
    device_name_ = device_name;
    device_name_explicitly_set_ = true;
  }
  void set_provisioning_key(const std::string &provisioning_key) {
    provisioning_key_ = provisioning_key;
  }
  void set_provisioning_secret(const std::string &provisioning_secret) {
    provisioning_secret_ = provisioning_secret;
  }
  // When set, the provisioning request payload includes `credentialsType` plus
  // the matching value field.
  void set_provisioning_credentials_type(const std::string &t) {
    provisioning_credentials_type_ = t;
  }
  void set_provisioning_credentials_token(const std::string &t) {
    provisioning_credentials_token_ = t;
  }
  void set_provisioning_credentials_client_id(const std::string &v) {
    provisioning_credentials_client_id_ = v;
  }
  void set_provisioning_credentials_username(const std::string &v) {
    provisioning_credentials_username_ = v;
  }
  void set_provisioning_credentials_password(const std::string &v) {
    provisioning_credentials_password_ = v;
  }
  void set_provisioning_credentials_cert_pem(const std::string &v) {
    provisioning_credentials_cert_pem_ = v;
  }
  void set_timeout(uint32_t timeout) { timeout_ = timeout; }

  void set_telemetry_interval(uint32_t interval) {
    telemetry_interval_ = interval;
  }
  void set_telemetry_throttle(uint32_t throttle) {
    telemetry_throttle_ = throttle;
  }
  void set_periodic_sync_interval(uint32_t interval) {
    all_telemetry_interval_ = interval;
  }
  void set_offline_queue_max(uint32_t n) { offline_queue_max_ = n; }
  // When true, emit `[{"ts": <ms>, "values": {...}}]` so replays after a
  // disconnect retain the original capture timestamp instead of inheriting
  // TB's server-receive time.
  void set_use_client_timestamps(bool v) { use_client_timestamps_ = v; }

  void set_mqtt_broker(const std::string &broker) { mqtt_broker_ = broker; }
  void set_mqtt_port(uint16_t port) { mqtt_port_ = port; }
  // MQTT_BASIC credentials (TB MQTT API: client_id + username + password).
  void set_mqtt_basic_credentials(const std::string &client_id,
                                  const std::string &username,
                                  const std::string &password) {
    mqtt_basic_client_id_ = client_id;
    mqtt_basic_username_ = username;
    mqtt_basic_password_ = password;
    mqtt_use_basic_ = true;
  }
  // X.509 mTLS client certificate + private key (PEM).
  void set_mqtt_client_certificate(const std::string &cert_pem,
                                   const std::string &key_pem) {
    mqtt_client_cert_pem_ = cert_pem;
    mqtt_client_key_pem_ = key_pem;
    mqtt_use_x509_ = true;
  }
  // Optional server CA bundle (PEM). Pin alone enables TLS even with
  // ACCESS_TOKEN auth.
  void set_mqtt_server_ca(const std::string &ca_pem) {
    mqtt_server_ca_pem_ = ca_pem;
  }
  void set_device_token(const std::string &token) {
    device_token_ = token;
    access_token_ = token;
  }

  void set_claim_secret_key(const std::string &secret_key) {
    claim_secret_key_ = secret_key;
  }
  void set_claim_duration_ms(uint32_t duration_ms) {
    claim_duration_ms_ = duration_ms;
  }

  bool send_telemetry(const std::string &data);
  bool send_attributes(const std::string &data);
  bool send_client_attributes(const std::string &data);
  void clear_device_token();
  // Persist token to NVS and adopt at runtime.
  void set_device_token_persistent(const std::string &token);

  bool claim_device(const std::string &secret_key = "",
                    uint32_t duration_ms = 0);

  bool send_rpc_request(const std::string &method, const std::string &params);

  bool request_attributes(const std::string &keys);

  std::string get_server_url() const { return server_url_; }

#ifdef USE_ESP32
  bool is_connected() const {
    return transport_ != nullptr && transport_->is_connected();
  }
#else
  bool is_connected() const { return false; }
#endif

  // Sibling transport components call this from their own setup() to register
  // themselves.
  void register_transport(TBTransport *transport) {
    this->transport_ = transport;
    if (transport == nullptr) return;
    transport->set_on_connected([this]() { this->dispatch_connected(); });
    transport->set_on_disconnected(
        [this]() { this->dispatch_disconnected(); });
    transport->set_on_rpc_request(
        [this](const std::string &id, const std::string &method,
               const std::string &params) {
          this->dispatch_rpc_request(id, method, params);
        });
    transport->set_on_shared_attributes(
        [this](const std::map<std::string, std::string> &attrs) {
          this->dispatch_shared_attributes(attrs);
        });
    transport->set_on_attribute_response(
        [this](const std::string &id,
               const std::map<std::string, std::string> &attrs) {
          this->dispatch_attribute_response(id, attrs);
        });
    transport->set_on_rpc_response(
        [this](const std::string &id, const std::string &response) {
          this->dispatch_rpc_response(id, response);
        });
    transport->set_on_provision_response(
        [this](const std::string &response_json) {
          this->dispatch_provision_response(response_json);
        });
  }
  TBTransport *get_transport() const { return this->transport_; }

  // Set only when the active transport offers OTA.
  void register_ota_transport(TBOTATransport *t) { this->ota_transport_ = t; }
  TBOTATransport *get_ota_transport() const { return this->ota_transport_; }

  void dispatch_connected();
  void dispatch_disconnected();
  void dispatch_rpc_request(const std::string &request_id,
                            const std::string &method,
                            const std::string &params);
  void dispatch_shared_attributes(
      const std::map<std::string, std::string> &attributes);
  void dispatch_attribute_response(
      const std::string &request_id,
      const std::map<std::string, std::string> &attributes);

  // Bridge fw_*/sw_* shared attributes to the OTA transport. Called from both
  // the shared-attr push path and the on-connect snapshot request so a TB-side
  // OTA assignment is picked up even if the push raced a poll window.
  void maybe_advertise_ota_(
      const std::map<std::string, std::string> &attributes);
  void dispatch_rpc_response(const std::string &request_id,
                             const std::string &response);
  void dispatch_provision_response(const std::string &response_json);
  bool is_provisioned() const {
    return !device_token_.empty() || has_access_token();
  }

  Trigger<> *get_connect_trigger() { return &connect_trigger_; }
  Trigger<> *get_disconnect_trigger() { return &disconnect_trigger_; }
  Trigger<std::string, std::string> *get_rpc_trigger() { return &rpc_trigger_; }
  Trigger<std::map<std::string, std::string>> *get_shared_attributes_trigger() {
    return &shared_attributes_trigger_;
  }
  Trigger<std::string, std::string> *get_rpc_response_trigger() {
    return &rpc_response_trigger_;
  }

#ifdef USE_THINGSBOARD_HTTP_OTA
  void register_ota_component(
      thingsboard_http_ota::ThingsBoardHttpOtaComponent *ota_component) {
    this->control_iterator_.register_ota_component(ota_component);
  }
#endif

#ifdef USE_SENSOR
  void on_sensor_update(sensor::Sensor *obj) override;
#endif
#ifdef USE_BINARY_SENSOR
  void on_binary_sensor_update(binary_sensor::BinarySensor *obj) override;
#endif
#ifdef USE_SWITCH
  void on_switch_update(switch_::Switch *obj) override;
#endif
#ifdef USE_NUMBER
  void on_number_update(number::Number *obj) override;
#endif
#ifdef USE_SELECT
  void on_select_update(select::Select *obj) override;
#endif
#ifdef USE_TEXT_SENSOR
  void on_text_sensor_update(text_sensor::TextSensor *obj) override;
#endif
#ifdef USE_FAN
  void on_fan_update(fan::Fan *obj) override;
#endif
#ifdef USE_LIGHT
  void on_light_update(light::LightState *obj) override;
#endif
#ifdef USE_COVER
  void on_cover_update(cover::Cover *obj) override;
#endif
#ifdef USE_CLIMATE
  void on_climate_update(climate::Climate *obj) override;
#endif
#ifdef USE_LOCK
  void on_lock_update(lock::Lock *obj) override;
#endif
#ifdef USE_VALVE
  void on_valve_update(valve::Valve *obj) override;
#endif
#ifdef USE_TEXT
  void on_text_update(text::Text *obj, const std::string &state) override;
#endif
#ifdef USE_DATETIME_DATE
  void on_date_update(datetime::DateEntity *obj) override;
#endif
#ifdef USE_DATETIME_TIME
  void on_time_update(datetime::TimeEntity *obj) override;
#endif
#ifdef USE_DATETIME_DATETIME
  void on_datetime_update(datetime::DateTimeEntity *obj) override;
#endif
#ifdef USE_LOCK
  void on_lock_update(lock::Lock *obj) override;
#endif
#ifdef USE_VALVE
  void on_valve_update(valve::Valve *obj) override;
#endif
#ifdef USE_MEDIA_PLAYER
  void on_media_player_update(media_player::MediaPlayer *obj) override;
#endif
#ifdef USE_ALARM_CONTROL_PANEL
  void on_alarm_control_panel_update(
      alarm_control_panel::AlarmControlPanel *obj) override;
#endif
#ifdef USE_EVENT
  void on_event(event::Event *obj, const std::string &event_type) override;
#endif
#ifdef USE_UPDATE
  void on_update(update::UpdateEntity *obj) override;
#endif

protected:
  std::string server_url_;
  std::string device_name_;
  bool device_name_explicitly_set_{false};
  std::string provisioning_key_;
  std::string provisioning_secret_;
  // Empty means server-generated ACCESS_TOKEN.
  std::string provisioning_credentials_type_;
  std::string provisioning_credentials_token_;
  std::string provisioning_credentials_client_id_;
  std::string provisioning_credentials_username_;
  std::string provisioning_credentials_password_;
  std::string provisioning_credentials_cert_pem_;
  uint32_t timeout_{10000};

  std::string mqtt_broker_;
  uint16_t mqtt_port_{1883};
  std::string device_token_;

  // Optional auth modes wired by the thingsboard_mqtt sibling. ACCESS_TOKEN is
  // the default; setting any of these flips the transport to MQTT_BASIC or
  // X.509 (with TLS implied by either x509 or a server CA pin).
  bool mqtt_use_basic_{false};
  bool mqtt_use_x509_{false};
  std::string mqtt_basic_client_id_;
  std::string mqtt_basic_username_;
  std::string mqtt_basic_password_;
  std::string mqtt_client_cert_pem_;
  std::string mqtt_client_key_pem_;
  std::string mqtt_server_ca_pem_;

  std::string claim_secret_key_;
  uint32_t claim_duration_ms_{0};

  bool provisioned_{false};
  bool setup_called_{false};
  std::string access_token_;

  bool provisioning_in_progress_{false};
  bool provisioning_via_mqtt_{false};
  uint32_t provisioning_start_time_{0};
  std::shared_ptr<http_request::HttpContainer> provision_container_{nullptr};

  uint32_t last_connection_attempt_{0};
  uint32_t connection_retry_interval_{5000};

  // Populated from getSessionLimits RPC.
  struct RateLimits {
    uint32_t max_payload_size_{65536};
    uint32_t max_inflight_messages_{100};
    std::string messages_rate_limit_;     // raw string, e.g. "200:1,6000:60,14000:3600"
    std::string telemetry_messages_rate_limit_;
    std::string telemetry_data_points_rate_limit_;
    bool limits_received_{false};
  } rate_limits_;

  // Sliding-window counter: parsed (max, window_ms) tiers + per-tier (window_start, count).
  // We only need to know whether a given tier is at capacity right now. When all
  // tiers admit `n` more events, the publish proceeds; otherwise process_batch_
  // defers (the batch stays pending and will be retried next loop tick).
  struct RateLimitCounter {
    struct Tier {
      uint32_t max;
      uint32_t window_ms;
      uint32_t window_start;
      uint32_t count;
    };
    std::vector<Tier> tiers;
    void parse(const std::string &spec);
    // Reports admit-or-defer for `n` events occurring at `now`. Does not mutate.
    bool can_admit(uint32_t n, uint32_t now) const;
    // Records `n` events at `now`, rotating any windows that have expired.
    void record(uint32_t n, uint32_t now);
  };
  RateLimitCounter messages_counter_;
  RateLimitCounter telemetry_messages_counter_;
  RateLimitCounter telemetry_data_points_counter_;
  void rebuild_rate_limit_counters_();

  struct PendingMessage {
    std::string key;
    // value carries a JSON-serialised payload (number literal, `"escaped"`,
    // `true`/`false`, or any compound JSON). process_batch_ inserts it
    // verbatim into the outgoing object via ArduinoJson's serialized() wrapper.
    std::string value;
    uint32_t timestamp;
    bool is_attribute;
  };
  std::map<std::string, PendingMessage> pending_messages_;
  uint32_t last_batch_process_{0};
  // Internal floor on how often process_batch_ may emit. Not user-configurable;
  // YAML callers set telemetry_interval_ if they want a longer cadence.
  static constexpr uint32_t DEFAULT_BATCH_DELAY_MS = 100;

  uint32_t telemetry_interval_{0};  // 0 = use DEFAULT_BATCH_DELAY_MS

  uint32_t effective_batch_interval_() const {
    return this->telemetry_interval_ > 0 ? this->telemetry_interval_
                                         : DEFAULT_BATCH_DELAY_MS;
  }
  uint32_t telemetry_throttle_{0};  // 0 = disabled
  std::map<std::string, uint32_t> last_key_send_;
  // Bound on last_key_send_ to keep long-running devices from leaking heap as
  // unique throttled keys accumulate (T6).
  static constexpr size_t LAST_KEY_SEND_MAX = 256;

  // T5 / T7 knobs.
  uint32_t offline_queue_max_{200};
  bool use_client_timestamps_{false};

  uint32_t last_all_telemetry_{0};
  uint32_t all_telemetry_interval_{30000};

  ControlIterator control_iterator_{this};

  class InitialStateIterator;
  friend class InitialStateIterator;

  std::unique_ptr<InitialStateIterator> initial_state_iterator_;
  bool initial_states_sent_{false};

  // Conservative cap to avoid saturating an MQTT inflight window.
  static constexpr size_t MAX_INITIAL_PER_BATCH = 10;

#if defined(USE_ESP32) && defined(USE_THINGSBOARD_MQTT_TRANSPORT)
  std::unique_ptr<ThingsBoardMQTT> mqtt_client_;
#endif

  // Active transport (non-owning).
  TBTransport *transport_{nullptr};
  TBOTATransport *ota_transport_{nullptr};

  Trigger<> connect_trigger_;
  Trigger<> disconnect_trigger_;
  Trigger<std::string, std::string> rpc_trigger_;
  Trigger<std::map<std::string, std::string>> shared_attributes_trigger_;
  Trigger<std::string, std::string> rpc_response_trigger_;

public:
  bool has_access_token() const { return !access_token_.empty(); }
  std::string get_access_token() const { return access_token_; }

protected:
  void initialize_component_();
  bool load_device_token_();
  void save_device_token_(const std::string &token);
  void clear_device_token_();
  bool provision_device_();
  // https://thingsboard.io/docs/reference/mqtt-api/#device-provisioning
  bool provision_device_mqtt_();
  bool provision_device_http_();
  // HTTP-only — MQTT provisioning completes via callback.
  void check_provisioning_status_();
  void handle_provision_response_(const std::string &response);
  void establish_connection_();

  void send_single_telemetry_(const std::string &key, float value);
  void send_single_telemetry_(const std::string &key, const std::string &value);
  void send_single_client_attribute_(const std::string &key, bool value);
  void send_single_client_attribute_(const std::string &key, float value);
  void send_single_client_attribute_(const std::string &key,
                                     const std::string &value);

  void send_immediate_telemetry_(const std::string &key,
                                 const std::string &value);
  void send_immediate_rpc_response_(const std::string &request_id,
                                    const std::string &response);
  std::string get_device_mac_();
  void send_all_components_telemetry_();
  void send_device_metadata_();

  void request_session_limits_();
  void handle_session_limits_response_(const std::string &response);
  bool check_rate_limits_();

  void add_to_batch_(const std::string &key, const std::string &value,
                     bool is_attribute);
  void process_batch_();
  // Emits a single side of pending_messages_ (telemetry vs client-attribute)
  // in payload-size-bounded chunks, deferring whatever the rate-limit windows
  // can't admit right now.
  void process_partition_(bool is_attribute);
  void clear_batch_();

  void send_status_telemetry_(const std::string &status,
                              const std::string &error_code = "");
  void send_warning_status_(const std::string &message);
  void send_error_status_(const std::string &message,
                          const std::string &error_code = "");

  void claim_device_(const std::string &secret_key, uint32_t duration_ms);

  uint32_t rpc_request_counter_{0};
  uint32_t attribute_request_counter_{0};

  std::string get_domain_scoped_id_(const std::string &domain,
                                    const std::string &object_id);
  // EntityBase overload uses get_object_id_to(buf) so call sites don't trip the
  // get_object_id() deprecation removed in ESPHome 2026.7.0.
  std::string get_domain_scoped_id_(const std::string &domain,
                                    const EntityBase *obj);

  // Routes a rich-domain entity update through its DomainHandler so the full
  // per-entity state shape lands on TB's time-series, not just the one field
  // the legacy on_*_update used to send (T10).
  void emit_handler_telemetry_(const std::string &domain, EntityBase *obj);

  void setup_mqtt_();
  void connect_mqtt_();
  void on_mqtt_connect_();
  void on_mqtt_disconnect_();
  void on_mqtt_auth_failure_();

  void process_initial_state_batch_();

  class InitialStateIterator : public esphome::ComponentIterator {
  public:
    InitialStateIterator(ThingsBoardComponent *parent) : parent_(parent) {}

    bool on_begin() override {
      ESP_LOGD("thingsboard", "Initial state iterator starting");
      return true;
    }

    bool on_end() override {
      ESP_LOGD("thingsboard", "Initial state iterator completed");
      return true;
    }

#ifdef USE_SENSOR
    bool on_sensor(sensor::Sensor *obj) override {
      char id_buf[OBJECT_ID_MAX_LEN];
      auto id = obj->get_object_id_to(id_buf);
      ESP_LOGD("thingsboard",
               "InitialStateIterator: on_sensor called for %s (internal=%s)",
               id.c_str(), obj->is_internal() ? "true" : "false");
      if (obj->is_internal()) {
        ESP_LOGD("thingsboard",
                 "InitialStateIterator: sensor %s is internal, still sending "
                 "for completeness",
                 id.c_str());
      }
      ESP_LOGD("thingsboard", "Initial state: sensor %s = %.2f", id.c_str(),
               obj->state);
      if (!std::isnan(obj->state)) {
        parent_->send_single_telemetry_(
            parent_->get_domain_scoped_id_("sensor", obj),
            obj->state);
      }
      return true;
    }
#endif

#ifdef USE_BINARY_SENSOR
    bool on_binary_sensor(binary_sensor::BinarySensor *obj) override {
      if (obj->is_internal())
        return true;
      parent_->send_single_telemetry_(
          parent_->get_domain_scoped_id_("binary_sensor", obj),
          obj->state ? 1.0f : 0.0f);
      return true;
    }
#endif

#ifdef USE_SWITCH
    bool on_switch(switch_::Switch *obj) override {
      char id_buf[OBJECT_ID_MAX_LEN];
      auto id = obj->get_object_id_to(id_buf);
      ESP_LOGD("thingsboard",
               "InitialStateIterator: on_switch called for %s (internal=%s)",
               id.c_str(), obj->is_internal() ? "true" : "false");
      if (obj->is_internal()) {
        ESP_LOGD("thingsboard",
                 "InitialStateIterator: switch %s is internal, still sending "
                 "for completeness",
                 id.c_str());
      }
      ESP_LOGD("thingsboard", "Initial state: switch %s = %s", id.c_str(),
               obj->state ? "ON" : "OFF");
      std::string scoped_id =
          parent_->get_domain_scoped_id_("switch", obj);
      parent_->send_single_telemetry_(scoped_id, obj->state ? 1.0f : 0.0f);
      parent_->send_single_client_attribute_(scoped_id, obj->state);
      return true;
    }
#endif

#ifdef USE_NUMBER
    bool on_number(number::Number *obj) override {
      if (obj->is_internal())
        return true;
      if (!std::isnan(obj->state)) {
        std::string scoped_id =
            parent_->get_domain_scoped_id_("number", obj);
        parent_->send_single_telemetry_(scoped_id, obj->state);
        parent_->send_single_client_attribute_(scoped_id, obj->state);
      }
      return true;
    }
#endif

#ifdef USE_TEXT
    bool on_text(text::Text *obj) override {
      if (obj->is_internal())
        return true;
      std::string scoped_id =
          parent_->get_domain_scoped_id_("text", obj);
      parent_->send_single_telemetry_(scoped_id, obj->state);
      parent_->send_single_client_attribute_(scoped_id, obj->state);
      return true;
    }
#endif

#ifdef USE_SELECT
    bool on_select(select::Select *obj) override {
      if (obj->is_internal())
        return true;
      std::string scoped_id =
          parent_->get_domain_scoped_id_("select", obj);
      parent_->send_single_telemetry_(scoped_id, obj->state);
      parent_->send_single_client_attribute_(scoped_id, obj->state);
      return true;
    }
#endif

#ifdef USE_DATETIME_DATE
    bool on_date(datetime::DateEntity *obj) override {
      if (obj->is_internal())
        return true;
      std::string value =
          str_sprintf("%d-%02d-%02d", obj->year, obj->month, obj->day);
      std::string scoped_id =
          parent_->get_domain_scoped_id_("date", obj);
      parent_->send_single_telemetry_(scoped_id, value);
      parent_->send_single_client_attribute_(scoped_id, value);
      return true;
    }
#endif

#ifdef USE_DATETIME_TIME
    bool on_time(datetime::TimeEntity *obj) override {
      if (obj->is_internal())
        return true;
      std::string value =
          str_sprintf("%02d:%02d:%02d", obj->hour, obj->minute, obj->second);
      std::string scoped_id =
          parent_->get_domain_scoped_id_("time", obj);
      parent_->send_single_telemetry_(scoped_id, value);
      parent_->send_single_client_attribute_(scoped_id, value);
      return true;
    }
#endif

#ifdef USE_DATETIME_DATETIME
    bool on_datetime(datetime::DateTimeEntity *obj) override {
      if (obj->is_internal())
        return true;
      std::string value =
          str_sprintf("%d-%02d-%02d %02d:%02d:%02d", obj->year, obj->month,
                      obj->day, obj->hour, obj->minute, obj->second);
      std::string scoped_id =
          parent_->get_domain_scoped_id_("datetime", obj);
      parent_->send_single_telemetry_(scoped_id, value);
      parent_->send_single_client_attribute_(scoped_id, value);
      return true;
    }
#endif

#ifdef USE_LOCK
    bool on_lock(lock::Lock *obj) override {
      if (obj->is_internal())
        return true;
      parent_->emit_handler_telemetry_("lock", obj);
      return true;
    }
#endif

#ifdef USE_VALVE
    bool on_valve(valve::Valve *obj) override {
      if (obj->is_internal())
        return true;
      parent_->emit_handler_telemetry_("valve", obj);
      return true;
    }
#endif

#ifdef USE_MEDIA_PLAYER
    bool on_media_player(media_player::MediaPlayer *obj) override {
      if (obj->is_internal())
        return true;
      parent_->emit_handler_telemetry_("media_player", obj);
      return true;
    }
#endif

#ifdef USE_ALARM_CONTROL_PANEL
    bool on_alarm_control_panel(
        alarm_control_panel::AlarmControlPanel *obj) override {
      if (obj->is_internal())
        return true;
      parent_->emit_handler_telemetry_("alarm_control_panel", obj);
      return true;
    }
#endif

#ifdef USE_EVENT
    bool on_event(event::Event *obj) override {
      if (obj->is_internal())
        return true;
      // Events don't have persistent state, skip for initial sync
      return true;
    }
#endif

#ifdef USE_UPDATE
    bool on_update(update::UpdateEntity *obj) override {
      if (obj->is_internal())
        return true;
      bool update_available = obj->state == update::UPDATE_STATE_AVAILABLE;
      parent_->send_single_telemetry_(
          parent_->get_domain_scoped_id_("update", obj),
          update_available ? 1.0f : 0.0f);
      return true;
    }
#endif

#ifdef USE_TEXT_SENSOR
    bool on_text_sensor(text_sensor::TextSensor *obj) override {
      if (obj->is_internal())
        return true;
      char buf[OBJECT_ID_MAX_LEN];
      ESP_LOGV("thingsboard", "Initial state: text_sensor %s = %s",
               obj->get_object_id_to(buf).c_str(), obj->state.c_str());
      parent_->send_single_telemetry_(
          parent_->get_domain_scoped_id_("text_sensor", obj),
          obj->state);
      return true;
    }
#endif

#ifdef USE_FAN
    bool on_fan(fan::Fan *obj) override {
      if (obj->is_internal())
        return true;
      parent_->emit_handler_telemetry_("fan", obj);
      return true;
    }
#endif

#ifdef USE_LIGHT
    bool on_light(light::LightState *obj) override {
      if (obj->is_internal())
        return true;
      parent_->emit_handler_telemetry_("light", obj);
      return true;
    }
#endif

#ifdef USE_COVER
    bool on_cover(cover::Cover *obj) override {
      if (obj->is_internal())
        return true;
      parent_->emit_handler_telemetry_("cover", obj);
      return true;
    }
#endif

#ifdef USE_CLIMATE
    bool on_climate(climate::Climate *obj) override {
      if (obj->is_internal())
        return true;
      parent_->emit_handler_telemetry_("climate", obj);
      return true;
    }
#endif

    // Skip button callbacks as they don't have state
#ifdef USE_BUTTON
    bool on_button(button::Button *button) override { return true; }
#endif

    bool completed() {
      return this->state_ == esphome::ComponentIterator::IteratorState::NONE;
    }

  private:
    ThingsBoardComponent *parent_;
  };
};

template <typename... Ts>
class ThingsBoardSendTelemetryAction : public Action<Ts...>,
                                       public Parented<ThingsBoardComponent> {
public:
  void set_data(const std::string &data) { data_ = data; }
  void set_data(std::function<std::string(Ts...)> func) { data_func_ = func; }

  void play(Ts... x) override {
    std::string data;
    if (this->data_func_.has_value()) {
      data = this->data_func_.value()(x...);
    } else {
      data = this->data_;
    }

    this->parent_->send_telemetry(data);
  }

protected:
  std::string data_;
  optional<std::function<std::string(Ts...)>> data_func_;
};

template <typename... Ts>
class ThingsBoardSendAttributesAction : public Action<Ts...>,
                                        public Parented<ThingsBoardComponent> {
public:
  void set_data(const std::string &data) { data_ = data; }
  void set_data(std::function<std::string(Ts...)> func) { data_func_ = func; }

  void play(Ts... x) override {
    std::string data;
    if (this->data_func_.has_value()) {
      data = this->data_func_.value()(x...);
    } else {
      data = this->data_;
    }

    this->parent_->send_attributes(data);
  }

protected:
  std::string data_;
  optional<std::function<std::string(Ts...)>> data_func_;
};

template <typename... Ts>
class ThingsBoardClearTokenAction : public Action<Ts...>,
                                    public Parented<ThingsBoardComponent> {
public:
  void play(Ts... x) override {
    ESP_LOGI("thingsboard",
             "Clearing stored device token to force re-provisioning");
    this->parent_->clear_device_token();
  }
};

template <typename... Ts>
class ThingsBoardSetTokenAction : public Action<Ts...>,
                                  public Parented<ThingsBoardComponent> {
public:
  void set_token(const std::string &token) { token_ = token; }
  void set_token(std::function<std::string(Ts...)> func) { token_func_ = func; }

  void play(const Ts &...x) override {
    std::string token;
    if (this->token_func_.has_value()) {
      token = this->token_func_.value()(x...);
    } else {
      token = this->token_;
    }
    ESP_LOGI("thingsboard", "Setting device token via action (%zu chars)",
             token.length());
    this->parent_->set_device_token_persistent(token);
  }

protected:
  std::string token_;
  optional<std::function<std::string(Ts...)>> token_func_;
};

template <typename... Ts>
class ThingsBoardClaimDeviceAction : public Action<Ts...>,
                                     public Parented<ThingsBoardComponent> {
public:
  void set_secret_key(const std::string &secret_key) {
    secret_key_ = secret_key;
  }
  void set_secret_key(std::function<std::string(Ts...)> func) {
    secret_key_func_ = func;
  }
  void set_duration_ms(uint32_t duration_ms) { duration_ms_ = duration_ms; }
  void set_duration_ms(std::function<uint32_t(Ts...)> func) {
    duration_ms_func_ = func;
  }

  void play(Ts... x) override {
    std::string secret_key;
    if (this->secret_key_func_.has_value()) {
      secret_key = this->secret_key_func_.value()(x...);
    } else {
      secret_key = this->secret_key_;
    }

    uint32_t duration_ms = 0;
    if (this->duration_ms_func_.has_value()) {
      duration_ms = this->duration_ms_func_.value()(x...);
    } else {
      duration_ms = this->duration_ms_;
    }

    ESP_LOGI("thingsboard", "Claiming device via action");
    this->parent_->claim_device(secret_key, duration_ms);
  }

protected:
  std::string secret_key_;
  optional<std::function<std::string(Ts...)>> secret_key_func_;
  uint32_t duration_ms_{0};
  optional<std::function<uint32_t(Ts...)>> duration_ms_func_;
};

template <typename... Ts>
class ThingsBoardSendRpcRequestAction : public Action<Ts...>,
                                        public Parented<ThingsBoardComponent> {
public:
  void set_method(const std::string &method) { method_ = method; }
  void set_method(std::function<std::string(Ts...)> func) {
    method_func_ = func;
  }
  void set_params(const std::string &params) { params_ = params; }
  void set_params(std::function<std::string(Ts...)> func) {
    params_func_ = func;
  }

  void play(Ts... x) override {
    std::string method;
    if (this->method_func_.has_value()) {
      method = this->method_func_.value()(x...);
    } else {
      method = this->method_;
    }

    std::string params;
    if (this->params_func_.has_value()) {
      params = this->params_func_.value()(x...);
    } else {
      params = this->params_;
    }

    this->parent_->send_rpc_request(method, params);
  }

protected:
  std::string method_;
  optional<std::function<std::string(Ts...)>> method_func_;
  std::string params_;
  optional<std::function<std::string(Ts...)>> params_func_;
};

template <typename... Ts>
class ThingsBoardRequestAttributesAction
    : public Action<Ts...>,
      public Parented<ThingsBoardComponent> {
public:
  void set_keys(const std::string &keys) { keys_ = keys; }
  void set_keys(std::function<std::string(Ts...)> func) { keys_func_ = func; }

  void play(Ts... x) override {
    std::string keys;
    if (this->keys_func_.has_value()) {
      keys = this->keys_func_.value()(x...);
    } else {
      keys = this->keys_;
    }

    this->parent_->request_attributes(keys);
  }

protected:
  std::string keys_;
  optional<std::function<std::string(Ts...)>> keys_func_;
};

} // namespace thingsboard
} // namespace esphome
