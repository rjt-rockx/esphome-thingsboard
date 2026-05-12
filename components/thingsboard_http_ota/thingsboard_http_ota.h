#pragma once

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/ota/ota_backend.h"
#include "esphome/components/ota/ota_backend_factory.h"
#include "esphome/components/thingsboard/thingsboard_client.h"
#include "esphome/components/thingsboard/transport.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include <memory>

namespace esphome {
namespace thingsboard_http_ota {

enum ThingsBoardOTAState {
  TB_OTA_IDLE,
  TB_OTA_DOWNLOADING,
  TB_OTA_DOWNLOADED,
  TB_OTA_VERIFIED,
  TB_OTA_UPDATING,
  TB_OTA_UPDATED,
  TB_OTA_FAILED
};

class ThingsBoardHttpOtaComponent
    : public ota::OTAComponent,
      public thingsboard::TBOTATransport,
      public Parented<http_request::HttpRequestComponent> {
public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::WIFI - 2.0f;
  }

  void
  set_thingsboard_component(thingsboard::ThingsBoardComponent *tb_component) {
    this->tb_component_ = tb_component;
  }

  // Invoked by core's dispatch_shared_attributes when `fw_*` attributes
  // arrive; forwards into process_firmware_update_.
  void on_firmware_advertised(const thingsboard::FirmwareInfo &info) override;
  void abort() override;

  void handle_firmware_attributes(
      const std::map<std::string, std::string> &attributes);

  void handle_download_progress(size_t bytes_downloaded, size_t total_bytes);

  void report_fw_status(const std::string &state,
                        const std::string &message = "", int progress = -1);

  void report_fw_info(const std::string &current_title,
                      const std::string &current_version,
                      const std::string &target_title,
                      const std::string &target_version, int attempt = 0);

protected:
  thingsboard::ThingsBoardComponent *tb_component_{nullptr};
  ota::OTABackendPtr backend_;

  ThingsBoardOTAState ota_state_{TB_OTA_IDLE};
  std::string fw_title_;
  std::string fw_version_;
  std::string fw_checksum_;
  std::string fw_checksum_algorithm_;
  size_t fw_size_{0};
  size_t bytes_written_{0};
  int update_attempt_{0};

  std::shared_ptr<http_request::HttpContainer> download_container_{nullptr};
  bool download_in_progress_{false};
  uint32_t download_start_time_{0};
  uint32_t last_progress_report_{0};
  size_t firmware_size_{0};

  bool start_firmware_download_(const std::string &title,
                                const std::string &version, size_t size = 0,
                                const std::string &checksum = "",
                                const std::string &checksum_algorithm = "");
  void process_download_chunk_();
  bool write_firmware_chunk_(uint8_t *data, size_t len);
  void proceed_to_update_();
  void abort_ota_(const std::string &error);
  void cleanup_download_();

  void process_firmware_update_(
      const std::map<std::string, std::string> &attributes);

  std::string build_firmware_url_(const std::string &title,
                                  const std::string &version);

  bool validate_firmware_info_(const std::string &title,
                               const std::string &version);
  bool should_update_firmware_(const std::string &version);
};

} // namespace thingsboard_http_ota
} // namespace esphome
