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

  // Inbound command-attribute prefix (default `"set."`). Shared attributes
  // matching `<prefix><domain>.<object_id>` are dispatched into the matching
  // domain handler -- so writing SHARED_SCOPE `set.switch.relay_1 = ON`
  // turns relay_1 on, while telemetry/state stays on `switch.relay_1`.
  void set_command_prefix(const std::string &p) { command_prefix_ = p; }
  const std::string &command_prefix() const { return command_prefix_; }

  // Outbound client-attribute (state echo) prefix. Default `""` keeps the
  // historical behaviour where state echoes share the scoped id with
  // telemetry (`switch.relay_1`). Set to e.g. `"state."` to namespace state
  // echoes as `state.switch.relay_1` so they never collide with telemetry on
  // TB's scope-agnostic read paths -- mirrors the wire-level gw_pending_
  // dedup separation in commit 592a565 at the TB key level.
  void set_state_prefix(const std::string &p) { state_prefix_ = p; }
  const std::string &state_prefix() const { return state_prefix_; }

  void set_mqtt_broker(const std::string &broker) { mqtt_broker_ = broker; }
  void set_mqtt_port(uint16_t port) { mqtt_port_ = port; }
  // Default QoS + retain applied to outbound TB publishes. QoS 1 / retain false
  // is the TB-recommended default; 0/2 and retain=true are exposed for
  // deployments with bespoke broker conventions.
  void set_mqtt_publish_qos(uint8_t qos) { mqtt_publish_qos_ = qos; }
  void set_mqtt_publish_retain(bool retain) { mqtt_publish_retain_ = retain; }
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

  // Set only when a thingsboard_gateway component is present. The pointer
  // stays null on plain device builds; every core path null-checks it.
  void register_gateway_transport(TBGatewayTransport *t) {
    this->gateway_transport_ = t;
  }
  TBGatewayTransport *get_gateway_transport() const {
    return this->gateway_transport_;
  }

#ifdef USE_THINGSBOARD_GATEWAY
  // Gateway children share the connection but carry their own server-side
  // budget (`gatewayRateLimits` from getSessionLimits). The gateway component
  // gates each v1/gateway/* publish through these: check_* is admit-or-defer
  // (does not mutate), record_* commits a publish. Both no-op gracefully until
  // limits arrive or when TB returns empty gateway specs.
  bool check_gateway_rate_limits(uint32_t n_msgs, uint32_t n_points);
  void record_gateway_publish(uint32_t n_msgs, uint32_t n_points);

  // B-layer: the gateway component forwards a `domain.method` RPC for an
  // ESPHome sub-device here. Resolution is scoped to `device_id` so only that
  // sub-device's entities match. `response` is filled with the ESPHome
  // REST-style JSON result for the gateway to publish on `v1/gateway/rpc`.
  esp_err_t handle_child_rpc(const std::string &method,
                             const std::string &params, uint32_t device_id,
                             std::string &response) {
    return this->control_iterator_.handle_rpc_with_response(method, params,
                                                            response,
                                                            device_id);
  }
  // B-layer: the gateway component forwards a `v1/gateway/attributes` push
  // for an ESPHome sub-device here. Routes through the existing per-domain
  // register_shared_attributes callbacks; those are keyed by bare object_id
  // today, so this works cleanly when sub-device entity ids are unique across
  // the firmware (e.g. relay_1/2/3 on sd_relay_1/2/3). Device-id-scoped
  // routing (so two sub-devices can share an object_id) is the larger ADJ-2
  // SA-M1 reshape -- when that lands, this delegates to a scoped overload.
  void handle_child_shared_attributes(
      const std::map<std::string, std::string> &attributes,
      uint32_t device_id) {
    (void) device_id;  // see comment above; not yet used.
    this->control_iterator_.handle_shared_attributes(attributes);
  }
#endif

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
  void on_text_update(text::Text *obj) override;
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
#ifdef USE_MEDIA_PLAYER
  void on_media_player_update(media_player::MediaPlayer *obj) override;
#endif
#ifdef USE_ALARM_CONTROL_PANEL
  void on_alarm_control_panel_update(
      alarm_control_panel::AlarmControlPanel *obj) override;
#endif
#ifdef USE_EVENT
  void on_event(event::Event *obj) override;
#endif
#ifdef USE_UPDATE
  void on_update(update::UpdateEntity *obj) override;
