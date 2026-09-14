#include "evbox_smartgrid.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace evbox_smartgrid {

static const char *const TAG = "evbox_smartgrid";

static const uint8_t STX = 0x02;
static const uint8_t ETX = 0x03;

static const char *const ADDRESS_CHARGEPOINT = "80";
static const char *const ADDRESS_SMARTGRID = "A0";
static const char *const COMMAND_MAX_CURRENTS = "69";

// Protocol Max v4: a module must see the bus idle for at least 100 ms before it transmits.
static const uint32_t BUS_IDLE_MS = 100;
// Transmit anyway if the bus never goes quiet, so the ChargePoint does not fall back.
static const uint32_t BUS_BUSY_GIVE_UP_MS = 2000;
// A reply describing one ChargeBox is 60 characters; anything this long is noise.
static const size_t MAX_FRAME_CHARS = 512;
// Reply data: minimum interval (word), connection maximum current (word), ChargeBox count (byte).
static const size_t REPLY_HEADER_CHARS = 10;
// Per ChargeBox: minimum current, three used currents and three cos phi (a word each), followed
// by the meter reading in Wh (two words).
static const size_t CHARGEBOX_CHARS = 36;

static void append_hex(std::string &out, uint32_t value, int digits) {
  static const char *const HEX_DIGITS = "0123456789ABCDEF";
  for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
    out += HEX_DIGITS[(value >> shift) & 0x0F];
}

static bool parse_hex(const std::string &text, size_t pos, size_t digits, uint32_t *value) {
  if (pos + digits > text.size())
    return false;
  uint32_t result = 0;
  for (size_t i = pos; i < pos + digits; i++) {
    const char c = text[i];
    uint8_t nibble;
    if (c >= '0' && c <= '9') {
      nibble = c - '0';
    } else if (c >= 'A' && c <= 'F') {
      nibble = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
      nibble = c - 'a' + 10;
    } else {
      return false;
    }
    result = (result << 4) | nibble;
  }
  *value = result;
  return true;
}

// Both checksums cover the ASCII characters of address, command and data.
static void calculate_checksums(const std::string &text, size_t len, uint8_t *sum, uint8_t *parity) {
  uint8_t s = 0;
  uint8_t p = 0;
  for (size_t i = 0; i < len; i++) {
    s += static_cast<uint8_t>(text[i]);
    p ^= static_cast<uint8_t>(text[i]);
  }
  *sum = s;
  *parity = p;
}

static uint16_t to_tenths(float amps) {
  if (!(amps > 0.0f))  // negative or NaN
    return 0;
  const float tenths = std::round(amps * 10.0f);
  return tenths >= 65535.0f ? 65535 : static_cast<uint16_t>(tenths);
}

void EVBoxSmartGrid::loop() {
  uint8_t byte;
  while (this->available() > 0 && this->read_byte(&byte)) {
    this->last_bus_activity_ms_ = millis();
    if (byte == STX) {
      this->rx_frame_.clear();
      this->receiving_ = true;
    } else if (!this->receiving_) {
      continue;  // bytes outside a frame, such as a trailing 0xFF
    } else if (byte == ETX) {
      this->receiving_ = false;
      this->handle_frame_(this->rx_frame_);
      this->rx_frame_.clear();
    } else if (this->rx_frame_.size() >= MAX_FRAME_CHARS) {
      ESP_LOGW(TAG, "Discarding frame longer than %u characters", static_cast<unsigned>(MAX_FRAME_CHARS));
      this->receiving_ = false;
      this->rx_frame_.clear();
    } else {
      this->rx_frame_.push_back(static_cast<char>(byte));
    }
  }

  if (!this->send_pending_)
    return;
  const uint32_t now = millis();
  const uint32_t waited = now - this->send_requested_ms_;
  if (waited < this->send_delay_ms_)
    return;
  const bool idle = !this->receiving_ && now - this->last_bus_activity_ms_ >= BUS_IDLE_MS;
  if (!idle && waited < this->send_delay_ms_ + BUS_BUSY_GIVE_UP_MS)
    return;
  if (!idle)
    ESP_LOGW(TAG, "Bus stayed busy, transmitting anyway");
  this->send_pending_ = false;
  this->send_max_currents_();
}

void EVBoxSmartGrid::update() {
  this->publish_responding_();
  if (this->keepalive_enabled_)
    this->request_send_();
}

void EVBoxSmartGrid::set_max_current(float amps) {
  if (std::isnan(amps) || amps < 0.0f)
    amps = 0.0f;
  const bool changed = amps != this->max_current_;
  this->max_current_ = amps;
  // Before the first frame this is configuration; afterwards apply a new limit without
  // waiting for the next keep-alive.
  if (changed && this->keepalive_enabled_ && this->has_sent_)
    this->request_send_();
}

