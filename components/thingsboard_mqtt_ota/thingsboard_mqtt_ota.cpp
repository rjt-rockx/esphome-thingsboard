#include "thingsboard_mqtt_ota.h"

#ifdef USE_ESP32

#include "esphome/components/json/json_util.h"
#include "esphome/components/thingsboard/thingsboard_client.h"
#include "esphome/components/thingsboard_mqtt/thingsboard_mqtt_transport.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard_mqtt_ota {

static const char *const TAG = "thingsboard.mqtt.ota";

void ThingsBoardMqttOtaComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MQTT OTA transport (chunk_size=%zu)",
                this->chunk_size_);
  this->backend_ = ota::make_ota_backend();
  if (!this->backend_) {
    ESP_LOGE(TAG, "Failed to create OTA backend");
    this->mark_failed();
    return;
  }
  if (this->parent_ != nullptr) {
    // The MQTT transport is created at runtime by the core component, so we
    // resolve the handle here rather than at codegen time.
    if (this->transport_ == nullptr) {
      auto *core_transport = this->parent_->get_transport();
      this->transport_ =
          static_cast<thingsboard::ThingsBoardMQTT *>(core_transport);
    }
    this->parent_->register_ota_transport(this);
  }
  if (this->transport_ != nullptr) {
    this->transport_->set_ota_handler(this);
  } else {
    ESP_LOGE(TAG, "MQTT transport handle not available; OTA will not function");
  }
}

void ThingsBoardMqttOtaComponent::loop() {
  if (this->state_ != OTA_RECEIVING)
    return;
  const uint32_t now = millis();
  if (now - this->last_progress_report_ < 5000)
    return;
  this->last_progress_report_ = now;
  int progress = 0;
  if (this->fw_.size > 0) {
    progress = static_cast<int>(100.0f * this->bytes_written_ / this->fw_.size);
  }
  this->report_state_("DOWNLOADING", "Download in progress", progress);
}

void ThingsBoardMqttOtaComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "MQTT OTA transport:");
  ESP_LOGCONFIG(TAG, "  Chunk size: %zu bytes", this->chunk_size_);
}

void ThingsBoardMqttOtaComponent::on_firmware_advertised(
    const thingsboard::FirmwareInfo &info) {
  if (this->state_ != OTA_IDLE) {
    ESP_LOGW(TAG, "OTA already in progress (state=%d), ignoring advertisement",
             this->state_);
    return;
  }
  if (info.title.empty() || info.version.empty()) {
    ESP_LOGE(TAG, "Firmware advertisement missing title/version");
    return;
  }
  if (this->transport_ == nullptr) {
    ESP_LOGE(TAG, "No MQTT transport bound; cannot start OTA");
    return;
  }
  this->fw_ = info;
  this->request_id_ = millis();
  this->next_chunk_idx_ = 0;
  this->bytes_written_ = 0;

  ESP_LOGI(TAG, "OTA advertised: %s v%s (size=%zu, checksum=%s/%s)",
           info.title.c_str(), info.version.c_str(), info.size,
           info.checksum.c_str(), info.checksum_algorithm.c_str());

  std::string sub_topic = std::string("v2/fw/response/") +
                          std::to_string(this->request_id_) + "/chunk/+";
  this->transport_->subscribe_raw(sub_topic, /*qos=*/1);

  auto begin_result = this->backend_->begin(info.size);
  if (begin_result != ota::OTA_RESPONSE_OK) {
    this->fail_("OTA backend begin failed");
    return;
  }
  if (!info.checksum.empty() && info.checksum_algorithm == "MD5") {
    this->backend_->set_update_md5(info.checksum.c_str());
  }

  this->report_fw_info_();
  this->state_ = OTA_REQUESTING;
  this->report_state_("DOWNLOADING", "Requesting first chunk", 0);
  this->request_chunk_(0);
  this->state_ = OTA_RECEIVING;
  this->last_progress_report_ = millis();
}

void ThingsBoardMqttOtaComponent::request_chunk_(uint32_t chunk_idx) {
  if (this->transport_ == nullptr)
    return;
  std::string topic = std::string("v2/fw/request/") +
                      std::to_string(this->request_id_) + "/chunk/" +
                      std::to_string(chunk_idx);
  std::string payload = std::to_string(this->chunk_size_);
  ESP_LOGV(TAG, "Requesting chunk %u (topic=%s, size=%zu)", chunk_idx,
           topic.c_str(), this->chunk_size_);
  this->transport_->publish(topic, payload, /*qos=*/1, /*retain=*/false);
}