#endif
#ifdef USE_WATER_HEATER
  void on_water_heater_update(water_heater::WaterHeater *obj) override;
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
  uint8_t mqtt_publish_qos_{1};
  bool mqtt_publish_retain_{false};
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
  // Reconnect backoff. A publish burst that trips ThingsBoard's per-device rate
  // limit gets the session dropped; on a fixed reconnect timer the firmware
  // re-floods before the sliding window drains, so the loop never breaks.
  // connection_retry_interval_ doubles on a short-lived connection and resets
  // once a connection proves stable.
  static constexpr uint32_t MIN_RETRY_INTERVAL_MS = 5000;
  static constexpr uint32_t MAX_RETRY_INTERVAL_MS = 60000;
  // A connection alive at least this long is considered stable: backoff resets.
  static constexpr uint32_t STABLE_CONNECTION_MS = 30000;
  // How long the first telemetry batch is held while the getSessionLimits
  // round-trip completes, so the rate-limit counters carry TB's real tiers
  // before anything is published. If TB never answers (older versions don't
  // implement the RPC), publishing proceeds ungated once this elapses.
  static constexpr uint32_t LIMITS_GRACE_MS = 3000;
  uint32_t connection_retry_interval_{MIN_RETRY_INTERVAL_MS};
  // Start of the current connection; only meaningful while connection_active_
  // is true. A separate bool, not a 0 sentinel: millis() is legitimately 0 for
  // the first millisecond after boot and wraps back to 0 every ~49.7 days.
  uint32_t connected_at_{0};
  bool connection_active_{false};

  // Bootstrap phase machine. dispatch_connected() used to fire a synchronous
  // burst of publishes (metadata + getSessionLimits RPC + OTA shared-attr
  // request + initial-state telemetry + gw_connect-per-child replay), all from
  // the same loop tick. That burst routinely exceeded ThingsBoard's per-device
  // sliding-window rate limit (default `20:1`) inside ~1s, and TB FIN'd the
  // socket -- the firmware then reconnect-looped because every retry replayed
  // the same burst before the bucket drained.
  //
  // process_bootstrap_() drives the chain from loop() instead, advancing one
  // phase per tick and gating each publishing phase through messages_counter_
  // exactly like the batch flusher does. Each phase records its publish on the
  // counter so subsequent ticks see the bucket honestly. dispatch_connected()
  // is now state-reset-only.
  enum BootstrapPhase : uint8_t {
    BOOT_NONE = 0,        // not connected, or bootstrap already drained
    BOOT_METADATA,        // publish device metadata as client attributes
    BOOT_REQUEST_LIMITS,  // RPC getSessionLimits (skipped if already received)
    BOOT_WAIT_LIMITS,     // pause until limits_received_ or LIMITS_GRACE_MS
    BOOT_OTA_ATTRS,       // request fw_*/sw_* shared-attribute snapshot
    BOOT_INITIAL_STATES,  // kick off InitialStateIterator (it self-paces)
    BOOT_GW_REPLAY,       // poke gateway component to replay child registry
    BOOT_DONE,
  };
  BootstrapPhase bootstrap_phase_{BOOT_NONE};
  // Phase-local clock: set when entering a phase that needs to wait (only
  // BOOT_WAIT_LIMITS today). millis()-based, wrap-safe via signed subtraction.
  uint32_t bootstrap_phase_started_{0};
  // Loop-driven advance. Returns quickly when bootstrap_phase_ == BOOT_NONE
  // or BOOT_DONE; defers when the counter denies or the wait hasn't elapsed.
  void process_bootstrap_();

  // Populated from the getSessionLimits RPC. Empty strings mean "no limit": a
  // RateLimitCounter with no tiers admits everything. The first telemetry batch
  // is held (check_rate_limits_) until limits_received_ flips, so a publish is
  // never gated against empty counters except as a last-resort fallback.
  struct RateLimits {
    uint32_t max_payload_size_{65536};
    uint32_t max_inflight_messages_{100};
    std::string messages_rate_limit_;  // raw, e.g. "200:1,6000:60,14000:3600"
    std::string telemetry_messages_rate_limit_;
    std::string telemetry_data_points_rate_limit_;
#ifdef USE_THINGSBOARD_GATEWAY
    // `gatewayRateLimits` from getSessionLimits — applied per child device.
    std::string gateway_messages_rate_limit_;
    std::string gateway_telemetry_messages_rate_limit_;
    std::string gateway_telemetry_data_points_rate_limit_;
#endif
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
#ifdef USE_THINGSBOARD_GATEWAY
  RateLimitCounter gateway_messages_counter_;
  RateLimitCounter gateway_telemetry_messages_counter_;
  RateLimitCounter gateway_telemetry_data_points_counter_;
#endif
  void rebuild_rate_limit_counters_();

  struct PendingMessage {
    std::string key;
    // value carries a JSON-serialised payload (number literal, `"escaped"`,
    // `true`/`false`, or any compound JSON). process_batch_ inserts it
    // verbatim into the outgoing object via ArduinoJson's serialized() wrapper.
    std::string value;
    uint32_t timestamp;
    bool is_attribute;
    // Throttled telemetry is held until this time instead of being dropped.
    bool deferred{false};
    uint32_t not_before{0};
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
  std::string command_prefix_{"set."};
  std::string state_prefix_{""};

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
  // Non-owning; set by a thingsboard_gateway component, null otherwise.
  TBGatewayTransport *gateway_transport_{nullptr};

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
  // HTTP-only; MQTT provisioning completes via callback.
  void check_provisioning_status_();
  void handle_provision_response_(const std::string &response);
  void establish_connection_();

  // Shared tail of the send_single_* overloads: routes an already-encoded
  // datapoint to the device-API batch, or to a gateway child when device_id is
  // a sub-device and a gateway component is registered.
  void route_single_datapoint_(const std::string &key,
                               const std::string &json_value,
                               bool is_attribute, uint32_t device_id);

  // The trailing `device_id` routes a datapoint to a gateway child instead of
  // the device-API batch: 0 (the default) means the main device; non-zero is
  // an ESPHome sub-device id, forwarded to gateway_transport_ when one is
  // registered. Callers pass entity_device_id_(obj).
  void send_single_telemetry_(const std::string &key, float value,
                              uint32_t device_id = 0);
  void send_single_telemetry_(const std::string &key, const std::string &value,
                              uint32_t device_id = 0);
  void send_single_client_attribute_(const std::string &key, bool value,
                                     uint32_t device_id = 0);
  void send_single_client_attribute_(const std::string &key, float value,
                                     uint32_t device_id = 0);
  void send_single_client_attribute_(const std::string &key,
                                     const std::string &value,
                                     uint32_t device_id = 0);

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

  bool claim_device_(const std::string &secret_key, uint32_t duration_ms);

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

  // Resolves the ESPHome sub-device an entity belongs to: 0 for the main
  // device, non-zero for a sub-device. Always 0 unless USE_DEVICES is set
  // (the YAML declares `esphome: devices:`). M7 uses this to route a
  // sub-device's telemetry/RPC through the gateway instead of v1/devices/me/*.
  uint32_t entity_device_id_(const EntityBase *obj) const;

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
      if (obj->is_internal())
        return true;
      ESP_LOGD("thingsboard", "Initial state: sensor %s = %.2f", id.c_str(),
               obj->state);
      if (!std::isnan(obj->state)) {
        parent_->send_single_telemetry_(
            parent_->get_domain_scoped_id_("sensor", obj),
            obj->state, parent_->entity_device_id_(obj));
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
          obj->state ? 1.0f : 0.0f, parent_->entity_device_id_(obj));
      return true;
    }
#endif

#ifdef USE_SWITCH
    bool on_switch(switch_::Switch *obj) override {
      char id_buf[OBJECT_ID_MAX_LEN];
      auto id = obj->get_object_id_to(id_buf);
      if (obj->is_internal())
        return true;
      ESP_LOGD("thingsboard", "Initial state: switch %s = %s", id.c_str(),
               obj->state ? "ON" : "OFF");
      std::string scoped_id =
          parent_->get_domain_scoped_id_("switch", obj);
      uint32_t device_id = parent_->entity_device_id_(obj);
      parent_->send_single_telemetry_(scoped_id, obj->state ? 1.0f : 0.0f,
                                      device_id);
      parent_->send_single_client_attribute_(scoped_id, obj->state, device_id);
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
        uint32_t device_id = parent_->entity_device_id_(obj);
        parent_->send_single_telemetry_(scoped_id, obj->state, device_id);
        parent_->send_single_client_attribute_(scoped_id, obj->state,
                                               device_id);
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
      uint32_t device_id = parent_->entity_device_id_(obj);
      parent_->send_single_telemetry_(scoped_id, obj->state, device_id);
      parent_->send_single_client_attribute_(scoped_id, obj->state, device_id);
      return true;
    }
#endif

#ifdef USE_SELECT
    bool on_select(select::Select *obj) override {
      if (obj->is_internal())
        return true;
      std::string scoped_id =
          parent_->get_domain_scoped_id_("select", obj);
      uint32_t device_id = parent_->entity_device_id_(obj);
      std::string state = obj->current_option().str();
      parent_->send_single_telemetry_(
          scoped_id, static_cast<float>(obj->active_index().value_or(0)),
          device_id);
      parent_->send_single_client_attribute_(scoped_id, state, device_id);
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
      uint32_t device_id = parent_->entity_device_id_(obj);
      parent_->send_single_telemetry_(scoped_id, value, device_id);
      parent_->send_single_client_attribute_(scoped_id, value, device_id);
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
      uint32_t device_id = parent_->entity_device_id_(obj);
      parent_->send_single_telemetry_(scoped_id, value, device_id);
      parent_->send_single_client_attribute_(scoped_id, value, device_id);
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
      uint32_t device_id = parent_->entity_device_id_(obj);
      parent_->send_single_telemetry_(scoped_id, value, device_id);
      parent_->send_single_client_attribute_(scoped_id, value, device_id);
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
          update_available ? 1.0f : 0.0f, parent_->entity_device_id_(obj));
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
          obj->state, parent_->entity_device_id_(obj));
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
#ifdef USE_WATER_HEATER
    // Water heaters are not published yet.
    bool on_water_heater(water_heater::WaterHeater *obj) override {
      return true;
    }
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
