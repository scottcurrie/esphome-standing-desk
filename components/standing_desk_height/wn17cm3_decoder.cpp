#include <cstring>
#include <cstdlib>
#include "wn17cm3_decoder.h"
#include "esphome/core/log.h"

namespace esphome {
namespace standing_desk_height {

static const char *const TAG = "wn17cm3_decoder";

void Wn17cm3Decoder::reset() {
  buf_len_ = 0;
  in_message_ = false;
}

uint8_t Wn17cm3Decoder::hex_char_to_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0xFF;  // Invalid
}

uint8_t Wn17cm3Decoder::calculate_checksum(const char *data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum += static_cast<uint8_t>(data[i]);
  }
  return sum;  // Lower byte (uint8_t wraps automatically)
}

bool Wn17cm3Decoder::put(uint8_t b) {
  char c = static_cast<char>(b);

  // Start of message
  if (c == ':') {
    reset();
    in_message_ = true;
    return false;
  }

  // Not in a message, ignore
  if (!in_message_) {
    return false;
  }

  // End of message
  if (c == ';') {
    in_message_ = false;
    return process_message();
  }

  // Accumulate into buffer
  if (buf_len_ < BUF_SIZE - 1) {
    buf_[buf_len_++] = c;
  } else {
    // Buffer overflow, reset
    ESP_LOGW(TAG, "Message too long, discarding");
    reset();
  }

  return false;
}

bool Wn17cm3Decoder::process_message() {
  // Minimum: 1 command char + 2 checksum chars = 3
  if (buf_len_ < 3) {
    ESP_LOGV(TAG, "Message too short: %zu bytes", buf_len_);
    return false;
  }

  // Null-terminate for string operations
  buf_[buf_len_] = '\0';

  // Last 2 chars are hex checksum
  uint8_t high = hex_char_to_nibble(buf_[buf_len_ - 2]);
  uint8_t low = hex_char_to_nibble(buf_[buf_len_ - 1]);

  if (high == 0xFF || low == 0xFF) {
    ESP_LOGW(TAG, "Invalid checksum hex chars in message: %s", buf_);
    return false;
  }

  uint8_t received_checksum = (high << 4) | low;
  uint8_t expected_checksum = calculate_checksum(buf_, buf_len_ - 2);

  if (received_checksum != expected_checksum) {
    ESP_LOGW(TAG, "Checksum mismatch: got 0x%02X, expected 0x%02X for message: %s",
             received_checksum, expected_checksum, buf_);
    return false;
  }

  ESP_LOGV(TAG, "Valid message: %s", buf_);

  // Parse command (first char after checksum validation)
  if (buf_[0] == 'D') {
    return parse_display_command();
  }

  // Other commands (A=ack, K=key, R=request, etc.) - ignore for now
  return false;
}

bool Wn17cm3Decoder::parse_display_command() {
  // Display command format: D<height>[,F600]
  // Examples: "D71.1" or "D100.5" or "D73.0,F600" (flashing mode)
  // The checksum chars have already been validated and are at the end

  // Start after 'D', end before checksum (2 chars)
  size_t cmd_len = buf_len_ - 2;  // Exclude checksum
  if (cmd_len < 2) {  // At least "D" + one digit
    return false;
  }

  // Find the height value (skip 'D', stop at ',' or end of command)
  char height_str[16];
  size_t height_len = 0;

  for (size_t i = 1; i < cmd_len && height_len < sizeof(height_str) - 1; i++) {
    char c = buf_[i];
    if (c == ',') break;  // Stop at ,F600 suffix
    height_str[height_len++] = c;
  }
  height_str[height_len] = '\0';

  if (height_len == 0) {
    // Empty display command (turn off display)
    return false;
  }

  // Parse height as float
  char *endptr;
  float height = strtof(height_str, &endptr);

  if (endptr == height_str || *endptr != '\0') {
    ESP_LOGW(TAG, "Failed to parse height from: %s", height_str);
    return false;
  }

  // Sanity check (reasonable desk height range in cm)
  if (height < 20.0 || height > 150.0) {
    ESP_LOGV(TAG, "Height out of expected range: %.1f", height);
    return false;
  }

  last_height_ = height;
  ESP_LOGD(TAG, "Decoded height: %.1f cm", height);
  send_ack();  // Acknowledge the display command (required by protocol)
  return true;
}

float Wn17cm3Decoder::decode() {
  return last_height_;
}

void Wn17cm3Decoder::send_command(const char *cmd) {
  if (this->uart_ == nullptr) {
    ESP_LOGW(TAG, "Cannot send command: UART not configured");
    return;
  }

  size_t len = strlen(cmd);
  uint8_t checksum = calculate_checksum(cmd, len);

  // Format: :command<checksum>;
  // Checksum is 2-char uppercase hex
  char msg[BUF_SIZE];
  snprintf(msg, sizeof(msg), ":%s%02X;", cmd, checksum);

  ESP_LOGD(TAG, "Sending command: %s", msg);
  this->uart_->write_str(msg);
}

void Wn17cm3Decoder::send_key_pressed(const char *key) {
  // Format: K<key>M (M = pressed)
  char cmd[8];
  snprintf(cmd, sizeof(cmd), "K%sM", key);
  send_command(cmd);
}

void Wn17cm3Decoder::send_key_released(const char *key) {
  // Format: K<key>B (B = released)
  char cmd[8];
  snprintf(cmd, sizeof(cmd), "K%sB", key);
  send_command(cmd);
}

void Wn17cm3Decoder::send_ack() {
  // Send ACK response to controller (required by protocol)
  send_command("A");
}

}  // namespace standing_desk_height
}  // namespace esphome
