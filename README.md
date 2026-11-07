# ESPHome Swing Fans Component

[![GitHub issues](https://img.shields.io/github/issues/robertos/esphome-swing-fans)](https://github.com/robertos/esphome-swing-fans/issues)
[![GitHub license](https://img.shields.io/github/license/robertos/esphome-swing-fans)](https://github.com/robertos/esphome-swing-fans/blob/main/LICENSE)

This is an ESPHome custom component to control multiple [Swing Fans DC ceiling fans](https://swingfans.com/collections/dc-ceiling-fan) that use [remotes that look like this](https://www.youtube.com/watch?v=WMw-yNIsO1c). The protocol uses a 12-bit or 24-bit command code, with specific pulse timings for sync, 0, and 1 bits.

> **NOTE:** This is shared with no guarantees that this will work or that it won't damage your fans. Use it at your own risk. This project has no relationship with the Swing Fans manufacturer.

This component requires you to configure a separate [`remote_transmitter`](https://esphome.io/components/remote_transmitter.html) component (and an optional [`remote_receiver`](https://esphome.io/components/remote_receiver.html) component), allowing flexibility in your choice of RF hardware (e.g., simple GPIO pins, CC1101 modules via their own transmitter/receiver platforms, etc.).

**Features:**

* Control multiple fans from a single ESP device.
* Supports both the 12-bit protocol (5-bit ID + 7-bit command) and the 24-bit protocol (16-bit ID + 8-bit command, with or without physical DIP switches).
* Integrates with standard ESPHome `remote_transmitter` and `remote_receiver`.
* Automatically creates standard `fan` entities for Home Assistant (supporting On/Off and 6 speeds).
* Automatically creates `button` entities to send the 'flip' (direction change) command.
* Provides optional `light` entities (supporting On/Off and 6 brightness levels) for 24-bit fans with built-in dimmable LED lights.
* Allows hooks (`on_transmit_begin`/`on_transmit_end`) for transmitters requiring special handling (like CC1101 mode switching).
* Updates fan and light state in Home Assistant (and repeats the RF signal) if commands are received via RF (e.g., from the original remote).

## Dependencies

* ESPHome - tested on 2026.10.2.
* [`remote_transmitter`](https://esphome.io/components/remote_transmitter.html) component configured in your YAML.
* [`remote_receiver`](https://esphome.io/components/remote_receiver.html) component configured in your YAML (optional, listening for `rc_switch` data to sync state from physical remotes).

## Hardware

- ESP32 board
- Some RF transmitter - I used a CC1101 module

For an example of building the hardware, see the [instructions in the ESPSomfy-RTS wiki](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Simple-ESPSomfy-RTS-device).

## Installation

You can install this component using the `external_components` feature of ESPHome. Add the following to your device's YAML configuration:

```yaml
external_components:
  - source: github://robertos/esphome-swing-fans@main # Or specify a release tag
    components: [ swing_fans ] # Specify the component name
```

## Configuration

```yaml
# 1. Configure your chosen transmitter and receiver FIRST
remote_transmitter:
  id: gpio_tx # Example ID
  pin: GPIO32 # Example pin
  carrier_duty_percent: 100%
  non_blocking: true

# The receiver is only required if you want to listen for physical remotes
remote_receiver:
  id: gpio_rx # Example ID
  pin: GPIO33 # Example pin
  tolerance: 60%
  filter: 4us
  idle: 4ms
  dump: rc_switch # Recommended when discovering your fan_id
  on_rc_switch:
    then:
      - swing_fans.received_code:
          hub_id: fan_controller
          code: !lambda return x.code;
          protocol: !lambda return x.protocol;

# 2. Configure the main swing_fans controller
swing_fans:
  id: fan_controller # An ID for this controller instance
  remote_transmitter_id: gpio_tx # Link to your transmitter ID

  # Optional hooks for complex transmitters
  # on_transmit_begin:
  #   - ...
  # on_transmit_end:
  #   - ...

  # List ALL fans controlled by this component
  fans:
    - name: "Office Fan"
      fan_id: "01000"                # 5-bit binary ID for 12-bit fans
      is_24_bit: false               # Optional, defaults to false
    - name: "Living Room Fan"
      fan_id: "00100"
    - name: "Second Room Fan"
      fan_id: "1110101110100001"     # 16-bit binary ID for 24-bit fans
      is_24_bit: true
      has_light: true                # Optional, exposes a dimmable light entity
      light_name: "Second Room Light" # Optional, defaults to "<Fan Name> Light"
```

### Configuration Variables

**`swing_fans` (Hub):**

* **id** (Optional, ID): Manually specify the ID used for linking.
* **remote_transmitter_id** (Required, ID): The ID of the `remote_transmitter` component to use for sending commands.
* **fans** (Required, list): A list defining each fan.
    * **name** (Required, string): The unique name for this fan.
    * **fan_id** (Required, string):
        * For **12-bit fans** (`is_24_bit: false`): A **5-character** binary string representing the fan's ID (e.g., `"01000"`).
        * For **24-bit fans** (`is_24_bit: true`): A **16-character** binary string representing the fan's full prefix/ID (e.g., `"1110101110100001"`).
    * **is_24_bit** (Optional, boolean): Set to `true` if this fan uses the 24-bit protocol. Defaults to `false` (12-bit protocol).
    * **has_light** (Optional, boolean): Set to `true` (on 24-bit fans) to create a dimmable `light` entity with 6 brightness levels. Defaults to `false`.
    * **light_name** (Optional, string): The name of the light entity when `has_light: true`. Defaults to `<Fan Name> Light`.
    * **direction_button_icon** (Optional, Icon): The icon to be used for the direction button entity, defaults to `"mdi:swap-horizontal"`.
    * **direction_button_name** (Optional, string): The name of the entity for the direction button, defaults to `<Fan Name> Direction`.
    * **restore_mode** (Optional): Control how the fan attempts to restore state on boot. See the [ESPHome docs for Fan](https://esphome.io/components/fan/index.html), defaults to `RESTORE_DEFAULT_OFF` so the previous state is restored after a reboot when available.
* **on_transmit_begin** (Optional, Action): An action to perform immediately before transmitting a command. Useful for `cc1101.begin_tx`.
* **on_transmit_end** (Optional, Action): An action to perform immediately after transmitting a command. Useful for `cc1101.end_tx`.

## Finding Your `fan_id`

### Method 1: From the `swing_fans` Logs (Recommended)

The easiest way to find your `fan_id` (whether your remote is 12-bit, 24-bit with DIP switches, or 24-bit without DIP switches) is to let the component decode it for you:

1. Flash your ESP device with `remote_receiver` and `swing_fans.received_code` configured (you can start with any dummy fan entry in `fans:`).
2. Open the ESPHome logs and press any speed or power button on your physical fan remote.
3. Look for the warning message logged by `swing_fans.hub`:
   ```text
   [W][swing_fans.hub:xxx]: Received command (11011111) for unknown 24-bit fan_id: 1111011010011011
   ```
   or for a 12-bit remote:
   ```text
   [W][swing_fans.hub:xxx]: Received command (0000010) for unknown 12-bit fan_id: 01000
   ```
   *(If `logger` level is `DEBUG`, you will also see `Decoded 24-bit: FanID=..., CmdCode=...`)*
4. Copy the exact `fan_id` string printed in the log into your YAML configuration, and set `is_24_bit: true` if it is a 24-bit fan.

### Method 2: From `dump: rc_switch` or DIP Switches

* **From `dump: rc_switch` logs:**
  When `dump: rc_switch` is enabled on `remote_receiver`, pressing a remote button logs an `rc_switch` packet with `protocol: 6` and a 12-bit or 24-bit binary code. Because `rc_switch` Protocol 6 reads bits inverted relative to the Swing Fans logical protocol:
  * **12-bit remote:** Take the **first 5 bits** of the received `rc_switch` binary code and **invert every bit** (`0` → `1`, `1` → `0`).
  * **24-bit remote:** Take the **first 16 bits** of the received `rc_switch` binary code and **invert every bit** (`0` → `1`, `1` → `0`).
* **Migrating an older 5-bit `fan_id` on a 24-bit fan:**
  Older versions of this component used a 5-bit `fan_id` for 24-bit fans and hardcoded the next 11 bits as `01110100001`. To migrate an existing 5-bit `fan_id` on a 24-bit fan to the 16-bit format, simply append `01110100001` to your 5-bit ID (for example, `"11101"` becomes `"1110101110100001"`).

## Example with CC1101

If you are using a CC1101 module via the [Official CC1101 component](https://esphome.io/components/cc1101):

```yaml
external_components:
  - source: github://robertos/esphome-swing-fans@main
    components: [ swing_fans ]

spi:
  clk_pin: GPIO18
  miso_pin: GPIO19
  mosi_pin: GPIO23

cc1101:
  id: transceiver
  cs_pin: GPIO5
  frequency: 433.92MHz

remote_transmitter:
  id: transmitter
  pin: GPIO32 # GDO0 on the CC1101
  carrier_duty_percent: 100%
  non_blocking: true
  on_transmit:
    then:
      - cc1101.begin_tx: transceiver # Use the ID of the cc1101 component
  on_complete:
    then:
      - cc1101.begin_rx: transceiver # Use the ID of the cc1101 component

remote_receiver:
  id: receiver
  pin: GPIO33 # GDO2 on the CC1101
  tolerance: 60%
  filter: 4us
  idle: 4ms
  dump: rc_switch
  on_rc_switch:
    then:
      - swing_fans.received_code:
          hub_id: fan_controller
          code: !lambda return x.code;
          protocol: !lambda return x.protocol;

swing_fans:
  id: fan_controller
  remote_transmitter_id: transmitter # Use the transmitter ID above
  fans:
    # ... fan list ...
```

## Protocol Details

This component implements a specific RF protocol observed for Swing Fans DC ceiling fans:

* **Transmission:** Uses raw pulse width modulation (repeated 15 times per command).
    * Sync Pulse: `(-8900, 336)` microseconds
    * '0' Bit Pulse: `(-658, 336)` microseconds
    * '1' Bit Pulse: `(-321, 689)` microseconds
* **Data Structure:**
    * **12-bit protocol:** `[5-bit Fan ID][7-bit Command]` (12 bits total)
    * **24-bit protocol:** `[16-bit Fan ID][8-bit Command]` (24 bits total)
        * On some 24-bit remotes, the 16-bit Fan ID consists of `[5-bit ID][01110100001]`; on others (such as Pebble Storm no-DIP remotes), all 16 bits form a factory-paired prefix/ID.
    * Both the ID and Command parts are bitwise inverted in raw `rc_switch` reception compared to the logical bit strings used by the pulse encoder. The component handles this inversion automatically.
* **Reception:** Uses `remote_receiver` listening for `rc_switch` data, specifically checking for `protocol: 6`, which is close enough to the Swing Fans RF protocol for decoding (not for transmitting). It decodes the received `code` back into Fan ID and Command.

## Changelog

### November 7, 2026

* **Breaking Change:** `fan_id` for 24-bit fans (`is_24_bit: true`) is now a full 16-bit binary string instead of 5 bits, enabling support for 24-bit remotes with different 16-bit factory prefixes (such as no-DIP remotes). To migrate an existing 5-bit `fan_id`, append `01110100001` (e.g., `"11101"` → `"1110101110100001"`).
* Added support for dimmable fan lights (6 brightness levels) via `has_light: true` and `light_name`.
* Automatically load `fan`, `button`, and `light` platforms via `AUTO_LOAD` so empty `fan: []` and `button: []` blocks are no longer required in YAML.
* Fixed `ReceivedCodeAction::play` `const Ts &...` signature for compatibility with ESPHome 2026.5+.
* Clamped restored fan speed on boot when no valid speed was previously saved in flash.

### May 23, 2026

* Set default `restore_mode` for fans to `RESTORE_DEFAULT_OFF`.
* Publish restored fan state during `setup()` so Home Assistant reflects persisted state after reboot.

## Contributing

Contributions are welcome! Please open an issue or pull request.
