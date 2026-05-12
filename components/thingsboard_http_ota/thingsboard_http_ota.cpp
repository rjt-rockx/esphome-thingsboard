#include "thingsboard_http_ota.h"
#include "esphome/components/json/json_util.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace thingsboard_http_ota {

static const char *const TAG = "thingsboard_http_ota";

void ThingsBoardHttpOtaComponent::setup() {
  ESP_LOGD(TAG, "Setting up ThingsBoard OTA");

  if (this->tb_component_ == nullptr) {
    ESP_LOGE(TAG, "ThingsBoard component not set!");
    this->mark_failed();
    return;
  }

  this->backend_ = ota::make_ota_backend();
  if (!this->backend_) {
    ESP_LOGE(TAG, "Failed to create OTA backend");
    this->mark_failed();
    return;
  }

#ifdef USE_THINGSBOARD_HTTP_OTA
  this->tb_component_->register_ota_component(this);
#endif

  // Drives us via TBOTATransport::on_firmware_advertised. The
  // register_ota_component path above is retained for defence-in-depth;
  // both routes converge on process_firmware_update_.
  this->tb_component_->register_ota_transport(this);

  ESP_LOGD(TAG, "ThingsBoard OTA component initialized");
}

void ThingsBoardHttpOtaComponent::on_firmware_advertised(
    const thingsboard::FirmwareInfo &info) {
  std::map<std::string, std::string> attrs;
  attrs["fw_title"] = info.title;
  attrs["fw_version"] = info.version;
  if (info.size > 0) attrs["fw_size"] = std::to_string(info.size);
  if (!info.checksum.empty()) attrs["fw_checksum"] = info.checksum;
  if (!info.checksum_algorithm.empty())
    attrs["fw_checksum_algorithm"] = info.checksum_algorithm;
  this->process_firmware_update_(attrs);
}

void ThingsBoardHttpOtaComponent::abort() {
  this->abort_ota_("Aborted via transport");
}

void ThingsBoardHttpOtaComponent::loop() {
  if (this->download_in_progress_ && this->download_container_) {
    this->process_download_chunk_();
  }

  // Report progress every 5s during download.
  uint32_t now = millis();
  if (this->ota_state_ == TB_OTA_DOWNLOADING &&
      now - this->last_progress_report_ > 5000) {
    float progress = 0.0f;
    if (this->fw_size_ > 0) {
      progress = (float)this->bytes_written_ / this->fw_size_ * 100.0f;
    }
    this->report_fw_status("DOWNLOADING", "Download in progress",
                           (int)progress);
    this->last_progress_report_ = now;
  }
}

void ThingsBoardHttpOtaComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ThingsBoard OTA:");
  ESP_LOGCONFIG(TAG, "  ThingsBoard Component: %s",
                this->tb_component_ ? "Connected" : "Not connected");
}

void ThingsBoardHttpOtaComponent::handle_firmware_attributes(
    const std::map<std::string, std::string> &attributes) {
  ESP_LOGD(TAG, "Processing firmware shared attributes");

  auto fw_title_it = attributes.find("fw_title");
  auto fw_version_it = attributes.find("fw_version");

  if (fw_title_it != attributes.end() && fw_version_it != attributes.end()) {
    ESP_LOGI(TAG, "Firmware update notification: %s v%s",
             fw_title_it->second.c_str(), fw_version_it->second.c_str());
    this->process_firmware_update_(attributes);
  }
}

void ThingsBoardHttpOtaComponent::process_firmware_update_(
    const std::map<std::string, std::string> &attributes) {
  if (this->ota_state_ != TB_OTA_IDLE) {
    ESP_LOGW(TAG, "OTA already in progress, ignoring firmware update request");
    return;
  }

  auto fw_title_it = attributes.find("fw_title");
  auto fw_version_it = attributes.find("fw_version");
  auto fw_size_it = attributes.find("fw_size");
  auto fw_checksum_it = attributes.find("fw_checksum");
  auto fw_checksum_algo_it = attributes.find("fw_checksum_algorithm");

  if (fw_title_it == attributes.end() || fw_version_it == attributes.end()) {
    ESP_LOGE(TAG,
             "Missing required firmware attributes (fw_title, fw_version)");
    this->report_fw_status("FAILED", "Missing firmware parameters");
    return;
  }

  std::string fw_title = fw_title_it->second;
  std::string fw_version = fw_version_it->second;
  size_t fw_size =
      fw_size_it != attributes.end() ? std::stoul(fw_size_it->second) : 0;
  std::string fw_checksum =
      fw_checksum_it != attributes.end() ? fw_checksum_it->second : "";
  std::string fw_checksum_algo = fw_checksum_algo_it != attributes.end()
                                     ? fw_checksum_algo_it->second
                                     : "";

  ESP_LOGI(TAG, "Processing firmware update: %s v%s (size: %zu bytes)",
           fw_title.c_str(), fw_version.c_str(), fw_size);

  if (!this->should_update_firmware_(fw_version)) {
    ESP_LOGI(TAG, "Firmware update not needed - already running version %s",
             fw_version.c_str());
    return;
  }

  if (!this->start_firmware_download_(fw_title, fw_version, fw_size,
                                      fw_checksum, fw_checksum_algo)) {
    this->report_fw_status("FAILED", "Failed to start firmware download");
  }
}

