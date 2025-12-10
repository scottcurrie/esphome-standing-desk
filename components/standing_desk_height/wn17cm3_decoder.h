#pragma once

#include <stdint.h>
#include "decoder.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace standing_desk_height {

// Decoder for Hangzhou Winner WN17CM3 (and WN17CM2) standing desk controllers.
// Protocol: ASCII text over UART at 19200 baud
// Message format: :command checksum;
// Example: :D71.10B; (Display 71.1cm, checksum 0x0B)
class Wn17cm3Decoder : public Decoder {
 public:
  Wn17cm3Decoder() {}
  ~Wn17cm3Decoder() {}

  bool put(uint8_t b) override;
  float decode() override;

  // UART control for sending commands
  void set_uart(uart::UARTDevice *uart) { this->uart_ = uart; }

  // Command sending methods
  void send_key_pressed(const char *key);
  void send_key_released(const char *key);
  void send_ack();

 protected:
  uart::UARTDevice *uart_{nullptr};

  // Line buffer for ASCII protocol (accumulate between ':' and ';')
  static const size_t BUF_SIZE = 32;
  char buf_[BUF_SIZE];
  size_t buf_len_ = 0;
  bool in_message_ = false;

  float last_height_ = -1;

  void reset();
  uint8_t calculate_checksum(const char *data, size_t len);
  bool process_message();
  bool parse_display_command();
  uint8_t hex_char_to_nibble(char c);
  void send_command(const char *cmd);
};

}  // namespace standing_desk_height
}  // namespace esphome
