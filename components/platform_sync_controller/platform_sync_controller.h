#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/standing_desk_height/standing_desk_height.h"
#include <cstring>
#include <cmath>
#include <functional>

namespace esphome {
namespace platform_sync_controller {

// Maximum number of desks supported
static const uint8_t MAX_DESKS = 10;

// Desk state enumeration
enum class DeskState : uint8_t {
  STOPPED = 0,  // Desk is not moving
  MOVING = 1,   // Desk is moving (up or down)
  PAUSED = 2,   // Desk paused by sync controller (waiting for others to catch up)
};

// Platform state enumeration
enum class PlatformState : uint8_t {
  IDLE = 0,        // Platform not moving
  MOVING_UP = 1,   // Platform moving up
  MOVING_DOWN = 2, // Platform moving down
  MOVING_TO_HEIGHT = 3, // Moving to specific height
  ERROR = 4,       // Error state (emergency stopped)
};

// Movement direction
enum class Direction : uint8_t {
  NONE = 0,
  UP = 1,
  DOWN = 2,
};

/**
 * Platform Sync Controller
 *
 * Coordinates multiple standing desks to move in sync, maintaining
 * platform level within configurable tolerances.
 *
 * Control Loop Algorithm (runs at ~20Hz):
 * 1. Receive height updates from slaves (via ESP-NOW)
 * 2. Check for communication failures (timeout)
 * 3. Calculate spread (max_height - min_height)
 * 4. Emergency stop if spread > emergency_threshold
 * 5. Throttle fast desks (pause if ahead by pause_threshold)
 * 6. Resume paused desks when within resume_threshold of slowest
 * 7. Check completion (all desks within tolerance of target)
 */
class PlatformSyncController : public Component {
 public:
  PlatformSyncController() {
    // Initialize arrays
    for (uint8_t i = 0; i < MAX_DESKS; i++) {
      heights_[i] = -1.0f;
      last_update_[i] = 0;
      desk_state_[i] = DeskState::STOPPED;
    }
  }

  // ESPHome component methods
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Configuration setters
  void set_pause_threshold(float t) { pause_threshold_ = t; }
  void set_resume_threshold(float t) { resume_threshold_ = t; }
  void set_emergency_threshold(float t) { emergency_threshold_ = t; }
  void set_comm_timeout(uint32_t t) { comm_timeout_ = t; }
  void set_num_desks(uint8_t n) { num_desks_ = n; }
  void set_control_loop_interval(uint32_t ms) { control_loop_interval_ = ms; }
  void set_target_tolerance(float t) { target_tolerance_ = t; }

  // Directly attached desk (desk 1 on the master). When set, commands for
  // desk 1 are driven over the local UART instead of the transport callback,
  // and emergency_stop() always stops it even if the transport is down.
  void set_local_desk_sensor(standing_desk_height::StandingDeskHeightSensor *sensor) {
    local_desk_sensor_ = sensor;
  }

  // Transport hooks (e.g. ESP-NOW, wired from YAML lambdas). Without these,
  // commands for remote desks are dropped with a warning.
  void set_send_callback(std::function<void(uint8_t desk_id, const char *cmd)> cb) {
    send_callback_ = std::move(cb);
  }
  void set_broadcast_callback(std::function<void(const char *cmd)> cb) {
    broadcast_callback_ = std::move(cb);
  }

  // Control methods (called from Home Assistant or automation)
  void move_up();
  void move_down();
  void stop();
  void emergency_stop(const char *reason);
  void preset(uint8_t num);
  void move_to_height(float height);

  // Status getters
  float get_platform_height() const;
  float get_max_spread() const;
  float get_min_height() const;
  float get_max_height() const;
  bool is_moving() const { return platform_state_ != PlatformState::IDLE && platform_state_ != PlatformState::ERROR; }
  bool has_error() const { return platform_state_ == PlatformState::ERROR; }
  PlatformState get_platform_state() const { return platform_state_; }
  const char *get_error_message() const { return error_message_; }

  // Called when height data received from a slave desk
  void on_height_received(uint8_t desk_id, float height);

  // Check if all desks are reporting (for startup check)
  bool all_desks_responding() const;

  // Pre-move health check: every desk must have reported at least once and not
  // too long ago. Deliberately looser than the strict comm_timeout watchdog,
  // because the WN17CM3 is silent at idle (no D frames until a display session
  // is active), so 250ms-fresh data cannot exist before movement starts.
  bool all_desks_have_reported() const;

  // Get desk state for diagnostics
  DeskState get_desk_state(uint8_t desk_id) const;
  float get_desk_height(uint8_t desk_id) const;

  // Clear error state
  void clear_error();