bool ThingsBoardHttpOtaComponent::start_firmware_download_(
    const std::string &title, const std::string &version, size_t size,
    const std::string &checksum, const std::string &checksum_algorithm) {
  if (!this->validate_firmware_info_(title, version)) {
    return false;
  }

  this->fw_title_ = title;
  this->fw_version_ = version;
  this->fw_size_ = size;
  this->fw_checksum_ = checksum;
  this->fw_checksum_algorithm_ = checksum_algorithm;
  this->bytes_written_ = 0;
  this->update_attempt_++;

  std::string firmware_url = this->build_firmware_url_(title, version);
  ESP_LOGI(TAG, "Starting firmware download from: %s", firmware_url.c_str());

#ifdef ESPHOME_PROJECT_NAME
  const char *current_fw_title = ESPHOME_PROJECT_NAME;
#else
  const char *current_fw_title = "";
#endif
#ifdef ESPHOME_PROJECT_VERSION
  const char *current_fw_version = ESPHOME_PROJECT_VERSION;
#else
  const char *current_fw_version = ESPHOME_VERSION;
#endif
  this->report_fw_info(current_fw_title, current_fw_version, title, version,
                       this->update_attempt_);
  this->report_fw_status("DOWNLOADING", "Starting firmware download", 0);

  std::list<http_request::Header> headers;
  headers.push_back({"User-Agent", "ESPHome-ThingsBoard-OTA/1.0"});
  headers.push_back({"Connection", "close"});

  this->download_container_ = this->get_parent()->get(firmware_url, headers);
  if (!this->download_container_) {
    ESP_LOGE(TAG, "Failed to start HTTP download");
    return false;
  }

  this->download_in_progress_ = true;
  this->download_start_time_ = millis();
  this->ota_state_ = TB_OTA_DOWNLOADING;

#ifdef USE_OTA_STATE_CALLBACK
  this->state_callback_.call(ota::OTA_STARTED, 0.0f, 0);
#endif

  return true;
}

void ThingsBoardHttpOtaComponent::process_download_chunk_() {
  if (!this->download_container_) {
    return;
  }

  // 30 minute hard cap on the whole download.
  if (millis() - this->download_start_time_ > 1800000) {
    ESP_LOGE(TAG, "Download timeout");
    this->abort_ota_("Download timeout");
    return;
  }

  if (this->download_container_->status_code == 0) {
    return; // Still waiting for response headers.
  }

  if (this->download_container_->status_code != 200) {
    ESP_LOGE(TAG, "HTTP download failed with status: %d",
             this->download_container_->status_code);
    this->abort_ota_("HTTP download failed");
    return;
  }

  // First chunk: initialise OTA backend with fw_size from shared attributes
  // when available; otherwise fall back to unknown size.
  if (this->firmware_size_ == 0 && this->fw_size_ > 0) {
    this->firmware_size_ = this->fw_size_;

    ota::OTAResponseTypes result = this->backend_->begin(this->firmware_size_);
    if (result != ota::OTA_RESPONSE_OK) {
      ESP_LOGE(TAG, "Failed to begin OTA: %d", result);
      this->abort_ota_("Failed to initialize OTA backend");
      return;
    }

    if (!this->fw_checksum_.empty() && this->fw_checksum_algorithm_ == "MD5") {
      this->backend_->set_update_md5(this->fw_checksum_.c_str());
    }

    this->ota_state_ = TB_OTA_DOWNLOADING;
    ESP_LOGI(TAG, "Started downloading firmware (%zu bytes)",
             this->firmware_size_);
  } else if (this->firmware_size_ == 0) {
    ota::OTAResponseTypes result = this->backend_->begin(0);
    if (result != ota::OTA_RESPONSE_OK) {
      ESP_LOGE(TAG, "Failed to begin OTA: %d", result);
      this->abort_ota_("Failed to initialize OTA backend");
      return;
    }

    this->ota_state_ = TB_OTA_DOWNLOADING;
    ESP_LOGI(TAG, "Started downloading firmware (unknown size)");
  }

  uint8_t buffer[512];
  int bytes_read = this->download_container_->read(buffer, sizeof(buffer));

  if (bytes_read > 0) {
    if (!this->write_firmware_chunk_(buffer, bytes_read)) {
      this->abort_ota_("Failed to write firmware chunk");
      return;
    }
  } else if (bytes_read == 0) {
    ESP_LOGI(TAG, "Firmware download completed (%zu bytes)",
             this->bytes_written_);
    this->ota_state_ = TB_OTA_DOWNLOADED;
    this->report_fw_status("DOWNLOADED", "Firmware downloaded successfully",
                           100);
    this->proceed_to_update_();
  } else {
    ESP_LOGE(TAG, "Error reading firmware data");
    this->abort_ota_("Error reading firmware data");
  }
}