void ThingsBoardMqttOtaComponent::on_chunk_received(uint32_t request_id, uint32_t chunk_idx,
                                         const uint8_t *data, size_t len) {
  if (this->state_ != OTA_RECEIVING) {
    ESP_LOGV(TAG, "Ignoring chunk in state %d", this->state_);
    return;
  }
  if (request_id != this->request_id_) {
    ESP_LOGW(TAG, "Chunk for unexpected request_id=%u (ours=%u)", request_id,
             this->request_id_);
    return;
  }
  if (chunk_idx != this->next_chunk_idx_) {
    ESP_LOGW(TAG, "Out-of-order chunk %u (expected %u); aborting", chunk_idx,
             this->next_chunk_idx_);
    this->fail_("Out-of-order chunk");
    return;
  }
  auto write_result = this->backend_->write(const_cast<uint8_t *>(data), len);
  if (write_result != ota::OTA_RESPONSE_OK) {
    this->fail_("Chunk write failed");
    return;
  }
  this->bytes_written_ += len;
  this->next_chunk_idx_++;

  // Per TB OTA spec: a chunk smaller than the requested size marks EOF.
  if (len < this->chunk_size_) {
    ESP_LOGI(TAG, "OTA download complete (%zu bytes)", this->bytes_written_);
    this->finish_();
    return;
  }
  this->request_chunk_(this->next_chunk_idx_);
}

void ThingsBoardMqttOtaComponent::finish_() {
  this->state_ = OTA_VERIFIED;
  if (!this->fw_.checksum.empty()) {
    this->report_state_("VERIFIED", "Firmware checksum verified", 100);
  } else {
    this->report_state_("DOWNLOADED", "Firmware downloaded", 100);
  }
  this->state_ = OTA_UPDATING;
  this->report_state_("UPDATING", "Finalizing firmware update", 100);
  auto end_result = this->backend_->end();
  if (end_result != ota::OTA_RESPONSE_OK) {
    std::string err = "Failed to finalize firmware";
    if (end_result == ota::OTA_RESPONSE_ERROR_MD5_MISMATCH) {
      err = "Checksum verification failed";
    }
    this->fail_(err);
    return;
  }
  this->state_ = OTA_DONE;
  this->report_state_("UPDATED", "Firmware update completed", 100);
  this->defer("mqtt-ota-restart", [this]() {
    ESP_LOGI(TAG, "Rebooting after MQTT OTA");
    App.safe_reboot();
  });
}

void ThingsBoardMqttOtaComponent::abort() {
  if (this->state_ == OTA_IDLE || this->state_ == OTA_DONE)
    return;
  ESP_LOGW(TAG, "MQTT OTA aborted (state=%d)", this->state_);
  if (this->backend_) {
    this->backend_->abort();
  }
  this->state_ = OTA_FAILED;
  this->report_state_("FAILED", "OTA aborted", 0);
  this->state_ = OTA_IDLE;
}

void ThingsBoardMqttOtaComponent::fail_(const std::string &reason) {
  ESP_LOGE(TAG, "MQTT OTA failed: %s", reason.c_str());
  if (this->backend_) {
    this->backend_->abort();
  }
  this->state_ = OTA_FAILED;
  this->report_state_("FAILED", reason, 0);
  this->state_ = OTA_IDLE;
}

void ThingsBoardMqttOtaComponent::report_state_(const char *state,
                                     const std::string &message, int progress) {
  if (this->parent_ == nullptr)
    return;
  // Per TB OTA spec: fw_state ∈ {DOWNLOADING, DOWNLOADED, VERIFIED, UPDATING,
  // UPDATED, FAILED}; fw_error carries the failure reason on FAILED.
  std::string payload = json::build_json([&](JsonObject root) {
    root["fw_state"] = state;
    bool is_failed = std::string(state) == "FAILED";
    if (is_failed && !message.empty())
      root["fw_error"] = message;
    if (progress >= 0)
      root["fw_state_progress"] = progress;
  });
  this->parent_->send_telemetry(payload);
}

void ThingsBoardMqttOtaComponent::report_fw_info_() {
  if (this->parent_ == nullptr)
    return;
  // TB dispatches an update when shared `fw_title`/`fw_version` differs from
  // the device's reported `current_fw_title`/`current_fw_version` telemetry.
  std::string payload = json::build_json([&](JsonObject root) {
#ifdef ESPHOME_PROJECT_NAME
    root["current_fw_title"] = ESPHOME_PROJECT_NAME;
#endif
#ifdef ESPHOME_PROJECT_VERSION
    root["current_fw_version"] = ESPHOME_PROJECT_VERSION;
#endif
  });
  this->parent_->send_telemetry(payload);
}

}  // namespace thingsboard_mqtt_ota
}  // namespace esphome

#endif  // USE_ESP32
