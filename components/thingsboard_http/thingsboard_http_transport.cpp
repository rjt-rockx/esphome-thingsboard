#include "thingsboard_http_transport.h"

#include <vector>

#include "esphome/components/json/json_util.h"
#include "esphome/components/thingsboard/thingsboard_client.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace thingsboard_http {

static const char *const TAG = "thingsboard.http";

namespace {

// Each synchronous HTTPS request peaks around 4 KB (mbedTLS handshake +
// esp_http_client buffers); 8 KB leaves headroom for one in-flight request
// plus poll bookkeeping.
constexpr uint32_t WORKER_STACK_WORDS = 8192;
// Just above IDLE so loopTask (priority 1) is never starved by HTTP work.
constexpr UBaseType_t WORKER_PRIORITY = tskIDLE_PRIORITY + 1;

}  // namespace

void ThingsBoardHttpTransport::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ThingsBoard HTTP transport");
  if (this->parent_ != nullptr) {
    if (this->server_url_.empty()) {
      this->server_url_ = this->parent_->get_server_url();
    }
    this->parent_->register_transport(this);
  }

#ifdef USE_ESP32
  this->out_mutex_ = xSemaphoreCreateMutex();
  this->in_mutex_ = xSemaphoreCreateMutex();
  this->token_mutex_ = xSemaphoreCreateMutex();
  // Binary wake signal — worker takes, main gives on every enqueue. Used
  // with a timeout so the worker also wakes periodically for polling.
  this->worker_wake_ = xSemaphoreCreateBinary();

  // Stagger the two long-polls by half a cycle so RPC latency isn't doubled
  // by running them back-to-back on the same worker iteration.
  const uint32_t now = millis();
  this->last_rpc_poll_ = now;
  this->last_attr_poll_ = now - this->poll_interval_ms_ / 2;

  // Pin to core 0 alongside the WiFi/IDF networking stack to avoid
  // cross-core wake-ups on every TLS round-trip.
  xTaskCreatePinnedToCore(&ThingsBoardHttpTransport::worker_task_entry_,
                          "tb_http_worker", WORKER_STACK_WORDS, this,
                          WORKER_PRIORITY, &this->worker_task_handle_, 0);
#endif
}

void ThingsBoardHttpTransport::loop() {
  // Main only drains inbound and fires TBTransport callbacks; HTTP work
  // happens on the worker (see worker_loop_).
  this->drain_in_();
}

void ThingsBoardHttpTransport::dump_config() {
  ESP_LOGCONFIG(TAG, "ThingsBoard HTTP transport:");
  ESP_LOGCONFIG(TAG, "  Server URL: %s", this->server_url_.c_str());
  ESP_LOGCONFIG(TAG, "  Token: %s",
                this->device_token_.empty() ? "<unset>" : "<set>");
  ESP_LOGCONFIG(TAG, "  Poll interval: %ums", this->poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Poll timeout: %ums (0 = short-poll)",
                this->poll_timeout_ms_);
}

void ThingsBoardHttpTransport::set_device_token(const std::string &t) {
#ifdef USE_ESP32
  if (this->token_mutex_ != nullptr) {
    xSemaphoreTake(this->token_mutex_, portMAX_DELAY);
    this->device_token_ = t;
    xSemaphoreGive(this->token_mutex_);
  } else {
    this->device_token_ = t;
  }
#else
  this->device_token_ = t;
#endif
}

std::string ThingsBoardHttpTransport::build_token_url_(const char *suffix) {
  // `/api/v1/<TOKEN>/<suffix>`. Token read under the mutex so worker and
  // main can race on set_device_token (e.g. mid-provisioning).
  std::string token;
#ifdef USE_ESP32
  if (this->token_mutex_ != nullptr) {
    xSemaphoreTake(this->token_mutex_, portMAX_DELAY);
    token = this->device_token_;
    xSemaphoreGive(this->token_mutex_);
  } else {
    token = this->device_token_;
  }
#else
  token = this->device_token_;
#endif
  return this->server_url_ + "/api/v1/" + token + suffix;
}

