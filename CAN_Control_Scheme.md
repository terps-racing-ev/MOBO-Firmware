# MOBO CAN Command and Control Scheme

This document describes the CAN-facing behavior implemented in the current MOBO firmware.

All MOBO-originated CAN traffic uses 29-bit extended identifiers under the `0x00200xxx` prefix.

## Default Power-Up Behavior

- On boot, all four controlled outputs are forced OFF:
  - `Pump`
  - `DRS`
  - `Fans`
  - `Radiator Fans`
- The system state starts in `INIT`.
- After module initialization completes in `main.c`, the state is set to `STANDBY`.
- The safety monitor then promotes `STANDBY -> ACTIVE` after its first task sample. In the current code, safety inputs are telemetry only and do not gate command acceptance.
- If a nonzero relay mask was previously saved to flash, that mask is applied once after the system first reaches `ACTIVE`.
- External relay commands are accepted from `VCU_MOBO_Command` only.
- Error bits and warning bits are telemetry only in the current codebase. They do not automatically force `FAULT`, do not drive `RECOVERY`, and do not reject relay commands.
- The pump can also be driven internally by the coolant-pump logic after its startup lockout expires.

## Controlled Outputs

Relay/control bit layout is shared by the VCU command payload and the persisted boot-restore state:

| Bit | Channel |
| --- | --- |
| 0 | `Pump` |
| 1 | `DRS` |
| 2 | `Fans` |
| 3 | `Radiator Fans` |

### Acc Fans (composite, not a separate relay)

`Acc Fans` is **not** an additional relay. It is a firmware-side composite control that drives `DRS` and `Fans` in an alternating, mutually-exclusive pattern. When `Acc Fans` is enabled:

- The firmware **ignores** the VCU-supplied `DRS_Request` and `Fans_Request` bits.
- Exactly one of `DRS` and `Fans` is on at any time.
- The firmware swaps which one is on every `ACC_FANS_TOGGLE_PERIOD_MS` (currently `30000 ms` / 30 s), defined in [Core/Inc/power_manager.h](Core/Inc/power_manager.h).
- The first phase after activation is `DRS` on, `Fans` off.

Acc Fans is engaged whenever **either** of these is true:

1. The VCU asserts `Acc_Fans_Request` on `VCU_MOBO_Command` (byte 1 bit 4).
2. The accumulator max-cell temperature override has latched on. The firmware listens to HVC `ACC_Summary` (extended CAN ID `0x004001F5`, signal `Acc_Temp_Max_C`, 0.1 °C/LSB at bytes 6..7 little-endian signed) and:
   - latches the override **ON** when `Acc_Temp_Max_C` reaches `ACC_FANS_TEMP_ON_C` (currently `45 °C`),
   - releases the override when it falls to `ACC_FANS_TEMP_OFF_C` (currently `40 °C`),
   - holds the previous state between the two thresholds.

   Both thresholds are defined in [Core/Inc/power_manager.h](Core/Inc/power_manager.h).

`DRS` and `Fans` are also unconditionally clamped to mutual exclusion in the firmware: if both bits ever resolve to `1` at the GPIO drive step (from any source — Acc Fans, direct VCU mask, internal override), `Fans` is forced off and `ERROR_RELAY_FAULT` is raised so the conflict is visible on the bus.

<!--
Dashboard guidance (firmware does NOT enforce any of this):
- The dashboard should expose a single prominent "Acc Fans" toggle that drives the Acc_Fans_Request bit (VCU_MOBO_Command byte 1 bit 4).
- The independent DRS and Fans toggles should be retained as backup controls only and rendered small / minimized so they cannot be accidentally pressed.
- When Acc Fans is reported active via MOBO_Relay_Status (Acc_Fans_Active = 1), the dashboard should visually deemphasize or disable the individual DRS/Fans toggles, since the firmware ignores them while Acc Fans is on.
- Acc_Fans_Active = 1 can be driven by either the VCU command bit or the HVC accumulator-temperature override; the dashboard cannot distinguish the two from this bit alone.
-->


## Command Inputs (Bus to MOBO)

### `VCU_MOBO_Command`

- CAN ID: `0x002001F0`
- Direction: `VCU -> MOBO`
- Purpose: Primary relay control input.

Signal names:

