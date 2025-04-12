#include "swing_fans.h"
#include "esphome/core/log.h"

namespace esphome {
namespace swing_fans {

static const char *const TAG = "swing_fans.hub";

// Define static constants
const std::vector<int> SwingFansHub::SYNC_PULSES = {-8900, 336};
const std::vector<int> SwingFansHub::ZERO_PULSES = {-658, 336};
const std::vector<int> SwingFansHub::ONE_PULSES = {-321, 689};

const std::map<std::string, std::string> SwingFansHub::COMMANDS_7BIT = {
    {"off", "0000010"}, 
    {"speed_1", "0001000"}, 
    {"speed_2", "0001010"},
    {"speed_3", "0010000"}, 
    {"speed_4", "0011000"}, 
    {"speed_5", "0100010"},
    {"speed_6", "0100000"}, 
    {"flip", "0000100"}
};
const std::map<std::string, std::string> SwingFansHub::COMMANDS_24BIT = {
    {"off", "0111010000111011111"}, 
    {"speed_1", "0111010000100111111"}, 
    {"speed_2", "0111010000101100111"},
    {"speed_3", "0111010000110111111"}, 
    {"speed_4", "0111010000110101111"}, 
    {"speed_5", "0111010000101101011"},
    {"speed_6", "0111010000101111111"}, 
    {"flip", "0111010000100101111"}
};

void SwingFansHub::setup() {
    ESP_LOGCONFIG(TAG, "Setting up Swing Fans Hub...");


    // Build reverse lookup map from stored configs
    for(const auto& config : this->fan_configs_) {
        this->fan_id_to_name_lookup_[config.fan_id] = config.name;
    }
    ESP_LOGCONFIG(TAG, "Fan ID lookup map built with %zu entries.", this->fan_id_to_name_lookup_.size());
}

void SwingFansHub::dump_config() {
    ESP_LOGCONFIG(TAG, "Swing Fans Hub:");
    ESP_LOGCONFIG(TAG, "  Transmitter Linked: %s", YESNO(this->transmitter_ != nullptr));
    ESP_LOGCONFIG(TAG, "  Configured Fans (%zu):", this->fan_configs_.size());
    for (const auto &config : this->fan_configs_) {
        ESP_LOGCONFIG(TAG, "    - Name: %s, ID: %s, 24-bit: %s",
                      config.name.c_str(), config.fan_id.c_str(), YESNO(config.is_24_bit));
    }
     ESP_LOGCONFIG(TAG, "  Managed Fan Entities (%zu):", this->fan_entities_.size());
     for (const auto &pair : this->fan_entities_) {
         ESP_LOGCONFIG(TAG, "    - '%s'", pair.first.c_str());
     }
}

void SwingFansHub::add_fan_config(const std::string &name, const std::string &fan_id, bool is_24_bit) {
    this->fan_configs_.push_back({name, fan_id, is_24_bit});
}

void SwingFansHub::add_managed_fan(const std::string &name, fan::Fan *fan_obj) {
    this->fan_entities_[name] = fan_obj;
    ESP_LOGD(TAG, "Hub is now managing fan entity: '%s'", name.c_str());
}

const FanConfig *SwingFansHub::get_fan_config_by_name_(const std::string &name) {
    for (const auto &config : this->fan_configs_) {
        if (config.name == name) {
            return &config;
        }
    }
    return nullptr;
}

const std::string SwingFansHub::get_command_code_(const FanConfig *config, const std::string &command_key) {
     const auto& command_map = config->is_24_bit ? COMMANDS_24BIT : COMMANDS_7BIT;
     auto it = command_map.find(command_key);
     if (it != command_map.end()) {
         return it->second;
     }
     ESP_LOGW(TAG, "Unknown command key '%s' requested for fan '%s'", command_key.c_str(), config->name.c_str());
     return "";
}


void SwingFansHub::send_command(const std::string &fan_name, const std::string &command_key) {
    if (!this->transmitter_) {
        ESP_LOGE(TAG, "Transmitter not configured, cannot send command for fan '%s'", fan_name.c_str());
        return;
    }

    const FanConfig *config = get_fan_config_by_name_(fan_name);
    if (!config) {
        ESP_LOGE(TAG, "Cannot send command, fan '%s' not found in configuration.", fan_name.c_str());
        return;
    }

    std::string command_code = get_command_code_(config, command_key);
    if (command_code.empty()) {
        // Warning already logged by get_command_code_
        return;
    }

    std::string bit_string = config->fan_id + command_code;
    ESP_LOGD(TAG, "Sending command for '%s': Key='%s', Bits='%s'",
             fan_name.c_str(), command_key.c_str(), bit_string.c_str());

    // Construct raw pulse data
    std::vector<int> pulse_list;
    pulse_list.insert(pulse_list.end(), SYNC_PULSES.begin(), SYNC_PULSES.end());

    for (char bit : bit_string) {
        if (bit == '0') {
            pulse_list.insert(pulse_list.end(), ZERO_PULSES.begin(), ZERO_PULSES.end());
        } else if (bit == '1') {
            pulse_list.insert(pulse_list.end(), ONE_PULSES.begin(), ONE_PULSES.end());
        } else {
             ESP_LOGW(TAG, "Invalid character '%c' in generated bit string for fan '%s'.", bit, fan_name.c_str());
        }
    }

    this->transmit_begin_trigger_->trigger();

    auto transmit_call = this->transmitter_->transmit();
    auto transmit_data = transmit_call.get_data();
    transmit_data->set_data(pulse_list);
    transmit_call.set_send_times(REPEAT_COUNT);
    transmit_call.perform();

    this->transmit_end_trigger_->trigger();
    ESP_LOGD(TAG, "Transmission complete for fan '%s'.", fan_name.c_str());
}

void SwingFansHub::process_rc_switch_code(uint64_t code, uint8_t protocol) {
    if (protocol != EXPECTED_PROTOCOL) {
        ESP_LOGV(TAG, "Ignoring RC-Switch code via action with protocol %d (expected %d)", protocol, EXPECTED_PROTOCOL);
        return;
    }

    ESP_LOGV(TAG, "Received RC-Switch Proto %d, Code: %llu", protocol, code);

    // Determine code type based on bit length (heuristic)
    bool is_24_bit_code = (code >> 12) > 0;

    std::string fan_id_str, command_code_str;
    uint32_t command_int;

    // Decode based on determined type (includes bit inversion)
    if (is_24_bit_code) {
        uint8_t fan_id_int = (~((code >> 19) & 0x1F)) & 0x1F;
        command_int = (~(code & 0x7FFFF)) & 0x7FFFF; // 19 bits mask
        fan_id_str = std::bitset<5>(fan_id_int).to_string();
        command_code_str = std::bitset<19>(command_int).to_string();
        ESP_LOGD(TAG, "Decoded 24-bit: FanID=%s, CmdCode=%s", fan_id_str.c_str(), command_code_str.c_str());
    } else {
        uint8_t fan_id_int = (~((code >> 7) & 0x1F)) & 0x1F;
        command_int = (~(code & 0x7F)) & 0x7F; // 7 bits mask
        fan_id_str = std::bitset<5>(fan_id_int).to_string();
        command_code_str = std::bitset<7>(command_int).to_string();
        ESP_LOGD(TAG, "Decoded 7-bit: FanID=%s, CmdCode=%s", fan_id_str.c_str(), command_code_str.c_str());
    }

    // Find fan name from the received ID
    auto name_it = this->fan_id_to_name_lookup_.find(fan_id_str);
    if (name_it == this->fan_id_to_name_lookup_.end()) {
        ESP_LOGW(TAG, "Received command for unknown fan_id: %s", fan_id_str.c_str());
        return;
    }
    const std::string fan_name = name_it->second;

    // Find the managed fan entity
    auto entity_it = this->fan_entities_.find(fan_name);
    if (entity_it == this->fan_entities_.end()) {
        ESP_LOGW(TAG, "Received command for known fan '%s', but no matching fan entity managed.", fan_name.c_str());
        return;
    }
    fan::Fan *fan_entity = entity_it->second;

    // Verify received code type matches the fan's configuration
    const FanConfig *config = get_fan_config_by_name_(fan_name);
    if (!config || config->is_24_bit != is_24_bit_code) {
         ESP_LOGW(TAG, "Received code type (%s-bit) mismatches config for fan '%s' (%s-bit). Ignoring.",
                  is_24_bit_code ? "24" : "7", fan_name.c_str(), config ? (config->is_24_bit ? "24" : "7") : "unknown");
         return;
    }

    // Find command key matching the decoded command code string
    const auto& command_map = is_24_bit_code ? COMMANDS_24BIT : COMMANDS_7BIT;
    std::string command_key = "";
    for(const auto& pair : command_map) {
        if (pair.second == command_code_str) {
            command_key = pair.first;
            break;
        }
    }

    if (command_key.empty()) {
        ESP_LOGW(TAG, "Unknown command code: %s received for fan '%s'", command_code_str.c_str(), fan_name.c_str());
        return;
    }

    // Apply the command to the fan entity's state
    ESP_LOGD(TAG, "Applying command '%s' to fan '%s' from received code.", command_key.c_str(), fan_name.c_str());
    auto call = fan_entity->make_call();
    if (command_key == "off") {
        call.set_state(false);
    } else if (command_key == "speed_1") {
        call.set_state(true).set_speed(1);
    } else if (command_key == "speed_2") {
        call.set_state(true).set_speed(2);
    } else if (command_key == "speed_3") {
        call.set_state(true).set_speed(3);
    } else if (command_key == "speed_4") {
        call.set_state(true).set_speed(4);
    } else if (command_key == "speed_5") {
        call.set_state(true).set_speed(5);
    } else if (command_key == "speed_6") {
        call.set_state(true).set_speed(6);
    } else if (command_key == "flip") {
        // 'Flip' command from remote doesn't change fan state, only button press does.
        ESP_LOGD(TAG, "Received 'flip' command for fan '%s'. No fan state change.", fan_name.c_str());
        return; // Do not perform the call
    } else {
         ESP_LOGW(TAG, "Internal error: Command key '%s' not handled in receiver.", command_key.c_str());
         return;
    }
    call.perform(); // Update the fan component's state
}

} // namespace swing_fans
} // namespace esphome