bool ThingsBoardHttpTransport::post_(const std::string &url,
                                     const std::string &body,
                                     std::string *resp_out) {
  if (this->http_ == nullptr) {
    ESP_LOGW(TAG, "HTTP parent unset, cannot POST %s", url.c_str());
    return false;
  }
  std::vector<http_request::Header> headers = {
      {"Content-Type", "application/json"},
      {"Connection", "keep-alive"},
  };
  auto container = this->http_->post(url, body, headers);
  if (container == nullptr) {
    ESP_LOGW(TAG, "POST failed (null container): %s", url.c_str());
    this->note_request_result_(false);
    return false;
  }
  bool ok = container->status_code >= 200 && container->status_code < 300;
  if (resp_out != nullptr && ok) {
    char buffer[512];
    int n;
    while ((n = container->read((uint8_t *) buffer, sizeof(buffer) - 1)) > 0) {
      buffer[n] = '\0';
      *resp_out += buffer;
    }
  }
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "POST %s -> HTTP %d", url.c_str(), container->status_code);
  }
  this->note_request_result_(ok);
  return ok;
}

bool ThingsBoardHttpTransport::get_(const std::string &url,
                                    std::string *resp_out) {
  if (this->http_ == nullptr) {
    ESP_LOGW(TAG, "HTTP parent unset, cannot GET %s", url.c_str());
    return false;
  }
  std::vector<http_request::Header> headers = {
      {"Connection", "keep-alive"},
  };
  auto container = this->http_->get(url, headers);
  if (container == nullptr) {
    this->note_request_result_(false);
    return false;
  }
  bool ok = container->status_code >= 200 && container->status_code < 300;
  // 408 is "no events in long-poll window" — not an error
  if (container->status_code == 408)
    ok = true;
  if (resp_out != nullptr && ok && container->status_code == 200) {
    char buffer[512];
    int n;
    while ((n = container->read((uint8_t *) buffer, sizeof(buffer) - 1)) > 0) {
      buffer[n] = '\0';
      *resp_out += buffer;
    }
  }
  container->end();
  this->note_request_result_(ok);
  return ok;
}

void ThingsBoardHttpTransport::note_request_result_(bool ok) {
  // HTTP is stateless on the wire but core's lifecycle wants discrete edges:
  // first OK emits on_connected_, 3 consecutive failures emit
  // on_disconnected_. The threshold suppresses transient single-request
  // blips. Called on the worker; InMsg dispatched on the main task.
  if (ok) {
    this->consecutive_failures_ = 0;
    if (!this->reported_connected_) {
      this->reported_connected_ = true;
      InMsg ev;
      ev.type = InMsg::CONNECTED;
      this->enqueue_in_(std::move(ev));
    }
  } else {
    this->consecutive_failures_++;
    if (this->reported_connected_ && this->consecutive_failures_ >= 3) {
      this->reported_connected_ = false;
      InMsg ev;
      ev.type = InMsg::DISCONNECTED;
      this->enqueue_in_(std::move(ev));
    }
  }
}

// publish_* runs on the main task: enqueue and return immediately. Return
// value means "enqueued"; async outcome reaches the caller via on_*_callback
// dispatch on the next loop().

bool ThingsBoardHttpTransport::publish_telemetry(const std::string &payload) {
  OutMsg m;
  m.type = OutMsg::TELEMETRY;
  m.body = payload;
  this->enqueue_out_(std::move(m));
  return true;
}

bool ThingsBoardHttpTransport::publish_attributes(const std::string &payload) {
  OutMsg m;
  m.type = OutMsg::ATTRIBUTES;
  m.body = payload;
  this->enqueue_out_(std::move(m));
  return true;
}

bool ThingsBoardHttpTransport::publish_client_attributes(
    const std::string &payload) {
  OutMsg m;
  m.type = OutMsg::CLIENT_ATTRIBUTES;
  m.body = payload;
  this->enqueue_out_(std::move(m));
  return true;
}

bool ThingsBoardHttpTransport::publish_rpc_response(
    const std::string &request_id, const std::string &payload) {
  OutMsg m;
  m.type = OutMsg::RPC_RESPONSE;
  m.request_id = request_id;
  m.body = payload;
  this->enqueue_out_(std::move(m));
  return true;
}

bool ThingsBoardHttpTransport::publish_rpc_request(
    const std::string &request_id, const std::string &method,
    const std::string &params) {
  OutMsg m;
  m.type = OutMsg::RPC_REQUEST;
  m.request_id = request_id;
  m.method = method;
  m.body = params;
  this->enqueue_out_(std::move(m));
  return true;
}

