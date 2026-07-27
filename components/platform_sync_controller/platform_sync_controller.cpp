#include "platform_sync_controller.h"
#include "esphome/core/log.h"

namespace esphome {
namespace platform_sync_controller {

static const char *const TAG = "platform_sync_controller";

void PlatformSyncController::setup() {
  ESP_LOGI(TAG, "Platform Sync Controller initializing...");
  ESP_LOGI(TAG, "  Number of desks: %d", num_desks_);
  ESP_LOGI(TAG, "  Pause threshold: %.2f cm", pause_threshold_);
  ESP_LOGI(TAG, "  Resume threshold: %.2f cm", resume_threshold_);
  ESP_LOGI(TAG, "  Emergency threshold: %.2f cm", emergency_threshold_);
  ESP_LOGI(TAG, "  Target tolerance: %.2f cm", target_tolerance_);
  ESP_LOGI(TAG, "  Communication timeout: %d ms", comm_timeout_);
  ESP_LOGI(TAG, "  Control loop interval: %d ms", control_loop_interval_);
}

void PlatformSyncController::dump_config() {
  ESP_LOGCONFIG(TAG, "Platform Sync Controller:");
  ESP_LOGCONFIG(TAG, "  Number of desks: %d", num_desks_);
  ESP_LOGCONFIG(TAG, "  Pause threshold: %.2f cm", pause_threshold_);
  ESP_LOGCONFIG(TAG, "  Resume threshold: %.2f cm", resume_threshold_);
  ESP_LOGCONFIG(TAG, "  Emergency threshold: %.2f cm", emergency_threshold_);
  ESP_LOGCONFIG(TAG, "  Target tolerance: %.2f cm", target_tolerance_);
  ESP_LOGCONFIG(TAG, "  Communication timeout: %d ms", comm_timeout_);
  ESP_LOGCONFIG(TAG, "  Control loop interval: %d ms", control_loop_interval_);
  ESP_LOGCONFIG(TAG, "  Local desk sensor: %s", local_desk_sensor_ != nullptr ? "yes (desk 1)" : "no");
}

void PlatformSyncController::loop() {
  uint32_t now = millis();

  // Run control loop at configured interval
  if (now - last_control_loop_ >= control_loop_interval_) {
    last_control_loop_ = now;
    run_control_loop();
  }
}

void PlatformSyncController::run_control_loop() {
  // Skip if not moving or in error state
  if (platform_state_ == PlatformState::IDLE || platform_state_ == PlatformState::ERROR) {
    return;
  }

  uint32_t now = millis();

  // Step 2: Check for communication failures.
  // The WN17CM3 is silent at idle: D frames only start streaming once the
  // display session wakes up, so give the stream a short grace period after
  // movement starts before enforcing the strict comm timeout. The spread
  // check below still runs during the grace period using last-known heights.
  if (now - movement_started_at_ > MOVE_START_GRACE_MS) {
    for (uint8_t i = 1; i <= num_desks_; i++) {
      if (last_update_[i - 1] == 0) {
        // Desk hasn't reported yet - this is handled by startup check
        continue;
      }
      if (now - last_update_[i - 1] > comm_timeout_) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Desk %d communication lost (timeout %dms)", i, comm_timeout_);
        emergency_stop(msg);
        return;
      }
    }
  }

  // Step 3: Calculate spread
  float min_height = get_min_height();
  float max_height = get_max_height();
  float spread = max_height - min_height;

