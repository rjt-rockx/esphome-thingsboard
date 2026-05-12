#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace esphome {
namespace thingsboard {

// Firmware metadata advertised by ThingsBoard via shared attributes
// (`fw_title`, `fw_version`, `fw_size`, `fw_checksum`, `fw_checksum_algorithm`).
struct FirmwareInfo {
  std::string title;
  std::string version;
  std::string checksum;
  std::string checksum_algorithm;
  size_t size{0};
};

// Optional OTA surface a transport may implement. Implementations report state
// transitions back through telemetry on the owning transport — `fw_state` ∈
// {DOWNLOADING, DOWNLOADED, VERIFIED, UPDATING, UPDATED, FAILED}.
class TBOTATransport {
 public:
  virtual ~TBOTATransport() = default;
  // Invoked by core when a new `fw_*` shared-attribute set arrives.
  virtual void on_firmware_advertised(const FirmwareInfo &info) = 0;
  // Abort an in-flight transfer (e.g., on disconnect).
  virtual void abort() = 0;
};

// Transport-agnostic interface the core ThingsBoardComponent uses to talk to
// a TB server. Concrete implementations live in sibling components
// (thingsboard_mqtt, thingsboard_http); the core never touches an ESP-MQTT
// client or esp_http_client directly.
class TBTransport {
 public:
  virtual ~TBTransport() = default;

  // True when the transport believes it can publish.
  virtual bool is_connected() const = 0;

  virtual bool publish_telemetry(const std::string &payload) = 0;

  // publish_attributes() is retained for historical call sites;
  // publish_client_attributes() is the canonical one.
  virtual bool publish_attributes(const std::string &payload) = 0;
  virtual bool publish_client_attributes(const std::string &payload) = 0;

  virtual bool publish_rpc_response(const std::string &request_id,
                                    const std::string &payload) = 0;

  virtual bool publish_rpc_request(const std::string &request_id,
                                   const std::string &method,
                                   const std::string &params) = 0;

  // `keys` is the raw MQTT payload body (e.g.
  // `{"clientKeys":"a,b","sharedKeys":"c,d"}`); the HTTP transport re-emits it
  // as a query string.
  virtual bool publish_attribute_request(const std::string &request_id,
                                         const std::string &keys) = 0;

  virtual bool publish_provision_request(const std::string &payload) = 0;

  virtual bool publish_claim(const std::string &payload) = 0;

  // Concrete transports invoke these when an inbound event arrives so the same
  // core dispatch logic runs regardless of transport.
  using ConnectedCb = std::function<void()>;
  using DisconnectedCb = std::function<void()>;
  using RpcRequestCb = std::function<void(const std::string &request_id,
                                          const std::string &method,
                                          const std::string &params)>;
  using SharedAttributesCb =
      std::function<void(const std::map<std::string, std::string> &)>;
  using AttributeResponseCb =
      std::function<void(const std::string &request_id,
                         const std::map<std::string, std::string> &)>;
  using RpcResponseCb = std::function<void(const std::string &request_id,
                                           const std::string &response)>;
  using ProvisionResponseCb =
      std::function<void(const std::string &response_json)>;

  void set_on_connected(ConnectedCb cb) { on_connected_ = std::move(cb); }
  void set_on_disconnected(DisconnectedCb cb) {
    on_disconnected_ = std::move(cb);
  }
  void set_on_rpc_request(RpcRequestCb cb) { on_rpc_request_ = std::move(cb); }
  void set_on_shared_attributes(SharedAttributesCb cb) {
    on_shared_attributes_ = std::move(cb);
  }
  void set_on_attribute_response(AttributeResponseCb cb) {
    on_attribute_response_ = std::move(cb);
  }
  void set_on_rpc_response(RpcResponseCb cb) {
    on_rpc_response_ = std::move(cb);
  }
  void set_on_provision_response(ProvisionResponseCb cb) {
    on_provision_response_ = std::move(cb);
  }

 protected:
  ConnectedCb on_connected_;
  DisconnectedCb on_disconnected_;
  RpcRequestCb on_rpc_request_;
  SharedAttributesCb on_shared_attributes_;
  AttributeResponseCb on_attribute_response_;
  RpcResponseCb on_rpc_response_;
  ProvisionResponseCb on_provision_response_;
};

}  // namespace thingsboard
}  // namespace esphome