bool ThingsBoardHttpTransport::publish_attribute_request(
    const std::string &request_id, const std::string &keys) {
  OutMsg m;
  m.type = OutMsg::ATTRIBUTE_REQUEST;
  m.request_id = request_id;
  m.keys = keys;
  this->enqueue_out_(std::move(m));
  return true;
}

bool ThingsBoardHttpTransport::publish_provision_request(
    const std::string &payload) {
  OutMsg m;
  m.type = OutMsg::PROVISION_REQUEST;
  m.body = payload;
  this->enqueue_out_(std::move(m));
  return true;
}

bool ThingsBoardHttpTransport::publish_claim(const std::string &payload) {
  OutMsg m;
  m.type = OutMsg::CLAIM;
  m.body = payload;
  this->enqueue_out_(std::move(m));
  return true;
}

// ----- Worker task -----

void ThingsBoardHttpTransport::worker_task_entry_(void *arg) {
  static_cast<ThingsBoardHttpTransport *>(arg)->worker_loop_();
}

void ThingsBoardHttpTransport::worker_loop_() {
#ifdef USE_ESP32
  for (;;) {
    // Wait for outbound work or the poll interval — whichever fires first.
    // Binary wake coalesces multiple enqueues into one wake; the drain loop
    // below catches everything that landed in the meantime.
    xSemaphoreTake(this->worker_wake_, pdMS_TO_TICKS(this->poll_interval_ms_));

    OutMsg msg;
    while (this->dequeue_out_(msg)) {
      this->worker_handle_out_(msg);
    }

    // Skip polling with no token (would hit `/api/v1//rpc`, rejected 400)
    // or no http parent.
    bool have_token = false;
    if (this->token_mutex_ != nullptr) {
      xSemaphoreTake(this->token_mutex_, portMAX_DELAY);
      have_token = !this->device_token_.empty();
      xSemaphoreGive(this->token_mutex_);
    }
    if (!have_token || this->http_ == nullptr) {
      continue;
    }

    const uint32_t now = millis();
    // Worst-case worker hold is 2 × poll_timeout_ms_ + socket timeouts;
    // tolerable because this task is not watched by the system WDT.
    if (now - this->last_rpc_poll_ >= this->poll_interval_ms_) {
      this->last_rpc_poll_ = now;
      this->worker_poll_rpc_();
    }
    if (now - this->last_attr_poll_ >= this->poll_interval_ms_) {
      this->last_attr_poll_ = now;
      this->worker_poll_shared_attributes_();
    }
  }
#endif
}