  // Step 4: Emergency check
  if (spread > emergency_threshold_) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Platform tilt detected: %.2fcm spread (threshold %.2fcm)",
             spread, emergency_threshold_);
    emergency_stop(msg);
    return;
  }

  // Reference heights for the pause/resume logic (steps 5-6). When moving to
  // a target, desks that already stopped at the target no longer move and
  // must not throttle the stragglers still chasing it, so use the min/max of
  // the still-active (MOVING/PAUSED) desks only. The emergency spread check
  // above intentionally keeps using ALL desks.
  float ref_min = min_height;
  float ref_max = max_height;
  if (platform_state_ == PlatformState::MOVING_TO_HEIGHT) {
    float active_min = 999999;
    float active_max = 0;
    bool found_active = false;
    for (uint8_t i = 1; i <= num_desks_; i++) {
      DeskState state = desk_state_[i - 1];
      if ((state == DeskState::MOVING || state == DeskState::PAUSED) && heights_[i - 1] > 0) {
        if (heights_[i - 1] < active_min) active_min = heights_[i - 1];
        if (heights_[i - 1] > active_max) active_max = heights_[i - 1];
        found_active = true;
      }
    }
    if (found_active) {
      ref_min = active_min;
      ref_max = active_max;
    }
  }

  // Step 5: Throttle fast desks (pause if ahead of slowest by pause_threshold)
  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (desk_state_[i - 1] == DeskState::MOVING) {
      float desk_height = heights_[i - 1];
      bool should_pause = false;

      if (direction_ == Direction::UP) {
        // When moving up, pause desks that are higher than min + threshold
        should_pause = desk_height > ref_min + pause_threshold_;
      } else if (direction_ == Direction::DOWN) {
        // When moving down, pause desks that are lower than max - threshold
        should_pause = desk_height < ref_max - pause_threshold_;
      }

      if (should_pause) {
        ESP_LOGD(TAG, "Pausing desk %d (height %.2f, min %.2f, max %.2f)",
                 i, desk_height, ref_min, ref_max);
        send_command_to_desk(i, "S");
        desk_state_[i - 1] = DeskState::PAUSED;
      }
    }
  }

  // Step 6: Resume paused desks when within resume_threshold of reference
  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (desk_state_[i - 1] == DeskState::PAUSED) {
      float desk_height = heights_[i - 1];
      bool should_resume = false;

      if (direction_ == Direction::UP) {
        // Resume when within resume_threshold of minimum
        should_resume = desk_height <= ref_min + resume_threshold_;
      } else if (direction_ == Direction::DOWN) {
        // Resume when within resume_threshold of maximum
        should_resume = desk_height >= ref_max - resume_threshold_;
      }

      if (should_resume) {
        ESP_LOGD(TAG, "Resuming desk %d (height %.2f)", i, desk_height);
        if (direction_ == Direction::UP) {
          send_command_to_desk(i, "U");
        } else {
          send_command_to_desk(i, "D");
        }
        desk_state_[i - 1] = DeskState::MOVING;
      }
    }
  }

  // Step 6.5: Keepalive - re-send the active direction to every MOVING desk.
  // The WN17CM3 only keeps moving while the key-pressed frame is re-sent
  // continuously (~100ms cadence; Dec 13 hardware finding), and the periodic
  // re-send also self-heals lost packets and feeds the slaves' dead-man
  // timers. Pause/stop remain transition-based ("S" above).
  if ((direction_ == Direction::UP || direction_ == Direction::DOWN) &&
      now - last_keepalive_ >= KEEPALIVE_INTERVAL_MS) {
    last_keepalive_ = now;
    const char *dir_cmd = (direction_ == Direction::UP) ? "U" : "D";
    for (uint8_t i = 1; i <= num_desks_; i++) {
      if (desk_state_[i - 1] == DeskState::MOVING) {
        send_command_to_desk(i, dir_cmd);
      }
    }
  }

  // Step 7: Check completion (for move_to_height).
  // Per-desk, direction-aware stops: each desk stops individually as it
  // reaches the target, so the platform cannot "arrive" tilted on an average.
  // One-sided tests (not fabs) so an overshooting desk still counts as done.
  if (platform_state_ == PlatformState::MOVING_TO_HEIGHT) {
    bool all_stopped = true;

    for (uint8_t i = 1; i <= num_desks_; i++) {
      DeskState state = desk_state_[i - 1];
      if (state != DeskState::MOVING && state != DeskState::PAUSED) {
        continue;
      }

      float desk_height = heights_[i - 1];
      bool at_target = false;
      if (direction_ == Direction::UP) {
        at_target = desk_height >= target_height_ - target_tolerance_;
      } else if (direction_ == Direction::DOWN) {
        at_target = desk_height <= target_height_ + target_tolerance_;
      }

      if (at_target) {
        ESP_LOGD(TAG, "Desk %d reached target %.2f (height %.2f); stopping it",
                 i, target_height_, desk_height);
        send_command_to_desk(i, "S");
        desk_state_[i - 1] = DeskState::STOPPED;
      } else {
        all_stopped = false;
      }
    }

    if (all_stopped) {
      ESP_LOGI(TAG, "Target height %.2f reached by all desks", target_height_);
      platform_state_ = PlatformState::IDLE;
      direction_ = Direction::NONE;
    }
  }
}

void PlatformSyncController::on_height_received(uint8_t desk_id, float height) {
  if (!is_valid_desk_id(desk_id)) {
    ESP_LOGW(TAG, "Received height from invalid desk ID %d", desk_id);
    return;
  }

  heights_[desk_id - 1] = height;
  last_update_[desk_id - 1] = millis();

  ESP_LOGV(TAG, "Desk %d height: %.2f cm", desk_id, height);
}

void PlatformSyncController::move_up() {
  if (platform_state_ == PlatformState::ERROR) {
    ESP_LOGW(TAG, "Cannot move: platform in error state. Clear error first.");
    return;
  }

  if (!all_desks_have_reported()) {
    ESP_LOGW(TAG, "Cannot move: not all desks have reported a recent height");
    return;
  }

  ESP_LOGI(TAG, "Starting platform movement UP");
  start_movement(Direction::UP);
  broadcast_command("U");
}

