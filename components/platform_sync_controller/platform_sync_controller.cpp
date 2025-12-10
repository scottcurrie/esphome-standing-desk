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
  ESP_LOGI(TAG, "  Communication timeout: %d ms", comm_timeout_);
  ESP_LOGI(TAG, "  Control loop interval: %d ms", control_loop_interval_);
}

void PlatformSyncController::dump_config() {
  ESP_LOGCONFIG(TAG, "Platform Sync Controller:");
  ESP_LOGCONFIG(TAG, "  Number of desks: %d", num_desks_);
  ESP_LOGCONFIG(TAG, "  Pause threshold: %.2f cm", pause_threshold_);
  ESP_LOGCONFIG(TAG, "  Resume threshold: %.2f cm", resume_threshold_);
  ESP_LOGCONFIG(TAG, "  Emergency threshold: %.2f cm", emergency_threshold_);
  ESP_LOGCONFIG(TAG, "  Communication timeout: %d ms", comm_timeout_);
  ESP_LOGCONFIG(TAG, "  Control loop interval: %d ms", control_loop_interval_);
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

  // Step 2: Check for communication failures
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

  // Step 5: Throttle fast desks (pause if ahead of slowest by pause_threshold)
  for (uint8_t i = 1; i <= num_desks_; i++) {
    if (desk_state_[i - 1] == DeskState::MOVING) {
      float desk_height = heights_[i - 1];
      bool should_pause = false;

      if (direction_ == Direction::UP) {
        // When moving up, pause desks that are higher than min + threshold
        should_pause = desk_height > min_height + pause_threshold_;
      } else if (direction_ == Direction::DOWN) {
        // When moving down, pause desks that are lower than max - threshold
        should_pause = desk_height < max_height - pause_threshold_;
      }

      if (should_pause) {
        ESP_LOGD(TAG, "Pausing desk %d (height %.2f, min %.2f, max %.2f)",
                 i, desk_height, min_height, max_height);
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
        should_resume = desk_height <= min_height + resume_threshold_;
      } else if (direction_ == Direction::DOWN) {
        // Resume when within resume_threshold of maximum
        should_resume = desk_height >= max_height - resume_threshold_;
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

  // Step 7: Check completion (for move_to_height)
  if (platform_state_ == PlatformState::MOVING_TO_HEIGHT) {
    float platform_height = get_platform_height();
    float target_tolerance = 0.3f;  // Complete when within 0.3cm of target

    bool reached_target = false;
    if (direction_ == Direction::UP) {
      reached_target = platform_height >= target_height_ - target_tolerance;
    } else if (direction_ == Direction::DOWN) {
      reached_target = platform_height <= target_height_ + target_tolerance;
    }

    if (reached_target) {
      ESP_LOGI(TAG, "Target height %.2f reached (current %.2f)", target_height_, platform_height);
      stop();
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

  if (!all_desks_responding()) {
    ESP_LOGW(TAG, "Cannot move: not all desks responding");
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

  if (!all_desks_responding()) {
    ESP_LOGW(TAG, "Cannot move: not all desks responding");
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

  // Broadcast stop to all desks immediately
  broadcast_command("*S");

  // Stop local desk if applicable
  stop_all_desks();

  // Set error state
  platform_state_ = PlatformState::ERROR;
  direction_ = Direction::NONE;
  strncpy(error_message_, reason, sizeof(error_message_) - 1);
  error_message_[sizeof(error_message_) - 1] = '\0';
}

void PlatformSyncController::preset(uint8_t num) {
  if (platform_state_ == PlatformState::ERROR) {
    ESP_LOGW(TAG, "Cannot move: platform in error state. Clear error first.");
    return;
  }

  if (!all_desks_responding()) {
    ESP_LOGW(TAG, "Cannot move: not all desks responding");
    return;
  }

  if (num < 1 || num > 4) {
    ESP_LOGW(TAG, "Invalid preset number: %d (must be 1-4)", num);
    return;
  }

  ESP_LOGI(TAG, "Moving platform to preset %d", num);

  // Send preset command to all desks
  char cmd[4];
  snprintf(cmd, sizeof(cmd), "*P%d", num);
  broadcast_command(cmd);

  // We don't know the target height for presets, so we can't do sync control
  // Just set all desks to moving and monitor for errors
  for (uint8_t i = 1; i <= num_desks_; i++) {
    desk_state_[i - 1] = DeskState::MOVING;
  }
  platform_state_ = PlatformState::MOVING_UP;  // Arbitrary direction for presets
  direction_ = Direction::UP;
}

void PlatformSyncController::move_to_height(float height) {
  if (platform_state_ == PlatformState::ERROR) {
    ESP_LOGW(TAG, "Cannot move: platform in error state. Clear error first.");
    return;
  }

  if (!all_desks_responding()) {
    ESP_LOGW(TAG, "Cannot move: not all desks responding");
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

  ESP_LOGD(TAG, "Sending to desk %d: %s", desk_id, cmd);

  // TODO: Implement ESP-NOW sending to specific desk
  // This requires the ESP-NOW component and peer management
  // For now, just log the command
}

void PlatformSyncController::broadcast_command(const char *cmd) {
  ESP_LOGD(TAG, "Broadcasting: %s", cmd);

  // TODO: Implement ESP-NOW broadcast
  // This requires the ESP-NOW component
  // For now, just log the command
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