void ThingsBoardHttpTransport::worker_handle_out_(const OutMsg &msg) {
  switch (msg.type) {
  case OutMsg::TELEMETRY:
    this->post_(this->build_token_url_("/telemetry"), msg.body);
    break;
  case OutMsg::ATTRIBUTES:
  case OutMsg::CLIENT_ATTRIBUTES:
    this->post_(this->build_token_url_("/attributes"), msg.body);
    break;
  case OutMsg::RPC_RESPONSE: {
    std::string suffix = std::string("/rpc/") + msg.request_id;
    this->post_(this->build_token_url_(suffix.c_str()), msg.body);
    break;
  }
  case OutMsg::RPC_REQUEST: {
    std::string body = json::build_json([&](JsonObject root) {
      root["method"] = msg.method;
      if (msg.body.empty() || msg.body == "{}") {
        JsonObject p = root["params"].to<JsonObject>();
        (void) p;
      } else {
        json::parse_json(msg.body, [&](JsonVariantConst v) -> bool {
          root["params"] = v;
          return true;
        });
      }
    });
    std::string resp;
    bool ok = this->post_(this->build_token_url_("/rpc"), body, &resp);
    if (ok && !resp.empty()) {
      InMsg ev;
      ev.type = InMsg::RPC_RESPONSE;
      ev.request_id = msg.request_id;
      ev.body = resp;
      this->enqueue_in_(std::move(ev));
    }
    break;
  }
  case OutMsg::ATTRIBUTE_REQUEST: {
    std::string client_keys, shared_keys;
    json::parse_json(msg.keys, [&](JsonObjectConst root) -> bool {
      if (root["clientKeys"].is<const char *>()) {
        client_keys = root["clientKeys"].as<std::string>();
      }
      if (root["sharedKeys"].is<const char *>()) {
        shared_keys = root["sharedKeys"].as<std::string>();
      }
      return true;
    });
    std::string url = this->build_token_url_("/attributes?");
    if (!client_keys.empty())
      url += "clientKeys=" + client_keys;
    if (!shared_keys.empty()) {
      if (url.back() != '?')
        url += '&';
      url += "sharedKeys=" + shared_keys;
    }
    std::string resp;
    bool ok = this->get_(url, &resp);
    if (ok && !resp.empty()) {
      std::map<std::string, std::string> attrs;
      json::parse_json(resp, [&](JsonObjectConst root) -> bool {
        auto flatten = [&](JsonObjectConst obj) {
          for (JsonPairConst p : obj) {
            std::string k = p.key().c_str();
            std::string v;
            if (p.value().is<const char *>())
              v = p.value().as<const char *>();
            else if (p.value().is<bool>())
              v = p.value().as<bool>() ? "true" : "false";
            else if (p.value().is<int>())
              v = std::to_string(p.value().as<int>());
            else if (p.value().is<float>())
              v = std::to_string(p.value().as<float>());
            attrs[k] = v;
          }
        };
        if (root["client"].is<JsonObjectConst>())
          flatten(root["client"].as<JsonObjectConst>());
        if (root["shared"].is<JsonObjectConst>())
          flatten(root["shared"].as<JsonObjectConst>());
        return true;
      });
      InMsg ev;
      ev.type = InMsg::ATTRIBUTE_RESPONSE;
      ev.request_id = msg.request_id;
      ev.attrs = std::move(attrs);
      this->enqueue_in_(std::move(ev));
    }
    break;
  }
  case OutMsg::PROVISION_REQUEST: {
    std::string url = this->server_url_ + "/api/v1/provision";
    std::string resp;
    bool ok = this->post_(url, msg.body, &resp);
    if (ok && !resp.empty()) {
      InMsg ev;
      ev.type = InMsg::PROVISION_RESPONSE;
      ev.body = resp;
      this->enqueue_in_(std::move(ev));
    }
    break;
  }
  case OutMsg::CLAIM:
    this->post_(this->build_token_url_("/claim"), msg.body);
    break;
  }
}

void ThingsBoardHttpTransport::worker_poll_rpc_() {
  // poll_timeout_ms_ == 0: short-poll (omit ?timeout, TB returns at once).
  // Non-zero requests a server-side long-poll; only constrained by
  // http_request's socket timeout, not the system WDT.
  std::string url = this->build_token_url_("/rpc");
  if (this->poll_timeout_ms_ > 0) {
    url += "?timeout=";
    url += std::to_string(this->poll_timeout_ms_);
  }
  std::string resp;
  if (!this->get_(url, &resp) || resp.empty() || resp == "{}") {
    return;
  }
  ESP_LOGD(TAG, "RPC poll received: %s", resp.c_str());
  // TB returns either a single `{id, method, params}` or null/empty on
  // timeout.
  json::parse_json(resp, [this](JsonObjectConst root) -> bool {
    if (!root["method"].is<const char *>())
      return false;
    std::string request_id;
    if (root["id"].is<const char *>())
      request_id = root["id"].as<std::string>();
    else if (root["id"].is<int>())
      request_id = std::to_string(root["id"].as<int>());
    std::string method = root["method"].as<std::string>();
    std::string params;
    if (root["params"].is<JsonObjectConst>()) {
      JsonObjectConst params_obj = root["params"];
      params = json::build_json([&](JsonObject obj) {
        for (JsonPairConst p : params_obj)
          obj[p.key()] = p.value();
      });
    }
    InMsg ev;
    ev.type = InMsg::RPC_REQUEST;
    ev.request_id = request_id;
    ev.method = method;
    ev.body = params;
    this->enqueue_in_(std::move(ev));
    return true;
  });
}