void PlatformSyncController::move_down() {
  if (platform_state_ == PlatformState::ERROR) {
    ESP_LOGW(TAG, "Cannot move: platform in error state. Clear error first.");
    return;
  }

  if (!all_desks_have_reported()) {
    ESP_LOGW(TAG, "Cannot move: not all desks have reported a recent height");
    return;
  }

  ESP_LOGI(TAG, "Starting platform movement DOWN");
  start_movement(Direction::DOWN);
  broadcast_command("D");
}

void PlatformSyncController::stop() {
  ESP_LOGI(TAG, "Stopping platform");
  stop_all_desks();
  platform_state_ = PlatformState::IDLE;
  direction_ = Direction::NONE;
}

void PlatformSyncController::emergency_stop(const char *reason) {
  ESP_LOGE(TAG, "EMERGENCY STOP: %s", reason);

  // Stop the directly attached desk first - this path involves no transport
  // and must work even if ESP-NOW is down
  if (local_desk_sensor_ != nullptr) {
    local_desk_sensor_->stop();
  }

  // Broadcast stop to all desks immediately
  broadcast_command("*S");

  // Belt and suspenders: ESP-NOW broadcast has no MAC-layer ACK/retry, so
  // also unicast a stop to every desk individually
  for (uint8_t i = 1; i <= num_desks_; i++) {
    send_command_to_desk(i, "S");
    desk_state_[i - 1] = DeskState::STOPPED;
  }

  // Set error state
  platform_state_ = PlatformState::ERROR;
  direction_ = Direction::NONE;
  strncpy(error_message_, reason, sizeof(error_message_) - 1);
  error_message_[sizeof(error_message_) - 1] = '\0';
}

void PlatformSyncController::preset(uint8_t num) {
  // Presets are unsafe for a rigid coupled platform: each WN17CM3 stores its
  // own preset heights and recalls them autonomously at its own speed, so the
  // desks cannot be kept synchronized, the sync loop cannot supervise the move
  // (no known target, no known direction), and it is unverified whether a stop
  // key event even cancels an in-flight preset recall. Refuse without
  // changing any state. Use move_to_height() instead - it computes direction
  // from target vs current height and the sync/throttle logic applies.
  ESP_LOGE(TAG,
           "Platform preset %d REFUSED: presets trigger autonomous per-desk moves that "
           "cannot be synchronized. Use move_to_height instead.",
           num);
}

void PlatformSyncController::move_to_height(float height) {
  if (platform_state_ == PlatformState::ERROR) {
    ESP_LOGW(TAG, "Cannot move: platform in error state. Clear error first.");
    return;
  }

  if (!all_desks_have_reported()) {
    ESP_LOGW(TAG, "Cannot move: not all desks have reported a recent height");
    return;
  }

  float current = get_platform_height();
  ESP_LOGI(TAG, "Moving platform from %.2f to %.2f cm", current, height);

  target_height_ = height;

  if (height > current) {
    start_movement(Direction::UP);
    broadcast_command("U");
    platform_state_ = PlatformState::MOVING_TO_HEIGHT;
  } else if (height < current) {
    start_movement(Direction::DOWN);
    broadcast_command("D");
    platform_state_ = PlatformState::MOVING_TO_HEIGHT;
  } else {
    // Already at target
    ESP_LOGI(TAG, "Already at target height");
  }
}

void PlatformSyncController::start_movement(Direction dir) {
  direction_ = dir;
  platform_state_ = (dir == Direction::UP) ? PlatformState::MOVING_UP : PlatformState::MOVING_DOWN;
  movement_started_at_ = millis();

  // Set all desks to moving state
  for (uint8_t i = 1; i <= num_desks_; i++) {
    desk_state_[i - 1] = DeskState::MOVING;
  }
}

void PlatformSyncController::stop_all_desks() {
  // Send stop to all desks
  broadcast_command("S");

  // Update state
  for (uint8_t i = 1; i <= num_desks_; i++) {
    desk_state_[i - 1] = DeskState::STOPPED;
  }
}

void PlatformSyncController::send_command_to_desk(uint8_t desk_id, const char *cmd) {
  if (!is_valid_desk_id(desk_id)) {
    return;
  }

  // VERBOSE: this is a hot path (keepalive re-sends at the control loop rate)
  ESP_LOGV(TAG, "Sending to desk %d: %s", desk_id, cmd);

  // The master's own desk is wired directly to this board: drive it over the
  // local UART instead of any transport
  if (desk_id == LOCAL_DESK_ID && local_desk_sensor_ != nullptr) {
    apply_local_command(cmd);
    return;
  }

  if (send_callback_) {
    send_callback_(desk_id, cmd);
    return;
  }

  // No transport wired up: the command is NOT delivered. Warn loudly (but
  // rate-limited, since the keepalive retries every control loop tick).
  uint32_t now = millis();
  if (now - last_no_transport_warn_ >= 1000) {
    last_no_transport_warn_ = now;
    ESP_LOGW(TAG, "No transport configured; command '%s' for desk %d NOT delivered", cmd, desk_id);
  }
}

