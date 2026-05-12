#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include <memory>
#include <string>

#include "esphome/components/ota/ota_backend.h"
#include "esphome/components/ota/ota_backend_factory.h"
#include "esphome/components/thingsboard/transport.h"
#include "esphome/core/component.h"

namespace esphome {
namespace thingsboard {
class ThingsBoardComponent;
class ThingsBoardMQTT;
}  // namespace thingsboard

namespace thingsboard_mqtt_ota {

// ThingsBoard chunked binary OTA over MQTT.
// See https://thingsboard.io/docs/reference/mqtt-api/#firmware-update.
class ThingsBoardMqttOtaComponent : public ota::OTAComponent,
                                    public thingsboard::TBOTATransport {
 public:
  enum State {
    OTA_IDLE,
    OTA_REQUESTING,
    OTA_RECEIVING,
    OTA_VERIFIED,
    OTA_UPDATING,
    OTA_DONE,
    OTA_FAILED,
  };

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI;
  }

  void set_thingsboard_component(thingsboard::ThingsBoardComponent *p) {
    parent_ = p;
  }
  void set_chunk_size(size_t s) { chunk_size_ = s; }

  void on_firmware_advertised(const thingsboard::FirmwareInfo &info) override;
  void abort() override;

  // `data` is binary, NOT null-terminated.
  void on_chunk_received(uint32_t request_id, uint32_t chunk_idx,
                         const uint8_t *data, size_t len);

 protected:
  void request_chunk_(uint32_t chunk_idx);
  void finish_();
  void fail_(const std::string &reason);
  void report_state_(const char *state, const std::string &message = "",
                     int progress = -1);
  void report_fw_info_();

  thingsboard::ThingsBoardComponent *parent_{nullptr};
  thingsboard::ThingsBoardMQTT *transport_{nullptr};
  ota::OTABackendPtr backend_;
  size_t chunk_size_{4096};

  State state_{OTA_IDLE};
  thingsboard::FirmwareInfo fw_;
  uint32_t request_id_{0};
  uint32_t next_chunk_idx_{0};
  size_t bytes_written_{0};
  uint32_t last_progress_report_{0};
};

}  // namespace thingsboard_mqtt_ota
}  // namespace esphome

#endif  // USE_ESP32