void EVBoxSmartGrid::set_keepalive_enabled(bool enabled) {
  if (enabled == this->keepalive_enabled_)
    return;
  this->keepalive_enabled_ = enabled;
  if (enabled) {
    ESP_LOGI(TAG, "Keep-alive enabled");
    this->request_send_();
  } else {
    ESP_LOGI(TAG, "Keep-alive disabled; the ChargePoint falls back to %.1f A after %u s", this->fallback_current_,
             static_cast<unsigned>(this->keepalive_timeout_s_));
    this->send_pending_ = false;
  }
}

void EVBoxSmartGrid::request_send_() {
  const uint32_t now = millis();
  uint32_t wait_ms = 0;
  // Never send more often than the ChargePoint's reported minimum interval.
  if (this->has_sent_ && this->min_interval_s_ > 0) {
    const uint32_t min_gap = this->min_interval_s_ * 1000UL;
    const uint32_t since_last = now - this->last_send_ms_;
    if (since_last < min_gap)
      wait_ms = min_gap - since_last;
  }
  if (this->send_pending_) {
    // Keep whichever deadline comes first.
    const uint32_t waited = now - this->send_requested_ms_;
    const uint32_t remaining = waited >= this->send_delay_ms_ ? 0 : this->send_delay_ms_ - waited;
    wait_ms = std::min(wait_ms, remaining);
  }
  this->send_pending_ = true;
  this->send_requested_ms_ = now;
  this->send_delay_ms_ = wait_ms;
}

void EVBoxSmartGrid::send_max_currents_() {
  const uint16_t limit = to_tenths(this->max_current_);
  const uint16_t fallback = to_tenths(this->fallback_current_);

  std::string frame;
  frame.reserve(40);
  frame += ADDRESS_CHARGEPOINT;
  frame += ADDRESS_SMARTGRID;
  frame += COMMAND_MAX_CURRENTS;
  for (int phase = 0; phase < 3; phase++)
    append_hex(frame, limit, 4);
  append_hex(frame, this->keepalive_timeout_s_, 4);
  for (int phase = 0; phase < 3; phase++)
    append_hex(frame, fallback, 4);

  uint8_t sum;
  uint8_t parity;
  calculate_checksums(frame, frame.size(), &sum, &parity);
  append_hex(frame, sum, 2);
  append_hex(frame, parity, 2);

  this->write_byte(STX);
  this->write_array(reinterpret_cast<const uint8_t *>(frame.data()), frame.size());
  this->write_byte(ETX);
  this->flush();

  const uint32_t now = millis();
  this->has_sent_ = true;
  this->last_send_ms_ = now;
  this->last_bus_activity_ms_ = now;
  ESP_LOGD(TAG, "Sent %.1f A per phase (timeout %u s, fallback %.1f A): %s", limit / 10.0f,
           static_cast<unsigned>(this->keepalive_timeout_s_), fallback / 10.0f, frame.c_str());
}

void EVBoxSmartGrid::handle_frame_(const std::string &frame) {
  // Destination, source and command take 6 characters, the checksums another 4.
  if (frame.size() < 10) {
    ESP_LOGV(TAG, "Ignoring short frame: %s", frame.c_str());
    return;
  }
  const size_t body_len = frame.size() - 4;
  uint8_t sum;
  uint8_t parity;
  calculate_checksums(frame, body_len, &sum, &parity);
  uint32_t received_sum;
  uint32_t received_parity;
  if (!parse_hex(frame, body_len, 2, &received_sum) || !parse_hex(frame, body_len + 2, 2, &received_parity) ||
      received_sum != sum || received_parity != parity) {
    ESP_LOGD(TAG, "Ignoring frame with invalid checksum: %s", frame.c_str());
    return;
  }
  // Also filters out our own frames, which some auto-direction transceivers echo back.
  if (frame.compare(0, 2, ADDRESS_SMARTGRID) != 0 || frame.compare(2, 2, ADDRESS_CHARGEPOINT) != 0) {
    ESP_LOGV(TAG, "Ignoring frame for another module: %s", frame.c_str());
    return;
  }
  if (frame.compare(4, 2, COMMAND_MAX_CURRENTS) != 0) {
    ESP_LOGD(TAG, "Ignoring unsupported command from ChargePoint: %s", frame.c_str());
    return;
  }
  this->handle_max_currents_reply_(frame.substr(6, body_len - 6));
}

