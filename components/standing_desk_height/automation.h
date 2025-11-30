#pragma once

#include "standing_desk_height.h"
#include "esphome/core/automation.h"

namespace esphome {
namespace standing_desk_height {

template<typename... Ts> class DetectDecoderAction : public Action<Ts...> {
 public:
  explicit DetectDecoderAction(StandingDeskHeightSensor *a_sdh) : sdh_(a_sdh) {}

  void play(const Ts &...x) override { this->sdh_->start_decoder_detection(); }

 protected:
  StandingDeskHeightSensor *sdh_;
};

template<typename... Ts> class MoveUpAction : public Action<Ts...> {
 public:
  explicit MoveUpAction(StandingDeskHeightSensor *sdh) : sdh_(sdh) {}

  void play(const Ts &...x) override { this->sdh_->move_up(); }

 protected:
  StandingDeskHeightSensor *sdh_;
};

template<typename... Ts> class MoveDownAction : public Action<Ts...> {
 public:
  explicit MoveDownAction(StandingDeskHeightSensor *sdh) : sdh_(sdh) {}

  void play(const Ts &...x) override { this->sdh_->move_down(); }

 protected:
  StandingDeskHeightSensor *sdh_;
};

template<typename... Ts> class StopAction : public Action<Ts...> {
 public:
  explicit StopAction(StandingDeskHeightSensor *sdh) : sdh_(sdh) {}

  void play(const Ts &...x) override { this->sdh_->stop(); }

 protected:
  StandingDeskHeightSensor *sdh_;
};

template<typename... Ts> class PresetAction : public Action<Ts...> {
 public:
  explicit PresetAction(StandingDeskHeightSensor *sdh) : sdh_(sdh) {}

  TEMPLATABLE_VALUE(uint8_t, preset)

  void play(const Ts &...x) override { this->sdh_->preset(this->preset_.value(x...)); }

 protected:
  StandingDeskHeightSensor *sdh_;
};

}
}