| Signal | Meaning |
| --- | --- |
| `Enable` | Master enable for the requested mask |
| `Pump_Request` | Request Pump ON |
| `DRS_Request` | Request DRS ON (ignored while `Acc_Fans_Request = 1`) |
| `Fans_Request` | Request Fans ON (ignored while `Acc_Fans_Request = 1`) |
| `Radiator_Fans_Request` | Request Radiator Fans ON |
| `Acc_Fans_Request` | Enable Acc Fans mode: firmware auto-alternates DRS and Fans every 30 s, mutually exclusive |

Behavior:

- If `Enable = 0`, MOBO forces the requested mask to `0x0`, disables Acc Fans, and turns all channels OFF.
- If `Enable = 1`, byte 1 bits 0..3 are applied as the requested mask, and byte 1 bit 4 (`Acc_Fans_Request`) enables or disables Acc Fans mode.
- While `Acc_Fans_Request = 1`, the firmware overrides `DRS_Request` and `Fans_Request` with its own alternation; the other relay bits (`Pump_Request`, `Radiator_Fans_Request`) still behave normally.
- Accepted commands update the in-RAM commanded mask immediately.
- The accepted mask is persisted to flash after a `500 ms` debounce window. The Acc Fans mode flag is **not** persisted across resets; it must be re-asserted by the VCU after a reboot.
- The current firmware has no RPI relay-command path and no CAN-level authority arbitration.

### `MOBO_Reset_Command`

- CAN ID: `0x002001F7`
- Direction: `Host -> MOBO`
- Purpose: Reset the MCU.

Signal names:

| Signal | Meaning |
| --- | --- |
| `Ignored` | Payload is ignored |

Behavior:

- Any received frame on `0x002001F7` triggers `NVIC_SystemReset()`.
- No magic bytes or payload validation are required.

### `MOBO_Config_Command`

- CAN ID: `0x002001F8`
- Direction: `Host -> MOBO`
- Purpose: Update runtime configuration parameters and persist them to flash.

Signal names:

| Signal | Meaning |
| --- | --- |
| `Param_ID` | Selects the configuration field to update |
| `Value` | Signed 32-bit little-endian value |

Supported `Param_ID` values:

| Param_ID | Name | Meaning |
| --- | --- | --- |
| `0x01` | `CONFIG_PARAM_LV_OFFSET_MA` | LV current-sensor offset in mA |
| `0x02` | `CONFIG_PARAM_HC_OFFSET_MA` | HC current-sensor offset in mA |
| `0x03` | `CONFIG_PARAM_VDIV_NUM` | Battery voltage-divider numerator |
| `0x04` | `CONFIG_PARAM_VDIV_DEN` | Battery voltage-divider denominator, must be `> 0` |
| `0x05` | `CONFIG_PARAM_RPI_TIMEOUT_MS` | Stored timeout value in milliseconds; currently not consumed by an active control path |

Behavior:

- MOBO requires at least 5 data bytes: 1 byte of `Param_ID` and 4 bytes of little-endian `Value`.
- MOBO applies the parameter update in RAM and persists it to flash.
- A bad `Param_ID` is rejected.
- The saved relay mask is not written through this command; it is managed automatically by the power manager after accepted VCU relay commands.

## External CAN Inputs Consumed by MOBO

These frames are not MOBO-prefixed commands, but they are subscribed by the current firmware:

### `Inverter Temperatures_3`

- CAN ID: `0x0A2` standard
- Purpose: Provides `INV_Coolant_Temp` for the internal coolant-pump temperature override.

Behavior:

- The coolant temperature is read from bytes 0..1 as a signed little-endian value with `0.1 C / LSB`.
- The pump temperature override latches ON at the configured temperature threshold and releases below the threshold minus hysteresis.

### `VCU_Summary`

- CAN ID: `0x0D1001F0` extended
- Purpose: Provides `VCU_RTD_Active` to the internal coolant-pump logic.

Behavior:

- `VCU_RTD_Active` is read from byte 4 bit 0.
- When asserted, it requests the pump ON internally.

## Internal Pump Override

The power manager computes an effective relay mask before driving GPIOs:

- The accepted external mask comes from `VCU_MOBO_Command`.
- The pump bit is ORed with the internal coolant-pump request.
- The internal coolant-pump request is true when either:
  - inverter coolant temperature is above the configured latch threshold, or
  - `VCU_RTD_Active` is set.