 protected:
  // Configuration
  float pause_threshold_{0.3f};      // Pause desk if ahead by this much (cm)
  float resume_threshold_{0.2f};     // Resume when within this of slowest (cm)
  float emergency_threshold_{1.0f};  // Emergency stop if spread exceeds this (cm)
  float target_tolerance_{0.3f};     // Per-desk stop tolerance for move_to_height (cm)
  uint32_t comm_timeout_{250};       // Stop all if any desk stops reporting (ms)
  uint8_t num_desks_{5};             // Number of desks in platform
  uint32_t control_loop_interval_{50}; // Control loop interval (ms) - ~20Hz

  // Desk ID of the desk wired directly to this board
  static constexpr uint8_t LOCAL_DESK_ID = 1;
  // Grace period after movement starts before the strict comm_timeout watchdog
  // applies (the D-frame stream only starts once the display session wakes up)
  static constexpr uint32_t MOVE_START_GRACE_MS = 1000;
  // Maximum height-report age accepted by the pre-move health check
  static constexpr uint32_t PRE_MOVE_MAX_AGE_MS = 5000;
  // Cadence for re-sending the active direction to MOVING desks. 100ms is the
  // hardware-proven WN17CM3 re-send interval (Dec 13 finding: timing changes
  // caused start/stop stutter, so stay on the proven cadence)
  static constexpr uint32_t KEEPALIVE_INTERVAL_MS = 100;

  // State tracking
  float heights_[MAX_DESKS];         // Last known height of each desk (1-indexed: 1..num_desks)
  uint32_t last_update_[MAX_DESKS];  // Timestamp of last height report
  DeskState desk_state_[MAX_DESKS];  // State of each desk
  PlatformState platform_state_{PlatformState::IDLE};
  Direction direction_{Direction::NONE};
  float target_height_{0.0f};        // Target height for move_to_height
  char error_message_[64]{""};       // Error message for diagnostics

  // Timing
  uint32_t last_control_loop_{0};
  uint32_t movement_started_at_{0};
  uint32_t last_keepalive_{0};
  uint32_t last_no_transport_warn_{0};

  // Local desk + transport
  standing_desk_height::StandingDeskHeightSensor *local_desk_sensor_{nullptr};
  std::function<void(uint8_t desk_id, const char *cmd)> send_callback_{nullptr};
  std::function<void(const char *cmd)> broadcast_callback_{nullptr};

  // Internal methods
  void run_control_loop();
  void send_command_to_desk(uint8_t desk_id, const char *cmd);
  void broadcast_command(const char *cmd);
  void apply_local_command(const char *cmd);
  void start_movement(Direction dir);
  void stop_all_desks();

  // Helper to check if desk index is valid (1-indexed)
  bool is_valid_desk_id(uint8_t desk_id) const {
    return desk_id >= 1 && desk_id <= num_desks_;
  }
};

// ==================== Automation Actions ====================

template<typename... Ts>
class MoveUpAction : public Action<Ts...> {
 public:
  explicit MoveUpAction(PlatformSyncController *controller) : controller_(controller) {}

  void play(Ts... x) override { this->controller_->move_up(); }

 protected:
  PlatformSyncController *controller_;
};

template<typename... Ts>
class MoveDownAction : public Action<Ts...> {
 public:
  explicit MoveDownAction(PlatformSyncController *controller) : controller_(controller) {}

  void play(Ts... x) override { this->controller_->move_down(); }

 protected:
  PlatformSyncController *controller_;
};

template<typename... Ts>
class StopAction : public Action<Ts...> {
 public:
  explicit StopAction(PlatformSyncController *controller) : controller_(controller) {}

  void play(Ts... x) override { this->controller_->stop(); }

 protected:
  PlatformSyncController *controller_;
};

template<typename... Ts>
class EmergencyStopAction : public Action<Ts...> {
 public:
  explicit EmergencyStopAction(PlatformSyncController *controller) : controller_(controller) {}

  void play(Ts... x) override { this->controller_->emergency_stop("Manual emergency stop"); }

 protected:
  PlatformSyncController *controller_;
};

template<typename... Ts>
class PresetAction : public Action<Ts...> {
 public:
  explicit PresetAction(PlatformSyncController *controller) : controller_(controller) {}

  TEMPLATABLE_VALUE(uint8_t, preset)

  void play(Ts... x) override { this->controller_->preset(this->preset_.value(x...)); }

 protected:
  PlatformSyncController *controller_;
};

template<typename... Ts>
class MoveToHeightAction : public Action<Ts...> {
 public:
  explicit MoveToHeightAction(PlatformSyncController *controller) : controller_(controller) {}

  TEMPLATABLE_VALUE(float, height)

  void play(Ts... x) override { this->controller_->move_to_height(this->height_.value(x...)); }

 protected:
  PlatformSyncController *controller_;
};

}  // namespace platform_sync_controller
}  // namespace esphome