void PlatformSyncController::broadcast_command(const char *cmd) {
  ESP_LOGV(TAG, "Broadcasting: %s", cmd);

  // Broadcasts also apply to the directly attached desk
  if (local_desk_sensor_ != nullptr) {
    apply_local_command(cmd);
  }

  if (broadcast_callback_) {
    broadcast_callback_(cmd);
    return;
  }

  // No broadcast transport: fall back to per-desk unicast delivery (strip the
  // '*' broadcast prefix - unicast commands are sent bare)
  const char *bare_cmd = (cmd[0] == '*') ? cmd + 1 : cmd;
  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (i == LOCAL_DESK_ID && local_desk_sensor_ != nullptr) {
      continue;  // Already applied locally above
    }
    send_command_to_desk(i, bare_cmd);
  }
}

void PlatformSyncController::apply_local_command(const char *cmd) {
  if (local_desk_sensor_ == nullptr) {
    return;
  }

  // Strip broadcast prefix
  if (cmd[0] == '*') {
    cmd++;
  }

  if (strcmp(cmd, "U") == 0) {
    local_desk_sensor_->move_up();
  } else if (strcmp(cmd, "D") == 0) {
    local_desk_sensor_->move_down();
  } else if (strcmp(cmd, "S") == 0) {
    local_desk_sensor_->stop();
  } else {
    ESP_LOGW(TAG, "Unhandled local desk command: %s", cmd);
  }
}

float PlatformSyncController::get_platform_height() const {
  // Return average height of all desks
  float sum = 0;
  uint8_t count = 0;

  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (heights_[i - 1] > 0) {
      sum += heights_[i - 1];
      count++;
    }
  }

  return (count > 0) ? (sum / count) : 0;
}

float PlatformSyncController::get_max_spread() const {
  float min_h = get_min_height();
  float max_h = get_max_height();
  return (min_h > 0 && max_h > 0) ? (max_h - min_h) : 0;
}

float PlatformSyncController::get_min_height() const {
  float min_h = 999999;
  bool found = false;

  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (heights_[i - 1] > 0) {
      if (heights_[i - 1] < min_h) {
        min_h = heights_[i - 1];
      }
      found = true;
    }
  }

  return found ? min_h : 0;
}

float PlatformSyncController::get_max_height() const {
  float max_h = 0;

  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (heights_[i - 1] > max_h) {
      max_h = heights_[i - 1];
    }
  }

  return max_h;
}

bool PlatformSyncController::all_desks_have_reported() const {
  uint32_t now = millis();

  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (last_update_[i - 1] == 0) {
      ESP_LOGW(TAG, "Desk %d has never reported a height", i);
      return false;
    }
    if (now - last_update_[i - 1] > PRE_MOVE_MAX_AGE_MS) {
      ESP_LOGW(TAG, "Desk %d height is stale (%d ms old, max %d ms) - refusing to start",
               i, now - last_update_[i - 1], PRE_MOVE_MAX_AGE_MS);
      return false;
    }
  }

  return true;
}

bool PlatformSyncController::all_desks_responding() const {
  uint32_t now = millis();

  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (last_update_[i - 1] == 0) {
      ESP_LOGW(TAG, "Desk %d has never reported", i);
      return false;
    }
    if (now - last_update_[i - 1] > comm_timeout_) {
      ESP_LOGW(TAG, "Desk %d last reported %d ms ago (timeout %d ms)",
               i, now - last_update_[i - 1], comm_timeout_);
      return false;
    }
  }

  return true;
}

DeskState PlatformSyncController::get_desk_state(uint8_t desk_id) const {
  if (!is_valid_desk_id(desk_id)) {
    return DeskState::STOPPED;
  }
  return desk_state_[desk_id - 1];
}

float PlatformSyncController::get_desk_height(uint8_t desk_id) const {
  if (!is_valid_desk_id(desk_id)) {
    return -1;
  }
  return heights_[desk_id - 1];
}

void PlatformSyncController::clear_error() {
  if (platform_state_ == PlatformState::ERROR) {
    ESP_LOGI(TAG, "Clearing error state");
    platform_state_ = PlatformState::IDLE;
    error_message_[0] = '\0';
  }
}

}  // namespace platform_sync_controller
}  // namespace esphome
