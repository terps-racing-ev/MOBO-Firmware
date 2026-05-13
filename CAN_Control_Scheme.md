# MOBO CAN Command and Control Scheme

This document describes how the MOBO firmware exposes control over CAN, what the default power-up behavior is, and which status messages confirm the current state.

All MOBO CAN traffic uses 29-bit extended identifiers under the `0x00200xxx` prefix.

## Default Power-Up Behavior

- On boot, all four controlled outputs are forced OFF:
  - `Pump`
  - `DRS`
  - `Fans`
  - `Radiator`
- The system state starts in `INIT`, then moves to `STANDBY` once firmware initialization completes.
- The system reaches `ACTIVE` only after all safety inputs are debounced clean.
- If a nonzero relay mask was previously saved to flash, that mask is applied once after the system first reaches `ACTIVE`.
- External CAN relay commands are accepted whenever the system is not in `FAULT` and the current authority rules allow them. `ACTIVE` is required for automatic boot-restore, not for ordinary VCU or RPI commands.
- Any critical error forces the state to `FAULT` and forces all outputs OFF.
- When all errors clear, the state moves to `RECOVERY`, then returns to `ACTIVE` once safety inputs are clean again.

## Controlled Outputs

Relay/control bit layout is shared by command messages, persisted boot-restore state, and relay-status telemetry:

| Bit | Channel |
| --- | --- |
| 0 | `Pump` |
| 1 | `DRS` |
| 2 | `Fans` |
| 3 | `Radiator` |

## Command Inputs (Bus to MOBO)

### `VCU_MOBO_Command`

- CAN ID: `0x002001F0`
- Direction: `VCU -> MOBO`
- Purpose: Normal relay control from the VCU.

Signal names:

| Signal | Meaning |
| --- | --- |
| `Enable` | Master enable for the requested mask |
| `Pump_Request` | Request Pump ON |
| `DRS_Request` | Request DRS ON |
| `Fans_Request` | Request Fans ON |
| `Rad_Request` | Request Radiator ON |

Behavior:

- If `Enable = 0`, MOBO forces the requested mask to `0x0` and turns all channels OFF.
- If `Enable = 1`, byte 1 bits 0..3 are applied as the requested mask.
- VCU commands are rejected when RPI override is active.
- VCU commands are also rejected in `FAULT` unless the requested mask is zero.
- Accepted VCU commands become the current command authority and are persisted to flash after a 500 ms debounce window.

### `RPI_MOBO_Command`

- CAN ID: `0x002001F1`
- Direction: `RPI -> MOBO`
- Purpose: Relay control from the Raspberry Pi, with optional authority override.

Signal names:

| Signal | Meaning |
| --- | --- |
| `Enable` | Master enable for the requested mask |
| `Override_Request` | Latch RPI as control authority |
| `Pump_Request` | Request Pump ON |
| `DRS_Request` | Request DRS ON |
| `Fans_Request` | Request Fans ON |
| `Rad_Request` | Request Radiator ON |

Behavior:

- If `Override_Request = 1`, MOBO asserts RPI override immediately.
- While override is active, VCU relay commands are ignored.
- If `Enable = 0`, MOBO forces the requested mask to `0x0` and turns all channels OFF.
- Every valid RPI command refreshes the internal RPI last-seen timestamp.
- Accepted RPI commands become the current command authority and are persisted to flash after a 500 ms debounce window.

### `RPI_Override_Release`

- CAN ID: `0x002001F2`
- Direction: `RPI -> MOBO`
- Purpose: Explicitly release RPI override authority.

Signal names:

| Signal | Meaning |
| --- | --- |
| `Magic` | Must be `0xA5` to release override |

Behavior:

- If `Magic = 0xA5`, MOBO clears RPI override.
- If override is not explicitly released, it also clears automatically after `RPI timeout` milliseconds of RPI silence.
- The default RPI timeout is `5000 ms`.

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
| `0x05` | `CONFIG_PARAM_RPI_TIMEOUT_MS` | RPI override timeout in milliseconds |

Behavior:

- MOBO applies the parameter update in RAM and persists it to flash.
- A bad `Param_ID` is rejected.
- The saved relay mask is not written through this command; it is managed automatically by the power manager after accepted VCU or RPI relay commands.

## Authority and Arbitration Rules

The relay authority reported by MOBO uses these values:

| Value | Authority |
| --- | --- |
| 0 | `NONE` |
| 1 | `VCU` |
| 2 | `RPI` |
| 3 | `FORCE_SAFE` |
| 4 | `BOOT_RESTORE` |

Rules:

- `VCU` is the normal authority when VCU commands are accepted.
- `RPI` becomes authority when RPI commands are accepted.
- `FORCE_SAFE` is set internally when the state machine enters `FAULT` and all relays are driven OFF.
- `BOOT_RESTORE` is set internally when the persisted relay mask is restored after first reaching `ACTIVE`.
- If RPI override is active, VCU commands are ignored until override is released or times out.

