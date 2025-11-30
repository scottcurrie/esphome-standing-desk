#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

#include "decoder.h"
#include "jarvis_decoder.h"
#include "uplift_decoder.h"
#include "omnidesk_decoder.h"
#include "wn17cm3_decoder.h"

#include "decoder_variant.h"

namespace esphome {
namespace standing_desk_height {

class StandingDeskHeightSensor : public PollingComponent, public uart::UARTDevice, public sensor::Sensor
{
public:
  void set_decoder_variant(DecoderVariant decoder_variant);
  void start_decoder_detection();

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_last_read();

  // UART control methods (for WN17CM3)
  void move_up();
  void move_down();
  void stop();
  void preset(uint8_t num);
  bool supports_uart_control();

protected:
  Decoder* decoder;
  DecoderVariant decoder_variant = DECODER_VARIANT_UNKNOWN;

  float last_read = -1;
  float last_published = -1;

  bool is_detecting = false;
  uint32_t started_detecting_at = 0;

  void try_next_decoder();
  Wn17cm3Decoder* get_wn17cm3_decoder();
};

}
}
