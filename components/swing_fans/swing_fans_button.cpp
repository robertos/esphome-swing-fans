#include "swing_fans_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace swing_fans {

static const char *const TAG = "swing_fans.button";

void SwingFansButton::dump_config() {
    LOG_BUTTON(TAG, "Swing Fan Direction Button", this);
    ESP_LOGCONFIG(TAG, "  Controls Fan: %s", this->fan_name_.c_str());
    ESP_LOGCONFIG(TAG, "  Hub Linked: %s", YESNO(this->hub_ != nullptr));
}

void SwingFansButton::press_action() {
    if (!this->hub_) {
        ESP_LOGE(TAG, "Cannot trigger direction for fan '%s', hub is not linked!", this->fan_name_.c_str());
        return;
    }

    ESP_LOGD(TAG, "Direction button pressed for fan '%s', sending 'flip' command.", this->fan_name_.c_str());
    // Send the "flip" command via the hub for the associated fan
    this->hub_->send_command(this->fan_name_, "flip");
}

} // namespace swing_fans
} // namespace esphome