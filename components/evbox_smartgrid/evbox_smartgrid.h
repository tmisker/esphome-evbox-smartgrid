#pragma once

#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {
namespace evbox_smartgrid {

/// Acts as the external "SmartGrid module" (bus address 0xA0) from EVBox Protocol Max v4.
///
/// Every update interval it sends command 69 (maximum phase currents) to the ChargePoint module
/// (0x80) on the charger's third-party RS-485 bus, and publishes the values from the reply. When
/// the frames stop, the ChargePoint applies the fallback current once the timeout has expired.
class EVBoxSmartGrid : public PollingComponent, public uart::UARTDevice {
 public:
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  /// Maximum current per phase in ampere. A changed value is sent right away.
  void set_max_current(float amps);
  float get_max_current() const { return this->max_current_; }

  /// Current per phase the ChargePoint applies when no frame arrives within the timeout.
  void set_fallback_current(float amps) { this->fallback_current_ = amps; }
  void set_keepalive_timeout(uint16_t seconds) { this->keepalive_timeout_s_ = seconds; }

  /// Stop sending frames; the ChargePoint reverts to the fallback current after the timeout.
  void set_keepalive_enabled(bool enabled);
  bool is_keepalive_enabled() const { return this->keepalive_enabled_; }

  void set_current_sensor(uint8_t phase, sensor::Sensor *s) { this->current_sensors_[phase] = s; }
  void set_power_factor_sensor(uint8_t phase, sensor::Sensor *s) { this->power_factor_sensors_[phase] = s; }
  void set_energy_sensor(sensor::Sensor *s) { this->energy_sensor_ = s; }
  void set_minimum_current_sensor(sensor::Sensor *s) { this->minimum_current_sensor_ = s; }
  void set_connection_max_current_sensor(sensor::Sensor *s) { this->connection_max_current_sensor_ = s; }
  void set_minimum_interval_sensor(sensor::Sensor *s) { this->minimum_interval_sensor_ = s; }
  void set_responding_binary_sensor(binary_sensor::BinarySensor *s) { this->responding_binary_sensor_ = s; }

 protected:
  void request_send_();
  void send_max_currents_();
  void handle_frame_(const std::string &frame);
  void handle_max_currents_reply_(const std::string &data);
  void publish_responding_();

  float max_current_{16.0f};
  float fallback_current_{16.0f};
  uint16_t keepalive_timeout_s_{60};
  bool keepalive_enabled_{true};

  std::string rx_frame_;
  bool receiving_{false};
  uint32_t last_bus_activity_ms_{0};

  bool send_pending_{false};
  uint32_t send_requested_ms_{0};
  uint32_t send_delay_ms_{0};
  bool has_sent_{false};
  uint32_t last_send_ms_{0};

  bool has_reply_{false};
  uint32_t last_reply_ms_{0};
  uint16_t min_interval_s_{0};
  bool min_interval_logged_{false};

  sensor::Sensor *current_sensors_[3]{nullptr, nullptr, nullptr};
  sensor::Sensor *power_factor_sensors_[3]{nullptr, nullptr, nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *minimum_current_sensor_{nullptr};
  sensor::Sensor *connection_max_current_sensor_{nullptr};
  sensor::Sensor *minimum_interval_sensor_{nullptr};
  binary_sensor::BinarySensor *responding_binary_sensor_{nullptr};
};

}  // namespace evbox_smartgrid
}  // namespace esphome