void EVBoxSmartGrid::handle_max_currents_reply_(const std::string &data) {
  uint32_t min_interval;
  uint32_t connection_max;
  uint32_t chargeboxes;
  if (!parse_hex(data, 0, 4, &min_interval) || !parse_hex(data, 4, 4, &connection_max) ||
      !parse_hex(data, 8, 2, &chargeboxes)) {
    ESP_LOGW(TAG, "Malformed reply: %s", data.c_str());
    return;
  }

  this->has_reply_ = true;
  this->last_reply_ms_ = millis();
  this->min_interval_s_ = min_interval;
  this->publish_responding_();

  if (this->minimum_interval_sensor_ != nullptr)
    this->minimum_interval_sensor_->publish_state(min_interval);
  if (this->connection_max_current_sensor_ != nullptr)
    this->connection_max_current_sensor_->publish_state(connection_max / 10.0f);
  if (!this->min_interval_logged_ && this->get_update_interval() < min_interval * 1000UL) {
    ESP_LOGI(TAG, "ChargePoint accepts one frame every %" PRIu32 " s; spacing frames accordingly", min_interval);
    this->min_interval_logged_ = true;
  }

  if (chargeboxes == 0) {
    ESP_LOGW(TAG, "ChargePoint reports no ChargeBox modules");
    return;
  }
  if (data.size() < REPLY_HEADER_CHARS + CHARGEBOX_CHARS) {
    ESP_LOGW(TAG, "Reply too short for ChargeBox data: %s", data.c_str());
    return;
  }
  if (chargeboxes > 1)
    ESP_LOGD(TAG, "ChargePoint reports %" PRIu32 " ChargeBox modules; publishing the first", chargeboxes);

  const size_t base = REPLY_HEADER_CHARS;
  uint32_t value;
  if (this->minimum_current_sensor_ != nullptr && parse_hex(data, base, 4, &value))
    this->minimum_current_sensor_->publish_state(value / 10.0f);
  for (uint8_t phase = 0; phase < 3; phase++) {
    if (this->current_sensors_[phase] != nullptr && parse_hex(data, base + 4 + phase * 4, 4, &value))
      this->current_sensors_[phase]->publish_state(value / 10.0f);
    if (this->power_factor_sensors_[phase] != nullptr && parse_hex(data, base + 16 + phase * 4, 4, &value))
      this->power_factor_sensors_[phase]->publish_state(static_cast<int16_t>(value) / 1000.0f);
  }
  if (this->energy_sensor_ != nullptr && parse_hex(data, base + 28, 8, &value))
    this->energy_sensor_->publish_state(value / 1000.0f);  // Wh to kWh
}

void EVBoxSmartGrid::publish_responding_() {
  if (this->responding_binary_sensor_ == nullptr)
    return;
  // Treat the ChargePoint as gone after roughly three missed replies.
  const uint32_t stale_ms =
      std::max<uint32_t>(3 * this->get_update_interval(), 3UL * this->min_interval_s_ * 1000UL);
  const bool responding = this->has_reply_ && millis() - this->last_reply_ms_ <= stale_ms;
  this->responding_binary_sensor_->publish_state(responding);
}

void EVBoxSmartGrid::dump_config() {
  ESP_LOGCONFIG(TAG, "EVBox SmartGrid module:");
  ESP_LOGW(TAG, "Experimental and untested on real chargers; use at your own risk");
  ESP_LOGCONFIG(TAG, "  Max current: %.1f A", this->max_current_);
  ESP_LOGCONFIG(TAG, "  Fallback current: %.1f A", this->fallback_current_);
  ESP_LOGCONFIG(TAG, "  Keep-alive timeout: %u s", static_cast<unsigned>(this->keepalive_timeout_s_));
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Current L1", this->current_sensors_[0]);
  LOG_SENSOR("  ", "Current L2", this->current_sensors_[1]);
  LOG_SENSOR("  ", "Current L3", this->current_sensors_[2]);
  LOG_SENSOR("  ", "Power factor L1", this->power_factor_sensors_[0]);
  LOG_SENSOR("  ", "Power factor L2", this->power_factor_sensors_[1]);
  LOG_SENSOR("  ", "Power factor L3", this->power_factor_sensors_[2]);
  LOG_SENSOR("  ", "Energy", this->energy_sensor_);
  LOG_SENSOR("  ", "Minimum current", this->minimum_current_sensor_);
  LOG_SENSOR("  ", "Connection max current", this->connection_max_current_sensor_);
  LOG_SENSOR("  ", "Minimum interval", this->minimum_interval_sensor_);
  LOG_BINARY_SENSOR("  ", "Responding", this->responding_binary_sensor_);
  this->check_uart_settings(38400);
}

}  // namespace evbox_smartgrid
}  // namespace esphome
