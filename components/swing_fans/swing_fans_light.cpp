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

void SwingFansLight::write_state(light::LightState *state) {
    if (!this->hub_) {
        ESP_LOGE(TAG, "Cannot control light for fan '%s', hub is not linked!", this->fan_name_.c_str());
        return;
    }

    bool is_on = false;
    state->current_values_as_binary(&is_on);

    int target_level = 0;
    if (is_on) {
        float brightness = 0.0f;
        state->current_values_as_brightness(&brightness);
        target_level = static_cast<int>(std::lround(brightness * 6.0f));
        if (target_level < 1) target_level = 1;
        if (target_level > 6) target_level = 6;
    }

    // The first write_state call happens on boot when LightState::setup() restores state.
    if (this->current_level_ == -1) {
        this->current_level_ = target_level;
        ESP_LOGD(TAG, "Restored initial light level %d for fan '%s'.", target_level, this->fan_name_.c_str());
        return;
    }

    if (target_level != this->current_level_) {
        if (target_level == 0) {
            ESP_LOGD(TAG, "Turning OFF light for fan '%s'", this->fan_name_.c_str());
            this->hub_->send_command(this->fan_name_, "light_off");
        } else {
            ESP_LOGD(TAG, "Setting light for fan '%s' to level %d", this->fan_name_.c_str(), target_level);
            this->hub_->send_command(this->fan_name_, "light_" + std::to_string(target_level));
        }
        this->current_level_ = target_level;
    }

    if (is_on && !state->is_transformer_active() &&
        std::fabs(state->remote_values.get_brightness() - target_level / 6.0f) > 0.0001f) {
        // Keep Home Assistant's brightness in sync with the six supported RF levels.
        state->make_call().set_state(true).set_brightness(target_level / 6.0f)
            .set_transition_length_if_supported(0).perform();
    }
}

} // namespace swing_fans
} // namespace esphome
