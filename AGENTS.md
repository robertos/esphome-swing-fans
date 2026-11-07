# Agent Guidelines (`esphome-swing-fans`)

## Architecture & Invariants
- All component code is in `components/swing_fans/`: `SwingFansHub` (`swing_fans.h/.cpp`), `SwingFansFan`, `SwingFansButton`, and `SwingFansLight`.
- `AUTO_LOAD = ["fan", "button", "light"]` is set in `__init__.py`; do not require empty `fan: []`, `button: []`, or `light: []` blocks in YAML.
- **Protocol & Bit Inversion:**
  - `rc_switch` Protocol 6 bits are **bitwise inverted** relative to the logical bit strings used in `SwingFansHub` (`'0'` = `{-658, 336}` us, `'1'` = `{-321, 689}` us, sync = `{-8900, 336}` us, 15 repeats).
  - **12-bit (`is_24_bit: false`):** `[5-bit fan_id] + [7-bit command]` (`COMMANDS_7BIT`).
  - **24-bit (`is_24_bit: true`):** `[16-bit fan_id] + [8-bit command]` (`COMMANDS_24BIT`, includes `light_off` and `light_1`..`light_6`).
- **RF Repeater:** Receiving an RF command updates entity state and intentionally re-transmits the RF packet when state changes.

## Validation
Always validate changes with `esphome` (using `external_components` pointing to `./components`):
```sh
esphome config <test.yaml>
esphome compile <test.yaml>
```