bool ThingsBoardHttpOtaComponent::write_firmware_chunk_(uint8_t *data, size_t len) {
  ota::OTAResponseTypes result = this->backend_->write(data, len);
  if (result != ota::OTA_RESPONSE_OK) {
    ESP_LOGE(TAG, "Failed to write firmware chunk: %d", result);
    return false;
  }

  this->bytes_written_ += len;

  float progress = 0.0f;
  if (this->fw_size_ > 0) {
    progress = (float)this->bytes_written_ / this->fw_size_ * 100.0f;
  }

#ifdef USE_OTA_STATE_CALLBACK
  this->state_callback_.call(ota::OTA_IN_PROGRESS, progress, 0);
#endif

  ESP_LOGV(TAG, "Written %zu bytes (%.1f%%)", this->bytes_written_, progress);
  return true;
}

void ThingsBoardHttpOtaComponent::proceed_to_update_() {
  this->cleanup_download_();

  // backend_->end() verifies the MD5 checksum if one was set above.
  if (!this->fw_checksum_.empty()) {
    ESP_LOGI(TAG, "Verifying firmware checksum...");
    this->ota_state_ = TB_OTA_VERIFIED;
    this->report_fw_status("VERIFIED", "Firmware checksum verified", 100);
  }

  ESP_LOGI(TAG, "Finalizing firmware update...");
  this->ota_state_ = TB_OTA_UPDATING;
  this->report_fw_status("UPDATING", "Installing firmware update", 100);

  ota::OTAResponseTypes result = this->backend_->end();
  if (result != ota::OTA_RESPONSE_OK) {
    ESP_LOGE(TAG, "Failed to finalize OTA: %d", result);
    std::string error_msg = "Failed to finalize firmware";
    if (result == ota::OTA_RESPONSE_ERROR_MD5_MISMATCH) {
      error_msg = "Checksum verification failed";
    }
    this->abort_ota_(error_msg);
    return;
  }

  this->ota_state_ = TB_OTA_UPDATED;
  ESP_LOGI(TAG, "OTA update completed successfully!");

  this->report_fw_status("UPDATED", "Firmware update completed", 100);

#ifdef USE_OTA_STATE_CALLBACK
  this->state_callback_.call(ota::OTA_COMPLETED, 100.0f, 0);
#endif

  // Defer restart so the status report has a chance to flush.
  this->defer("ota_restart", [this]() {
    ESP_LOGI(TAG, "Restarting after OTA update...");
    App.safe_reboot();
  });
}

void ThingsBoardHttpOtaComponent::abort_ota_(const std::string &error) {
  ESP_LOGE(TAG, "Aborting OTA: %s", error.c_str());

  this->cleanup_download_();

  if (this->backend_) {
    this->backend_->abort();
  }

  this->ota_state_ = TB_OTA_FAILED;
  this->report_fw_status("FAILED", error, 0);

#ifdef USE_OTA_STATE_CALLBACK
  this->state_callback_.call(ota::OTA_ERROR, 0.0f, 1);
#endif

  this->defer("ota_reset", [this]() { this->ota_state_ = TB_OTA_IDLE; });
}