void ThingsBoardHttpTransport::worker_poll_shared_attributes_() {
  std::string url = this->build_token_url_("/attributes/updates");
  if (this->poll_timeout_ms_ > 0) {
    url += "?timeout=";
    url += std::to_string(this->poll_timeout_ms_);
  }
  std::string resp;
  if (!this->get_(url, &resp) || resp.empty() || resp == "{}") {
    return;
  }
  ESP_LOGD(TAG, "Shared-attr poll received: %s", resp.c_str());
  std::map<std::string, std::string> attributes;
  json::parse_json(resp, [&](JsonObjectConst root) -> bool {
    for (JsonPairConst p : root) {
      std::string k = p.key().c_str();
      std::string v;
      if (p.value().is<const char *>())
        v = p.value().as<const char *>();
      else if (p.value().is<bool>())
        v = p.value().as<bool>() ? "true" : "false";
      else if (p.value().is<int>())
        v = std::to_string(p.value().as<int>());
      else if (p.value().is<float>())
        v = std::to_string(p.value().as<float>());
      attributes[k] = v;
    }
    return true;
  });
  if (!attributes.empty()) {
    InMsg ev;
    ev.type = InMsg::SHARED_ATTRIBUTES;
    ev.attrs = std::move(attributes);
    this->enqueue_in_(std::move(ev));
  }
}

// ----- Queue helpers -----

void ThingsBoardHttpTransport::enqueue_out_(OutMsg msg) {
#ifdef USE_ESP32
  if (this->out_mutex_ == nullptr) {
    return;  // setup() hasn't run yet.
  }
  xSemaphoreTake(this->out_mutex_, portMAX_DELAY);
  this->out_queue_.push(std::move(msg));
  xSemaphoreGive(this->out_mutex_);
  // give-while-given is a no-op on a binary sem — gives us free coalescing.
  xSemaphoreGive(this->worker_wake_);
#else
  this->out_queue_.push(std::move(msg));
#endif
}

bool ThingsBoardHttpTransport::dequeue_out_(OutMsg &out) {
#ifdef USE_ESP32
  if (this->out_mutex_ == nullptr)
    return false;
  xSemaphoreTake(this->out_mutex_, portMAX_DELAY);
  bool got = !this->out_queue_.empty();
  if (got) {
    out = std::move(this->out_queue_.front());
    this->out_queue_.pop();
  }
  xSemaphoreGive(this->out_mutex_);
  return got;
#else
  if (this->out_queue_.empty())
    return false;
  out = std::move(this->out_queue_.front());
  this->out_queue_.pop();
  return true;
#endif
}

void ThingsBoardHttpTransport::enqueue_in_(InMsg msg) {
#ifdef USE_ESP32
  if (this->in_mutex_ == nullptr)
    return;
  xSemaphoreTake(this->in_mutex_, portMAX_DELAY);
  this->in_queue_.push(std::move(msg));
  xSemaphoreGive(this->in_mutex_);
#else
  this->in_queue_.push(std::move(msg));
#endif
}

void ThingsBoardHttpTransport::drain_in_() {
  // Runs on the main task; callbacks fire on main, so user automations and
  // domain handlers don't need thread-safety concerns.
  while (true) {
    InMsg ev;
#ifdef USE_ESP32
    if (this->in_mutex_ == nullptr)
      return;
    xSemaphoreTake(this->in_mutex_, portMAX_DELAY);
    bool got = !this->in_queue_.empty();
    if (got) {
      ev = std::move(this->in_queue_.front());
      this->in_queue_.pop();
    }
    xSemaphoreGive(this->in_mutex_);
    if (!got)
      return;
#else
    if (this->in_queue_.empty())
      return;
    ev = std::move(this->in_queue_.front());
    this->in_queue_.pop();
#endif

    switch (ev.type) {
    case InMsg::CONNECTED:
      if (this->on_connected_)
        this->on_connected_();
      break;
    case InMsg::DISCONNECTED:
      if (this->on_disconnected_)
        this->on_disconnected_();
      break;
    case InMsg::RPC_REQUEST:
      if (this->on_rpc_request_)
        this->on_rpc_request_(ev.request_id, ev.method, ev.body);
      break;
    case InMsg::RPC_RESPONSE:
      if (this->on_rpc_response_)
        this->on_rpc_response_(ev.request_id, ev.body);
      break;
    case InMsg::SHARED_ATTRIBUTES:
      if (this->on_shared_attributes_)
        this->on_shared_attributes_(ev.attrs);
      break;
    case InMsg::ATTRIBUTE_RESPONSE:
      if (this->on_attribute_response_)
        this->on_attribute_response_(ev.request_id, ev.attrs);
      break;
    case InMsg::PROVISION_RESPONSE:
      if (this->on_provision_response_)
        this->on_provision_response_(ev.body);
      break;
    }
  }
}

}  // namespace thingsboard_http
}  // namespace esphome
