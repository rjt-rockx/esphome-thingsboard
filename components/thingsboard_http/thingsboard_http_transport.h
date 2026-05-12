#pragma once

#include "esphome/core/defines.h"

#include <functional>
#include <map>
#include <queue>
#include <string>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/thingsboard/transport.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace esphome {
namespace thingsboard {
class ThingsBoardComponent;
}
namespace thingsboard_http {

// ThingsBoardHttpTransport implements the ThingsBoard HTTP device API
// (<https://thingsboard.io/docs/reference/http-api/>) as a TBTransport.
//
// All synchronous esp_http_client work runs on a dedicated FreeRTOS worker
// task so loopTask never blocks on the network. The main task enqueues
// outbound work, drains inbound events, and fires the TBTransport callbacks;
// the worker drains outbound, performs HTTP requests, long-polls /rpc and
// /attributes/updates, and enqueues inbound events.
class ThingsBoardHttpTransport : public Component,
                                 public thingsboard::TBTransport {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::WIFI - 1.5f;  // After WIFI, before core's WIFI-1.0
  }

  void set_parent_thingsboard(thingsboard::ThingsBoardComponent *p) {
    parent_ = p;
  }
  void set_http(http_request::HttpRequestComponent *h) { http_ = h; }
  void set_server_url(const std::string &u) { server_url_ = u; }
  void set_device_token(const std::string &t);
  void set_poll_interval(uint32_t ms) { poll_interval_ms_ = ms; }
  void set_poll_timeout_ms(uint32_t ms) { poll_timeout_ms_ = ms; }

  // HTTP is stateless, so "connected" means "have a usable device token".
  // Provisioning happens before this returns true.
  bool is_connected() const override { return !device_token_.empty(); }

  bool publish_telemetry(const std::string &payload) override;
  bool publish_attributes(const std::string &payload) override;
  bool publish_client_attributes(const std::string &payload) override;

  bool publish_rpc_response(const std::string &request_id,
                            const std::string &payload) override;

  // TB returns the client-side RPC response synchronously in the POST body;
  // surfaced through on_rpc_response_callback so callers see the same shape
  // as MQTT.
  bool publish_rpc_request(const std::string &request_id,
                           const std::string &method,
                           const std::string &params) override;

  // HTTP returns the attribute result inline; re-dispatched through core's
  // attribute-response path.
  bool publish_attribute_request(const std::string &request_id,
                                 const std::string &keys) override;

  bool publish_provision_request(const std::string &payload) override;

  bool publish_claim(const std::string &payload) override;

 protected:
  // ---- Worker task ----
  struct OutMsg {
    enum Type {
      TELEMETRY,
      ATTRIBUTES,
      CLIENT_ATTRIBUTES,
      RPC_RESPONSE,
      RPC_REQUEST,
      ATTRIBUTE_REQUEST,
      PROVISION_REQUEST,
      CLAIM,
    };
    Type type;
    std::string body;
    std::string request_id;  // RPC_RESPONSE, RPC_REQUEST, ATTRIBUTE_REQUEST
    std::string method;      // RPC_REQUEST
    std::string keys;        // ATTRIBUTE_REQUEST
  };

  struct InMsg {
    enum Type {
      CONNECTED,
      DISCONNECTED,
      RPC_REQUEST,
      RPC_RESPONSE,
      SHARED_ATTRIBUTES,
      ATTRIBUTE_RESPONSE,
      PROVISION_RESPONSE,
    };
    Type type;
    std::string request_id;
    std::string body;
    std::string method;
    std::map<std::string, std::string> attrs;
  };

  static void worker_task_entry_(void *arg);
  void worker_loop_();
  void worker_handle_out_(const OutMsg &msg);
  void worker_poll_rpc_();
  void worker_poll_shared_attributes_();

  void enqueue_out_(OutMsg msg);
  bool dequeue_out_(OutMsg &out);
  void enqueue_in_(InMsg msg);
  void drain_in_();

  // Synthesise on_connected_ / on_disconnected_ edges from request outcomes
  // since HTTP itself has no connection lifecycle. Called on the worker
  // task; the CONNECTED / DISCONNECTED InMsgs are dispatched on the main.
  void note_request_result_(bool ok);

  std::string build_token_url_(const char *suffix);
  bool post_(const std::string &url, const std::string &body,
             std::string *resp_out = nullptr);
  bool get_(const std::string &url, std::string *resp_out);

  thingsboard::ThingsBoardComponent *parent_{nullptr};
  http_request::HttpRequestComponent *http_{nullptr};
  std::string server_url_;
  std::string device_token_;
  uint32_t poll_interval_ms_{5000};
  uint32_t poll_timeout_ms_{0};

#ifdef USE_ESP32
  TaskHandle_t worker_task_handle_{nullptr};
  SemaphoreHandle_t out_mutex_{nullptr};
  SemaphoreHandle_t in_mutex_{nullptr};
  SemaphoreHandle_t worker_wake_{nullptr};
  SemaphoreHandle_t token_mutex_{nullptr};
#endif

  std::queue<OutMsg> out_queue_;
  std::queue<InMsg> in_queue_;

  // Worker-task-local state.
  uint32_t last_rpc_poll_{0};
  uint32_t last_attr_poll_{0};
  bool reported_connected_{false};
  uint8_t consecutive_failures_{0};
};

}  // namespace thingsboard_http
}  // namespace esphome
