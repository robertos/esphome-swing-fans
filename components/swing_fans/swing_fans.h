#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include "esphome/components/fan/fan.h"

#include <vector>
#include <map>
#include <string>
#include <bitset>

// Forward declarations
namespace esphome {
namespace swing_fans {

class SwingFansFan;     // Although managed as fan::Fan*, specific type exists
class SwingFansButton;  // Although managed as button::Button*, specific type exists

struct FanConfig {
    std::string name;
    std::string fan_id;
    bool is_24_bit;
};

class SwingFansHub : public Component {
public:
    void setup() override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

    void set_transmitter(remote_transmitter::RemoteTransmitterComponent *transmitter) { this->transmitter_ = transmitter; }

    // Stores config data for later use (sending commands)
    void add_fan_config(const std::string &name, const std::string &fan_id, bool is_24_bit);
    // Stores pointer to the managed fan entity (for receiving commands)
    void add_managed_fan(const std::string &name, fan::Fan *fan_obj);

    // Called by managed Fan/Button entities to trigger RF transmission
    void send_command(const std::string &fan_name, const std::string &command_key);

    // Public method to be called by the remote_receiver when a code is received
    void process_rc_switch_code(uint64_t code, uint8_t protocol);

    // Triggers for external actions (e.g., CC1101 control)
    Trigger<> *get_transmit_begin_trigger() const { return this->transmit_begin_trigger_; }
    Trigger<> *get_transmit_end_trigger() const { return this->transmit_end_trigger_; }

protected:
    // RF Protocol Constants
    static const std::vector<int> SYNC_PULSES;
    static const std::vector<int> ZERO_PULSES;
    static const std::vector<int> ONE_PULSES;
    static const int REPEAT_COUNT = 15;
    static const uint8_t EXPECTED_PROTOCOL = 6;

    // Command Code Mappings
    static const std::map<std::string, std::string> COMMANDS_7BIT;
    static const std::map<std::string, std::string> COMMANDS_24BIT;

    remote_transmitter::RemoteTransmitterComponent *transmitter_{nullptr};

    std::vector<FanConfig> fan_configs_;                       // Stores config (ID, bitness) per fan name
    std::map<std::string, fan::Fan *> fan_entities_;           // Maps fan name to the managed fan entity pointer
    std::map<std::string, std::string> fan_id_to_name_lookup_; // Maps received fan ID string to fan name

    Trigger<> *transmit_begin_trigger_ = new Trigger<>();
    Trigger<> *transmit_end_trigger_ = new Trigger<>();

    // Internal helper methods
    const FanConfig *get_fan_config_by_name_(const std::string &name);
    const std::string get_command_code_(const FanConfig *config, const std::string &command_key);
};

template <typename... Ts>
class ReceivedCodeAction : public Action<Ts...> {
 public:
  ReceivedCodeAction(SwingFansHub *hub) : hub_(hub) {}

  TEMPLATABLE_VALUE(uint64_t, code)
  TEMPLATABLE_VALUE(uint8_t, protocol)

  void play(Ts... x) override {
    this->hub_->process_rc_switch_code(
        this->code_.value(x...),
        this->protocol_.value(x...)
    );
  }

 protected:
  SwingFansHub *hub_;
};

} // namespace swing_fans
} // namespace esphome