void ThingsBoardHttpOtaComponent::cleanup_download_() {
  if (this->download_container_) {
    this->download_container_->end();
    this->download_container_.reset();
  }
  this->download_in_progress_ = false;
}

void ThingsBoardHttpOtaComponent::report_fw_status(const std::string &state,
                                               const std::string &message,
                                               int progress) {
  if (!this->tb_component_) {
    return;
  }

  std::string attributes = json::build_json([&](JsonObject root) {
    root["ota.state"] = state;
    if (!message.empty()) {
      root["ota.message"] = message;
    }
    if (progress >= 0) {
      root["ota.progress"] = progress;
    }
  });

  this->tb_component_->send_attributes(attributes);
  ESP_LOGD(TAG, "Reported firmware status: %s - %s (%d%%)", state.c_str(),
           message.c_str(), progress);
}

void ThingsBoardHttpOtaComponent::report_fw_info(const std::string &current_title,
                                             const std::string &current_version,
                                             const std::string &target_title,
                                             const std::string &target_version,
                                             int attempt) {
  if (!this->tb_component_) {
    return;
  }

  std::string attributes = json::build_json([&](JsonObject root) {
    root["ota.current_title"] = current_title;
    root["ota.current_version"] = current_version;
    root["ota.target_title"] = target_title;
    root["ota.target_version"] = target_version;
    if (attempt > 0) {
      root["ota.attempt"] = attempt;
    }
  });

  this->tb_component_->send_attributes(attributes);
  ESP_LOGD(TAG, "Reported firmware info: %s v%s -> %s v%s (attempt %d)",
           current_title.c_str(), current_version.c_str(), target_title.c_str(),
           target_version.c_str(), attempt);
}

std::string
ThingsBoardHttpOtaComponent::build_firmware_url_(const std::string &title,
                                             const std::string &version) {
  char encoded_title[128];
  char encoded_version[64];

  auto url_encode = [](char *dst, const char *src, size_t dst_size) -> size_t {
    const char *hex = "0123456789ABCDEF";
    size_t src_len = strlen(src);
    size_t dst_pos = 0;

    for (size_t i = 0; i < src_len && dst_pos < dst_size - 1; i++) {
      unsigned char c = (unsigned char)src[i];

      if (c == ' ' || c == '"' || c == '<' || c == '>' || c == '#' ||
          c == '%' || c == '{' || c == '}' || c == '|' || c == '\\' ||
          c == '^' || c == '~' || c == '[' || c == ']' || c == '`' ||
          c < 0x20 || c > 0x7E) {

        if (dst_pos + 3 >= dst_size)
          break;
        dst[dst_pos++] = '%';
        dst[dst_pos++] = hex[c >> 4];
        dst[dst_pos++] = hex[c & 0x0F];
      } else {
        dst[dst_pos++] = c;
      }
    }
    dst[dst_pos] = '\0';
    return dst_pos;
  };

  url_encode(encoded_title, title.c_str(), sizeof(encoded_title));
  url_encode(encoded_version, version.c_str(), sizeof(encoded_version));

  // /api/v1/{ACCESS_TOKEN}/firmware?title=...&version=...
  std::string base_url = this->tb_component_->get_server_url();
  if (base_url.back() == '/') {
    base_url.pop_back();
  }

  std::string access_token = this->tb_component_->get_access_token();
  if (access_token.empty()) {
    ESP_LOGE(TAG, "Device not provisioned - access token missing");
    return "";
  }

  return base_url + "/api/v1/" + access_token +
         "/firmware?title=" + encoded_title + "&version=" + encoded_version;
}

bool ThingsBoardHttpOtaComponent::validate_firmware_info_(
    const std::string &title, const std::string &version) {
  if (title.empty()) {
    ESP_LOGE(TAG, "Firmware title is empty");
    return false;
  }

  if (version.empty()) {
    ESP_LOGE(TAG, "Firmware version is empty");
    return false;
  }

  return true;
}

bool ThingsBoardHttpOtaComponent::should_update_firmware_(
    const std::string &version) {
#ifdef ESPHOME_PROJECT_VERSION
  std::string current_version = ESPHOME_PROJECT_VERSION;
#else
  std::string current_version = ESPHOME_VERSION;
#endif

  if (version == current_version) {
    ESP_LOGI(TAG, "Already running firmware version: %s", version.c_str());
    return false;
  }

  ESP_LOGI(TAG, "Firmware update available: %s -> %s", current_version.c_str(),
           version.c_str());
  return true;
}

} // namespace thingsboard_http_ota
} // namespace esphome
