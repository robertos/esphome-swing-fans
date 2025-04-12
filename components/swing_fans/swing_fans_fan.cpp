#include "swing_fans_fan.h"
#include "esphome/core/log.h"

namespace esphome {
namespace swing_fans {

static const char *const TAG = "swing_fans.fan";

void SwingFansFan::setup() {
    ESP_LOGCONFIG(TAG, "Setting up Swing Fan '%s'...", this->name_.c_str());
    auto restore = this->restore_state_();
    if (restore.has_value()) {
        restore->apply(*this);
        ESP_LOGD(TAG, "Restored state for fan '%s'.", this->name_.c_str());
    } else {
        ESP_LOGD(TAG, "Initializing state for fan '%s'.", this->name_.c_str());
        // Initialize state if not restored
        this->state = false;
        this->speed = 1; // Default speed if turned on
    }
}

void SwingFansFan::dump_config() {
    LOG_FAN(TAG, "Swing Fan", this);
    ESP_LOGCONFIG(TAG, "  Name: %s", this->name_.c_str());
    ESP_LOGCONFIG(TAG, "  Hub Linked: %s", YESNO(this->hub_ != nullptr));
}

fan::FanTraits SwingFansFan::get_traits() {
    auto traits = fan::FanTraits();
    traits.set_speed(true);
    traits.set_supported_speed_count(6);
    return traits;
}

void SwingFansFan::control(const fan::FanCall &call) {
    if (!this->hub_) {
        ESP_LOGE(TAG, "Cannot control fan '%s', hub is not linked!", this->name_.c_str());
        return;
    }

    bool state_updated = false;

    // State (On/Off) control
    if (call.get_state().has_value()) {
        bool new_state = *call.get_state();
        if (this->state != new_state) {
            if (new_state) { // Turning ON
                // Use requested speed, or current speed if >0, otherwise default to 1
                int target_speed = call.get_speed().value_or(this->speed > 0 ? this->speed : 1);
                if (target_speed < 1) target_speed = 1;
                if (target_speed > 6) target_speed = 6;
                ESP_LOGD(TAG, "Turning ON fan '%s' to speed %d", this->name_.c_str(), target_speed);
                this->hub_->send_command(this->name_, "speed_" + std::to_string(target_speed));
                this->speed = target_speed; // Update internal speed state
            } else { // Turning OFF
                ESP_LOGD(TAG, "Turning OFF fan '%s'", this->name_.c_str());
                this->hub_->send_command(this->name_, "off");
            }
            this->state = new_state; // Update internal state
            state_updated = true;
        }
    }

    // Speed control (only applies if fan is ON or being turned ON)
    if (call.get_speed().has_value()) {
        int new_speed = *call.get_speed();
        if (new_speed < 1) new_speed = 1;
        if (new_speed > 6) new_speed = 6;

        // Only send speed command if fan is ON and speed is different
        if (this->state && this->speed != new_speed) {
            ESP_LOGD(TAG, "Changing speed for fan '%s' to %d", this->name_.c_str(), new_speed);
            this->hub_->send_command(this->name_, "speed_" + std::to_string(new_speed));
            this->speed = new_speed; // Update internal speed state
            state_updated = true;
        } else if (this->state && this->speed == new_speed) {
            // Speed requested is same as current speed while ON - do nothing
        } else if (!this->state) {
            // If fan is OFF (and not being turned on by this call), just store the speed for next time
             ESP_LOGV(TAG, "Storing speed %d for fan '%s' while OFF.", new_speed, this->name_.c_str());
             this->speed = new_speed;
             // Do not set state_updated = true, state hasn't visually changed yet
        }
    }

    // Publish the state back to ESPHome framework if any changes were made
    if (state_updated) {
        this->publish_state();
    }
}

} // namespace swing_fans
} // namespace esphome