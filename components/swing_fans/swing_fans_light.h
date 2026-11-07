#pragma once

#include "esphome/components/light/light_output.h"
#include "swing_fans.h" // Needs Hub definition for interaction

namespace esphome {
namespace swing_fans {

class SwingFansLight : public light::LightOutput {
public:
    // Constructor takes the fan's name that this light belongs to
    SwingFansLight(const std::string &fan_name) : fan_name_(fan_name) {}

    // Called by code generation to link this light output to its controlling hub
    void set_hub(SwingFansHub *hub) { this->hub_ = hub; }

    // Declare light capabilities (brightness with 6 discrete levels)
    light::LightTraits get_traits() override;

    // Handle calls from Home Assistant or ESPHome to change the light state
    void write_state(light::LightState *state) override;

protected:
    SwingFansHub *hub_{nullptr}; // Pointer to the central hub component
    std::string fan_name_;       // Name of the fan this light belongs to
    int current_level_{-1};      // -1 = uninitialized (boot restore), 0 = off, 1..6 = brightness level
};

} // namespace swing_fans
} // namespace esphome
