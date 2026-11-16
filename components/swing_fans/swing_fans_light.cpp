#include "swing_fans_light.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace swing_fans {

static const char *const TAG = "swing_fans.light";

light::LightTraits SwingFansLight::get_traits() {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    return traits;
}

int SwingFansLight::compute_level(light::LightState *state) {
    bool is_on = false;
    state->current_values_as_binary(&is_on);
    if (!is_on) {
        return 0;
    }
    float brightness = 0.0f;
    state->current_values_as_brightness(&brightness);
    int level = static_cast<int>(std::lround(brightness * 6.0f));
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    return level;
}

void SwingFansLight::update_state(light::LightState *state) {
    int target_level = compute_level(state);
    if (target_level > 0 && !state->is_transformer_active()) {
        float snapped = state->gamma_uncorrect_lut(static_cast<float>(target_level) / 6.0f);
        if (state->remote_values == state->current_values) {
            state->remote_values.set_brightness(snapped);
        }
        state->current_values.set_brightness(snapped);
    }
    if (this->current_level_ == -1) {
        this->current_level_ = target_level;
        ESP_LOGD(TAG, "Restored initial light level %d for fan '%s'.", this->current_level_, this->fan_name_.c_str());
        return;
    }
    this->from_rf_ = this->hub_ != nullptr && this->hub_->is_receiving_rf();
}

void SwingFansLight::write_state(light::LightState *state) {
    if (!this->hub_) {
        ESP_LOGE(TAG, "Cannot control light for fan '%s', hub is not linked!", this->fan_name_.c_str());
        return;
    }

    const bool is_repeat = this->from_rf_;
    this->from_rf_ = false;

    int target_level = compute_level(state);

    // The first write_state call happens on boot when LightState::setup() restores state.
    if (this->current_level_ == -1) {
        this->current_level_ = target_level;
        ESP_LOGD(TAG, "Restored initial light level %d for fan '%s'.", target_level, this->fan_name_.c_str());
        return;
    }

    if (target_level != this->current_level_) {
        if (target_level == 0) {
            ESP_LOGD(TAG, "Turning OFF light for fan '%s'", this->fan_name_.c_str());
            this->hub_->send_command(this->fan_name_, "light_off", is_repeat);
        } else {
            ESP_LOGD(TAG, "Setting light for fan '%s' to level %d", this->fan_name_.c_str(), target_level);
            this->hub_->send_command(this->fan_name_, "light_" + std::to_string(target_level), is_repeat);
        }
        this->current_level_ = target_level;
    }

    if (target_level > 0 && !state->is_transformer_active() &&
        state->remote_values.is_on() && state->remote_values == state->current_values) {
        const float snapped = state->gamma_uncorrect_lut(static_cast<float>(target_level) / 6.0f);
        if (std::fabs(state->remote_values.get_brightness() - snapped) > 0.0001f) {
            // Keep Home Assistant's brightness in sync after a transition completes.
            state->make_call()
                .set_state(true)
                .set_brightness(snapped)
                .set_transition_length_if_supported(0)
                .perform();
        }
    }
}

} // namespace swing_fans
} // namespace esphome
