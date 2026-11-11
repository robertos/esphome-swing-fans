#include "swing_fans.h"
#include "swing_fans_light.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cinttypes>

namespace esphome {
namespace swing_fans {

static const char *const TAG = "swing_fans.hub";

// Define static constants
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
    {"off", "11011111"}, 
    {"speed_1", "00111111"}, 
    {"speed_2", "01100111"},
    {"speed_3", "10111111"}, 
    {"speed_4", "10101111"}, 
    {"speed_5", "01101011"},
    {"speed_6", "01111111"}, 
    {"flip", "00101111"},
    {"light_off", "00000110"},
    {"light_1", "10100001"},
    {"light_2", "10110110"},
    {"light_3", "01110110"},
    {"light_4", "10100010"},
    {"light_5", "01111010"},
    {"light_6", "10100100"}
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
    ESP_LOGCONFIG(TAG, "  Managed Light Entities (%zu):", this->light_entities_.size());
    for (const auto &pair : this->light_entities_) {
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

void SwingFansHub::add_managed_light(const std::string &name, light::LightState *light_obj) {
    this->light_entities_[name] = light_obj;
    ESP_LOGD(TAG, "Hub is now managing light entity for fan: '%s'", name.c_str());
}

int SwingFansHub::get_fan_index_by_name_(const std::string &name) const {
    for (size_t i = 0; i < this->fan_configs_.size(); ++i) {
        if (this->fan_configs_[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const FanConfig *SwingFansHub::get_fan_config_by_name_(const std::string &name) {
    int idx = this->get_fan_index_by_name_(name);
    if (idx != -1) {
        return &this->fan_configs_[idx];
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

void SwingFansHub::send_command(const std::string &fan_name, const std::string &command_key, bool is_rf_repeat) {
    if (!this->transmitter_) {
        ESP_LOGE(TAG, "Transmitter not configured, cannot send command for fan '%s'", fan_name.c_str());
        return;
    }

    int fan_idx = this->get_fan_index_by_name_(fan_name);
    if (fan_idx < 0) {
        ESP_LOGE(TAG, "Cannot send command, fan '%s' not found in configuration.", fan_name.c_str());
        return;
    }
    const FanConfig *config = &this->fan_configs_[fan_idx];

    std::string command_code = get_command_code_(config, command_key);
    if (command_code.empty()) {
        // Warning already logged by get_command_code_
        return;
    }

    std::string bit_string = config->fan_id + command_code;
    const bool is_light_cmd = command_key.rfind("light_", 0) == 0;
    const uint32_t repeat_timer_id = static_cast<uint32_t>(fan_idx * 2 + (is_light_cmd ? 1 : 0));

    if (is_rf_repeat || this->receiving_rf_) {
        ESP_LOGD(TAG, "Scheduling RF repeat for '%s' in %" PRIu32 " ms: Key='%s'",
                 fan_name.c_str(), RF_REPEAT_DELAY_MS, command_key.c_str());
        this->set_timeout(repeat_timer_id, RF_REPEAT_DELAY_MS, [this, fan_name, command_key, bit_string]() {
            this->transmit_bit_string_(fan_name, command_key, bit_string);
        });
        return;
    }

    if (command_key != "flip") {
        this->cancel_timeout(repeat_timer_id);
    }
    this->transmit_bit_string_(fan_name, command_key, bit_string);
}

void SwingFansHub::transmit_bit_string_(const std::string &fan_name, const std::string &command_key, const std::string &bit_string) {
    if (!this->transmitter_) {
        return;
    }

    uint64_t raw_rc_code = 0;
    for (char bit : bit_string) {
        raw_rc_code = (raw_rc_code << 1) | (bit == '0' ? 1ULL : 0ULL);
    }
    this->has_last_received_code_ = true;
    this->last_received_code_ = raw_rc_code;
    this->last_received_time_ms_ = millis();

    ESP_LOGD(TAG, "Sending command for '%s': Key='%s', Bits='%s'",
             fan_name.c_str(), command_key.c_str(), bit_string.c_str());

    auto transmit_call = this->transmitter_->transmit();
    auto *transmit_data = transmit_call.get_data();
    transmit_data->reserve(2 + bit_string.size() * 2);
    transmit_data->space(SYNC_SPACE_US);
    transmit_data->mark(SYNC_MARK_US);

    for (char bit : bit_string) {
        if (bit == '0') {
            transmit_data->space(ZERO_SPACE_US);
            transmit_data->mark(ZERO_MARK_US);
        } else if (bit == '1') {
            transmit_data->space(ONE_SPACE_US);
            transmit_data->mark(ONE_MARK_US);
        } else {
            ESP_LOGW(TAG, "Invalid character '%c' in generated bit string for fan '%s'.", bit, fan_name.c_str());
        }
    }

    transmit_call.set_send_times(REPEAT_COUNT);
    transmit_call.perform();

    ESP_LOGD(TAG, "Transmission complete for fan '%s'.", fan_name.c_str());
}

void SwingFansHub::process_rc_switch_code(uint64_t code, uint8_t protocol) {
    if (protocol != EXPECTED_PROTOCOL) {
        ESP_LOGV(TAG, "Ignoring RC-Switch code via action with protocol %d (expected %d)", protocol, EXPECTED_PROTOCOL);
        return;
    }

    if ((code >> 24) > 0) {
        ESP_LOGV(TAG, "Ignoring RC-Switch code exceeding 24 bits: %" PRIu64, code);
        return;
    }

    const uint32_t now = millis();
    if (this->has_last_received_code_ && code == this->last_received_code_ &&
        (now - this->last_received_time_ms_) < RF_DEBOUNCE_MS) {
        this->last_received_time_ms_ = now;
        ESP_LOGV(TAG, "Ignoring duplicate RC-Switch code %" PRIu64 " within %" PRIu32 " ms debounce window",
                 code, RF_DEBOUNCE_MS);
        return;
    }
    this->has_last_received_code_ = true;
    this->last_received_code_ = code;
    this->last_received_time_ms_ = now;

    ESP_LOGV(TAG, "Received RC-Switch Proto %d, Code: %" PRIu64, protocol, code);

    auto decode_24 = [](uint64_t c, std::string &fid, std::string &cmd) {
        uint16_t fid_int = (~((c >> 8) & 0xFFFF)) & 0xFFFF;
        uint32_t cmd_int = (~(c & 0xFF)) & 0xFF;
        fid = std::bitset<16>(fid_int).to_string();
        cmd = std::bitset<8>(cmd_int).to_string();
    };

    auto decode_12 = [](uint64_t c, std::string &fid, std::string &cmd) {
        uint8_t fid_int = (~((c >> 7) & 0x1F)) & 0x1F;
        uint32_t cmd_int = (~(c & 0x7F)) & 0x7F;
        fid = std::bitset<5>(fid_int).to_string();
        cmd = std::bitset<7>(cmd_int).to_string();
    };

    auto has_command = [](const std::map<std::string, std::string> &cmd_map, const std::string &cmd_str) -> bool {
        for (const auto &pair : cmd_map) {
            if (pair.second == cmd_str) {
                return true;
            }
        }
        return false;
    };

    bool is_24_bit_code = false;
    std::string fan_id_str, command_code_str;

    if ((code >> 12) > 0) {
        is_24_bit_code = true;
        decode_24(code, fan_id_str, command_code_str);
    } else {
        std::string fan_id_12, cmd_12;
        std::string fan_id_24, cmd_24;
        decode_12(code, fan_id_12, cmd_12);
        decode_24(code, fan_id_24, cmd_24);

        bool fid_12_known = this->fan_id_to_name_lookup_.find(fan_id_12) != this->fan_id_to_name_lookup_.end();
        bool fid_24_known = this->fan_id_to_name_lookup_.find(fan_id_24) != this->fan_id_to_name_lookup_.end();
        bool cmd_12_valid = has_command(COMMANDS_7BIT, cmd_12);
        bool cmd_24_valid = has_command(COMMANDS_24BIT, cmd_24);

        if (fid_12_known && fid_24_known) {
            if (cmd_24_valid && !cmd_12_valid) {
                is_24_bit_code = true;
            } else {
                is_24_bit_code = false;
            }
        } else if (fid_12_known) {
            is_24_bit_code = false;
        } else if (fid_24_known) {
            is_24_bit_code = true;
        } else if (cmd_24_valid && !cmd_12_valid) {
            is_24_bit_code = true;
        } else {
            is_24_bit_code = false;
        }

        if (is_24_bit_code) {
            fan_id_str = std::move(fan_id_24);
            command_code_str = std::move(cmd_24);
        } else {
            fan_id_str = std::move(fan_id_12);
            command_code_str = std::move(cmd_12);
        }
    }

    ESP_LOGD(TAG, "Decoded %s-bit: FanID=%s, CmdCode=%s",
             is_24_bit_code ? "24" : "12", fan_id_str.c_str(), command_code_str.c_str());

    // Find fan name from the received ID
    auto name_it = this->fan_id_to_name_lookup_.find(fan_id_str);
    if (name_it == this->fan_id_to_name_lookup_.end()) {
        ESP_LOGW(TAG, "Received command (%s) for unknown %s-bit fan_id: %s",
                 command_code_str.c_str(), is_24_bit_code ? "24" : "12", fan_id_str.c_str());
        return;
    }
    const std::string fan_name = name_it->second;

    // Verify received code type matches the fan's configuration
    const FanConfig *config = get_fan_config_by_name_(fan_name);
    if (!config || config->is_24_bit != is_24_bit_code) {
         ESP_LOGW(TAG, "Received code type (%s-bit) mismatches config for fan '%s' (%s-bit). Ignoring.",
                  is_24_bit_code ? "24" : "12", fan_name.c_str(), config ? (config->is_24_bit ? "24" : "12") : "unknown");
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

    // Handle light commands
    if (command_key.rfind("light_", 0) == 0) {
        auto light_it = this->light_entities_.find(fan_name);
        if (light_it == this->light_entities_.end()) {
            ESP_LOGD(TAG, "Received '%s' command for fan '%s', but no light entity is managed.", command_key.c_str(), fan_name.c_str());
            return;
        }
        int target_level = (command_key == "light_off") ? 0 : (command_key.back() - '0');
        light::LightState *light_entity = light_it->second;
        auto *light_output = static_cast<SwingFansLight *>(light_entity->get_output());
        int current_level = light_output->get_current_level();
        if (current_level == -1) {
            current_level = SwingFansLight::compute_level(light_entity);
        }
        if (current_level == target_level) {
            ESP_LOGD(TAG, "Light for fan '%s' is already at level %d. Ignoring duplicate.", fan_name.c_str(), target_level);
            return;
        }
        ESP_LOGD(TAG, "Applying light command '%s' to fan '%s' from received code.", command_key.c_str(), fan_name.c_str());
        auto light_call = light_entity->make_call();
        if (command_key == "light_off") {
            light_call.set_state(false);
        } else {
            int level = command_key.back() - '0';
            light_call.set_state(true).set_brightness(static_cast<float>(level) / 6.0f);
        }
        this->receiving_rf_ = true;
        light_call.perform();
        this->receiving_rf_ = false;
        return;
    }

    // Find the managed fan entity
    auto entity_it = this->fan_entities_.find(fan_name);
    if (entity_it == this->fan_entities_.end()) {
        ESP_LOGW(TAG, "Received command for known fan '%s', but no matching fan entity managed.", fan_name.c_str());
        return;
    }
    fan::Fan *fan_entity = entity_it->second;

    if (command_key == "off" && !fan_entity->state) {
        ESP_LOGD(TAG, "Fan '%s' is already OFF. Ignoring duplicate.", fan_name.c_str());
        return;
    }
    if (command_key.rfind("speed_", 0) == 0) {
        int target_speed = command_key.back() - '0';
        if (fan_entity->state && fan_entity->speed == target_speed) {
            ESP_LOGD(TAG, "Fan '%s' is already at speed %d. Ignoring duplicate.", fan_name.c_str(), target_speed);
            return;
        }
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
    this->receiving_rf_ = true;
    call.perform(); // Update the fan component's state
    this->receiving_rf_ = false;
}

} // namespace swing_fans
} // namespace esphome
