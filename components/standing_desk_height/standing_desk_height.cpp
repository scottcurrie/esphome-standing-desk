#include "standing_desk_height.h"

namespace esphome {
namespace standing_desk_height {

static const char *const TAG = "standing_desk_height";

void StandingDeskHeightSensor::set_decoder_variant(DecoderVariant decoder_variant) {
  if (this->decoder != nullptr) {
    delete this->decoder;
    this->decoder = nullptr;
  }

  this->decoder_variant = decoder_variant;
  switch (decoder_variant) {
    case DECODER_VARIANT_JARVIS:
      this->decoder = new JarvisDecoder();
      break;
    case DECODER_VARIANT_UPLIFT:
      this->decoder = new UpliftDecoder();
      break;
    case DECODER_VARIANT_OMNIDESK:
      this->decoder = new OmnideskDecoder();
      break;
    case DECODER_VARIANT_WN17CM3: {
      auto *dec = new Wn17cm3Decoder();
      dec->set_uart(this);
      this->decoder = dec;
      break;
    }
    case DECODER_VARIANT_UNKNOWN:
      this->decoder = nullptr;
      return;
    default:
      ESP_LOGE(TAG, "Unknown decoder variant %d", (uint8_t) decoder_variant);
      this->decoder = nullptr;
      return;
  }
}

void StandingDeskHeightSensor::start_decoder_detection() {
  ESP_LOGI(TAG, "Starting decoder detection");

  this->decoder_variant = DECODER_VARIANT_UNKNOWN;
  this->try_next_decoder();
}

void StandingDeskHeightSensor::try_next_decoder() {
  if (this->decoder_variant == DECODER_VARIANT_COUNT - 1) {
    ESP_LOGW(TAG, "No valid decoder found. Please make sure your desk is reporting the height and you can see it on the keypad");

    delete this->decoder;
    this->decoder = nullptr;

    this->decoder_variant = DECODER_VARIANT_UNKNOWN;
    this->is_detecting = false;
    return;
  }

  this->set_decoder_variant((DecoderVariant) (this->decoder_variant + 1));

  const LogString *variant_s = decoder_variant_to_string(this->decoder_variant);
  ESP_LOGD(TAG, "Attempting next decoder variant: %s", LOG_STR_ARG(variant_s));

  this->last_read = -1;
  this->started_detecting_at = millis();
  this->is_detecting = true;
}

void StandingDeskHeightSensor::setup() {
  if (this->decoder_variant == DECODER_VARIANT_UNKNOWN) {
    ESP_LOGD(TAG, "Decoder variant was not set in config; using decoder detection");
    this->start_decoder_detection();
  } else {
    const LogString *variant_s = decoder_variant_to_string(this->decoder_variant);
    ESP_LOGD(TAG, "Using hardcoded decoder variant %s", LOG_STR_ARG(variant_s));
  }

  // A real WN17CM3 handset announces itself on connect; do the same
  if (auto *dec = this->get_wn17cm3_decoder()) {
    dec->send_boot_banner();
  }
}

void StandingDeskHeightSensor::loop() {
  while (this->available() > 0)
  {
    uint8_t byte;
    this->read_byte(&byte);

    ESP_LOGVV(TAG, "Reading byte: %d", byte);

    if (this->decoder != nullptr && this->decoder->put(byte)) {
      float height = this->decoder->decode();
      ESP_LOGVV(TAG, "Got desk height: %f", height);
      this->last_read = height;
      this->last_frame_at_ = millis();
    }
  }

  if (this->is_detecting) {
    if (this->last_read != -1) {
      this->is_detecting = false;

      const LogString *variant_s = decoder_variant_to_string(this->decoder_variant);
      ESP_LOGI(TAG, "Decoder detection complete. Correct decoder variant: %s", LOG_STR_ARG(variant_s));
      ESP_LOGI(TAG, "If you want to make this change permanent, add the following to this sensor's configuration:");
      ESP_LOGI(TAG, "  variant: %s", LOG_STR_ARG(variant_s));
    } else if (millis() - this->started_detecting_at > 1000) {
      const LogString *variant_s = decoder_variant_to_string(this->decoder_variant);
      ESP_LOGD(TAG, "Decoder %s does not appear to work; trying next decoder", LOG_STR_ARG(variant_s));
      this->try_next_decoder();
    }
  }
}

void StandingDeskHeightSensor::update() {
  if (this->last_read > 0 && this->last_read != this->last_published) {
    publish_state(this->last_read);
    this->last_published = this->last_read;
  }
}

void StandingDeskHeightSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "Standing Desk Height:");
  LOG_SENSOR("  ", "Height Sensor", this);
  LOG_UPDATE_INTERVAL(this);

  const LogString *variant_s = decoder_variant_to_string(this->decoder_variant);
  ESP_LOGCONFIG(TAG, "  Decoder Variant: %s", LOG_STR_ARG(variant_s));
}

float StandingDeskHeightSensor::get_last_read() {
  return this->last_read;
}

Wn17cm3Decoder* StandingDeskHeightSensor::get_wn17cm3_decoder() {
  if (this->decoder_variant == DECODER_VARIANT_WN17CM3) {
    return static_cast<Wn17cm3Decoder*>(this->decoder);
  }
  return nullptr;
}

bool StandingDeskHeightSensor::supports_uart_control() {
  return this->decoder_variant == DECODER_VARIANT_WN17CM3;
}

void StandingDeskHeightSensor::move_up() {
  if (auto *dec = get_wn17cm3_decoder()) {
    dec->send_key_pressed("UA");
  }
}

void StandingDeskHeightSensor::move_down() {
  if (auto *dec = get_wn17cm3_decoder()) {
    dec->send_key_pressed("DA");
  }
}

void StandingDeskHeightSensor::stop() {
  if (auto *dec = get_wn17cm3_decoder()) {
    dec->send_key_released("UA");
    dec->send_key_released("DA");
    // Also release the memory keys in case a preset key is logically held.
    // NOTE: do not rely on this to abort an in-flight preset move -- that
    // likely needs a brief key press+release tap (unverified on hardware).
    dec->send_key_released(" 1");
    dec->send_key_released(" 2");
    dec->send_key_released(" 3");
    dec->send_key_released(" 4");
  }
}

void StandingDeskHeightSensor::preset(uint8_t num) {
  if (auto *dec = get_wn17cm3_decoder()) {
    // Keys are " 1", " 2", " 3", " 4" (space prefix)
    static const char *keys[] = {" 1", " 2", " 3", " 4"};
    if (num >= 1 && num <= 4) {
      const char *key = keys[num - 1];
      dec->send_key_pressed(key);
      // Release the key ~100ms later, mirroring a physical tap; a press with
      // no matching release leaves the key logically held forever
      this->set_timeout("preset_key_release", 100, [this, key]() {
        if (auto *d = this->get_wn17cm3_decoder()) {
          d->send_key_released(key);
        }
      });
    }
  }
}

}
}