## Safety and State Preconditions

The system state is published in heartbeat telemetry as `System_State`:

| Value | State | Meaning |
| --- | --- | --- |
| 0 | `INIT` | Boot and peripheral initialization in progress |
| 1 | `STANDBY` | Init complete, waiting for clean safety inputs |
| 2 | `ACTIVE` | Normal operation, commands accepted |
| 3 | `FAULT` | Critical error latched, outputs forced OFF |
| 4 | `RECOVERY` | Errors cleared, waiting to re-arm |

Safety inputs are active-low in hardware. A low GPIO means the input is faulted/asserted.

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

- `STANDBY -> ACTIVE` happens only when all debounced safety bits are clear.
- `FAULT` is entered when a critical error bit is set.
- In `FAULT`, nonzero external relay commands are rejected.

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
| `Has_Warnings` | Nonzero when any warning bit is set |

### `MOBO_Relay_Status`

- CAN ID: `0x00200040`
- Period: `100 ms`
- Primary status frame for control confirmation.

Signal names:

| Signal | Meaning |
| --- | --- |
| `Pump_Commanded` | Last accepted commanded Pump state |
| `DRS_Commanded` | Last accepted commanded DRS state |
| `Fans_Commanded` | Last accepted commanded Fans state |
| `Rad_Commanded` | Last accepted commanded Radiator state |
| `Pump_Actual` | GPIO readback for Pump |
| `DRS_Actual` | GPIO readback for DRS |
| `Fans_Actual` | GPIO readback for Fans |
| `Rad_Actual` | GPIO readback for Radiator |
| `Pump_State` | Per-channel FSM state |
| `DRS_State` | Per-channel FSM state |
| `Fans_State` | Per-channel FSM state |
| `Rad_State` | Per-channel FSM state |
| `Authority` | Current accepted control authority |
| `Ms_Since_Cmd` | Age of the last accepted relay command |

Relay FSM values:

| Value | State |
| --- | --- |
| 0 | `OFF` |
| 1 | `TURNING_ON` |
| 2 | `ON` |
| 3 | `TURNING_OFF` |
| 4 | `FAULT` |

### `MOBO_Errors`

- CAN ID: `0x00200001`
- Period: `500 ms`

Signal names:

| Signal | Meaning |
| --- | --- |
| `Error_Flags` | Full 32-bit error bitmask |
| `Warning_Flags` | Full 32-bit warning bitmask |

Relevant control-related warning:

- `WARNING_RPI_OVERRIDE_ACTIVE` is set while RPI override is latched.

### `MOBO_Safety_Status`

- CAN ID: `0x00200030`
- Period: `100 ms`
- Confirms whether the system is allowed to enter or remain in `ACTIVE`.

### `MOBO_CAN_Stats`

- CAN ID: `0x00200002`
- Period: `1000 ms`

Signal names:

| Signal | Meaning |
| --- | --- |
| `TX_Success` | Successful MOBO transmissions |
| `TX_Failures` | MOBO transmit failures |
| `RX_Messages` | Received MOBO-directed messages |
| `RX_Drops` | Dropped RX messages |

### `MOBO_Power_Telemetry`

- CAN ID: `0x00200010`
- Period: `200 ms`

Signal names:

| Signal | Meaning |
| --- | --- |
| `Battery_Voltage` | Measured battery voltage |
| `FiveV_Sense` | Measured 5V rail |
| `Brake_Input` | Measured brake input voltage |

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
- Leave `DRS_Request = 0`, `Rad_Request = 0`
- Confirm acceptance in `MOBO_Relay_Status`:
  - `Pump_Commanded = 1`
  - `Fans_Commanded = 1`
  - `Authority = VCU`

### Take control from the RPI

- Send `RPI_MOBO_Command` on `0x002001F1`
- Set `Enable = 1`
- Set `Override_Request = 1`
- Set the requested relay bits
- Confirm in `MOBO_Relay_Status` that `Authority = RPI`
- Confirm in `MOBO_Errors` that `WARNING_RPI_OVERRIDE_ACTIVE` is set

### Release RPI override

- Send `RPI_Override_Release` on `0x002001F2`
- Set `Magic = 0xA5`
- Or stop sending RPI messages until the configured RPI timeout expires

### Reset MOBO

- Send any frame on `0x002001F7`
- Payload contents do not matter

## Source of Truth

This document reflects the current firmware and generated DBC:

- `Core/Inc/can_ids.h`
- `Core/Src/power_manager.c`
- `Core/Inc/config_manager.h`
- `Core/Src/safety_monitor.c`
- `Core/Inc/state_machine.h`
- `tools/generate_dbc.py`