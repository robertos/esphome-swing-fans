#pragma once

#include "esphome/core/component.h"
#include "esphome/components/fan/fan.h"
#include "swing_fans.h" // Needs Hub definition for interaction

namespace esphome {
namespace swing_fans {

class SwingFansFan : public fan::Fan, public Component {
public:
    // Constructor takes the fan's name, used for linking and logging
    SwingFansFan(const std::string &name) : name_(name) {}

    void setup() override;
    void dump_config() override;

    // Called by code generation to link this fan to its controlling hub
    void set_hub(SwingFansHub *hub) { this->hub_ = hub; }

    // Declare fan capabilities (speed levels)
    fan::FanTraits get_traits() override;

protected:
    // Handle calls from Home Assistant or ESPHome to change the fan state
    void control(const fan::FanCall &call) override;

    SwingFansHub *hub_{nullptr}; // Pointer to the central hub component
    std::string name_;           // Name of this fan (matches config)
};

} // namespace swing_fans
} // namespace esphome