- A startup lockout suppresses all internal pump requests for the first configured interval after boot.

Because of this, `Pump_Actual` can be ON while `Pump_Commanded` is 0.

## Safety and State Telemetry

The system state is published in heartbeat telemetry as `System_State`:

| Value | State | Meaning |
| --- | --- | --- |
| 0 | `INIT` | Boot and peripheral initialization in progress |
| 1 | `STANDBY` | Init complete, waiting for safety task handoff |
| 2 | `ACTIVE` | Normal runtime state |
| 3 | `FAULT` | Defined in the enum, but not entered by current code |
| 4 | `RECOVERY` | Defined in the enum, but not entered by current code |

Safety inputs are active-low in hardware, but the CAN telemetry is normalized so that a set bit means fault/asserted:

- GPIO high: CAN bit `0`
- GPIO low: CAN bit `1`

Safety status signals on `0x00200030`:

| Raw | Debounced | Latched |
| --- | --- | --- |
| `SDC1_Raw` | `SDC1_Debounced` | `SDC1_Latched` |
| `SDC2_Raw` | `SDC2_Debounced` | `SDC2_Latched` |
| `SDC3_Raw` | `SDC3_Debounced` | `SDC3_Latched` |
| `BMS_Raw` | `BMS_Debounced` | `BMS_Latched` |
| `BSPD_Raw` | `BSPD_Debounced` | `BSPD_Latched` |
| `IMD_Raw` | `IMD_Debounced` | `IMD_Latched` |

Operational impact:

- `Raw = 1` means the input is currently faulted/asserted.
- `Debounced = 1` means the input has been faulted for `SAFETY_DEBOUNCE_SAMPLES` consecutive task samples. The current settings are `3` samples at `10 ms`, so about `30 ms`.
- `Latched = 1` means the input faulted at some point since boot and remains remembered.
- Debounced bits set and clear the corresponding error flags in `error_manager`.
- There is currently no CAN command to clear the latched safety bits.
- In the current firmware, safety inputs do not block command acceptance and do not drive the state machine into `FAULT`.

## Control Confirmation and Feedback (MOBO to Bus)

### `MOBO_Heartbeat`

- CAN ID: `0x00200000`
- Period: `100 ms`

Signal names:

| Signal | Meaning |
| --- | --- |
| `System_State` | Current coarse system state |
| `Heartbeat_Counter` | Rolling heartbeat counter |
| `Fault_Count` | Count of newly set error bits over uptime |
| `Error_Summary` | 32-bit summary of current error flags |
| `Has_Warnings` | `1` when any warning bit is set, else `0` |

### `MOBO_Relay_Status`

- CAN ID: `0x00200040`
- Period: `100 ms`
- Primary relay-status frame.

Signal names:

| Signal | Meaning |
| --- | --- |
| `Pump_Commanded` | Last accepted external Pump command bit |
| `DRS_Commanded` | Last accepted external DRS command bit |
| `Fans_Commanded` | Last accepted external Fans command bit |
| `Radiator_Fans_Commanded` | Last accepted external Radiator Fans command bit |
| `Pump_Actual` | GPIO readback for Pump |
| `DRS_Actual` | GPIO readback for DRS |
| `Fans_Actual` | GPIO readback for Fans |
| `Radiator_Fans_Actual` | GPIO readback for Radiator Fans |
| `Pump_State` | Per-channel FSM state |
| `DRS_State` | Per-channel FSM state |
| `Fans_State` | Per-channel FSM state |
| `Radiator_Fans_State` | Per-channel FSM state |
| `Acc_Fans_Active` | `1` while Acc Fans alternation mode is engaged |
| `Acc_Fans_Phase` | `0` = DRS is the currently-driven channel, `1` = Fans is the currently-driven channel |
| `Ms_Since_Cmd` | Age of the last accepted external relay command |

Relay FSM values:

| Value | State |
| --- | --- |
| 0 | `OFF` |
| 1 | `TURNING_ON` |
| 2 | `ON` |
| 3 | `TURNING_OFF` |
| 4 | `FAULT` |

Notes:

- Bytes 2 and 3 pack the four relay FSM states as 4-bit fields.
- Byte 4 carries Acc Fans status: bit 0 = `Acc_Fans_Active`, bit 1 = `Acc_Fans_Phase`. The remaining bits in byte 4 are reserved.
- `Pump_Actual` and `Pump_State` reflect the effective output after internal coolant-pump override, while `Pump_Commanded` reflects only the last accepted external command bit.
- When `Acc_Fans_Active = 1`, `DRS_Actual` / `Fans_Actual` reflect the alternation rather than the VCU-supplied request bits. `DRS_Commanded` / `Fans_Commanded` continue to reflect the raw last-accepted VCU bits.

### `MOBO_Errors`

- CAN ID: `0x00200001`
- Period: `500 ms`

Signal names:

| Signal | Meaning |
| --- | --- |
| `Error_Flags` | Full 32-bit error bitmask |
| `Warning_Flags` | Full 32-bit warning bitmask |

### `MOBO_Safety_Status`

- CAN ID: `0x00200030`
- Period: `100 ms`
- Telemetry frame carrying raw, debounced, and latched safety-fault bits.

### `MOBO_CAN_Stats`

- CAN ID: `0x00200002`
- Period: `1000 ms`

Signal names:

| Signal | Meaning |
| --- | --- |
| `TX_Success` | Successful MOBO transmissions |
| `TX_Failures` | MOBO transmit failures |
| `RX_Messages` | Received messages accepted into the RX queue |
| `RX_Drops` | Dropped RX messages due to RX queue full |

### `MOBO_Power_Telemetry`

- CAN ID: `0x00200010`
- Period: `200 ms`

Signal names:

| Signal | Meaning |
| --- | --- |
| `Battery_Voltage` | Measured battery voltage |
| `FiveV_Sense` | Measured 5V rail |
| `BSE_PSI_Rear` | Rear brake pressure, encoded with `0.1 PSI / bit` |
| `LV_Current_Raw` | Raw LV current ADC count after the software averaging path |

Brake pressure notes:

- The brake sensor is converted from sensor voltage to PSI as `PSI = floor((mV - 500) * 3000 / 4000)` after reconstructing the pre-divider sensor voltage.
- Voltages below `250 mV` or above `4750 mV` are treated as out-of-range and publish `0 PSI`.
- Voltages inside the tolerance band but outside `500..4500 mV` clamp to `0..3000 PSI`.
- Any computed brake pressure below `10 PSI` is clamped to `0 PSI` before transmission.
- On the CAN wire, the transmitted raw value is `PSI * 10` to match the DBC scaling.

### `MOBO_Current_Telemetry`

- CAN ID: `0x00200020`
- Period: `100 ms`

Signal names:

| Signal | Meaning |
| --- | --- |
| `LV_Current` | Low-voltage current |
| `HC_Current` | High-current current |
| `LV_Current_Peak` | Running LV current peak |
| `HC_Current_Peak` | Running HC current peak |

## Practical Command Sequences

### Turn Pump and Fans ON from VCU

- Send `VCU_MOBO_Command` on `0x002001F0`
- Set `Enable = 1`
- Set `Pump_Request = 1`
- Set `Fans_Request = 1`
- Leave `DRS_Request = 0`, `Radiator_Fans_Request = 0`
- Confirm in `MOBO_Relay_Status`:
  - `Pump_Commanded = 1`
  - `Fans_Commanded = 1`

### Turn all channels OFF from VCU

- Send `VCU_MOBO_Command` on `0x002001F0`
- Set `Enable = 0`
- Confirm in `MOBO_Relay_Status` that the commanded bits return to `0`

### Update battery-divider calibration

- Send `MOBO_Config_Command` on `0x002001F8`
- Set `Param_ID = 0x03` to update `CONFIG_PARAM_VDIV_NUM`
- Set `Param_ID = 0x04` to update `CONFIG_PARAM_VDIV_DEN`
- Confirm the effect in `MOBO_Power_Telemetry` via `Battery_Voltage`

### Reset MOBO

- Send any frame on `0x002001F7`
- Payload contents do not matter

## Source of Truth

This document reflects the current firmware and generated DBC:

- `Core/Inc/can_ids.h`
- `Core/Src/can_manager.c`
- `Core/Src/power_manager.c`
- `Core/Src/coolant_pump.c`
- `Core/Inc/config_manager.h`
- `Core/Src/sensor_manager.c`
- `Core/Src/safety_monitor.c`
- `Core/Src/state_machine.c`
- `tools/generate_dbc.py`