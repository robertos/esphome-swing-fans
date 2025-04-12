#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "swing_fans.h" // Needs Hub definition for interaction

namespace esphome {
namespace swing_fans {

class SwingFansButton : public button::Button, public Component {
public:
    // Constructor takes the fan's name that this button controls
    SwingFansButton(const std::string &fan_name) : fan_name_(fan_name) {}

    void dump_config() override;

    // Called by code generation to link this button to its controlling hub
    void set_hub(SwingFansHub *hub) { this->hub_ = hub; }

protected:
    // Action performed when the button is pressed in Home Assistant or ESPHome
    void press_action() override;

    SwingFansHub *hub_{nullptr}; // Pointer to the central hub component
    std::string fan_name_;       // Name of the fan this button controls
};

} // namespace swing_fans
} // namespace esphome