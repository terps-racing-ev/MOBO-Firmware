# MOBO Firmware — Agent Instructions

**Baby MOBO** (Modular On-Board) is the power distribution and relay controller for a Formula SAE electric vehicle. STM32L432KCUx (256 KB flash, 64 KB RAM), FreeRTOS v10.3.1 with CMSIS-RTOS V2 wrappers, dual-bank OTA via a separate bootloader.

This firmware was rewritten from scratch on the `complete_rewrite` branch. The architecture is modeled on the BMS-Firmware-RTOS pattern (queue-based CAN manager + per-domain tasks + mutex-protected shared state).

---

## Build

```powershell
# Both bank A and bank B images
make -f dual_build.mk

# Single bank
make TARGET=Baby_MOBO_a LDSCRIPT=STM32L432XX_APP_BANK_A.ld

# Clean (use PowerShell, not the Makefile clean target)
if (Test-Path build) { Remove-Item -Recurse -Force build }
```

Outputs land in `build/debug/Baby_MOBO_a.{bin,hex,elf}` and `build/debug/Baby_MOBO_b.{bin,hex,elf}`.

### Dual-bank flash layout

| Region        | Address range          | Notes                              |
|---------------|------------------------|------------------------------------|
| Bootloader    | `0x08000000`–`0x08007FFF` | Owned by separate repo            |
| App Bank A    | `0x08008000`–`0x08021FFF` | `VECT_TAB_OFFSET=0x8000`          |
| App Bank B    | `0x08022000`–`0x0803BFFF` | `VECT_TAB_OFFSET=0x22000`         |
| Bootloader scratch | `0x0803C000`–`0x0803F7FF` | Reserved                       |
| **Persistent config** | `0x0803F800`–`0x0803FFFF` | Page 127 — owned by `config_manager` |

The config page is outside both bank linker scripts so firmware updates cannot clobber persisted settings (last commanded relay mask, current-sensor calibration, RPI authority timeout).

---

## Architecture

### Tasks

| Task        | Priority      | Period   | File                                          |
|-------------|---------------|----------|-----------------------------------------------|
| `Watchdog`  | RealTime      | 250 ms   | [Core/Src/watchdog.c](Core/Src/watchdog.c)    |
| `CAN_Mgr`   | High          | 10 ms    | [Core/Src/can_manager.c](Core/Src/can_manager.c) |
| `SafetyMon` | High          | 10 ms    | [Core/Src/safety_monitor.c](Core/Src/safety_monitor.c) |
| `PowerMgr`  | AboveNormal   | 20 ms    | [Core/Src/power_manager.c](Core/Src/power_manager.c) |
| `SensorMgr` | Normal        | 50 ms    | [Core/Src/sensor_manager.c](Core/Src/sensor_manager.c) |
| `LED_Blink` | Normal (CubeMX) | 500 ms | LD3 heartbeat, in [Core/Src/main.c](Core/Src/main.c) |
| `defaultTask`/`can_messages` | Normal (CubeMX) | sleeps | Inert; retained for ABI |

The two CubeMX-generated worker tasks (`defaultTask`, `can_messages`) are now sleep-only stubs because their definitions live outside the `USER CODE` markers and would be overwritten on `.ioc` regeneration.

### Module layering

```
                    ┌──────────────────────────────┐
                    │  CAN bus (29-bit extended)   │
                    └───────────────┬──────────────┘
                                    │
                    ┌───────────────▼──────────────┐
                    │  can_manager (TX/RX queues + │
                    │  static dispatch table)      │
                    └─┬───────────┬───────────┬────┘
        VCU/RPI cmds  │           │ telemetry │ reset/config
                      ▼           │           ▼
              ┌──────────────┐    │     ┌──────────┐
              │ power_manager│    │     │ config_  │
              │ (FSM+auth.)  │    │     │ manager  │
              └──┬───────────┘    │     └────┬─────┘
                 │ ForceSafe       │           │
                 ▼                 │           │
         ┌───────────────┐         │           │
         │ state_machine │◀────────┴────┬──────┘
         └──────┬────────┘              │
                │ EnterFault            │
                ▼                       │
         ┌───────────────┐              │
         │ error_manager │◀─────────────┴────────────┐
         └───────────────┘                           │
                                                     │
   ┌────────────────────┐    ┌─────────────────┐    │
   │ sensor_manager     │    │ safety_monitor  │────┘
   │ (ADC1, calibration)│    │ (SDC/BMS/BSPD/  │
   └────────────────────┘    │  IMD debounce)  │
                             └─────────────────┘
```

### Key invariants

- **`can_manager` owns CAN1 exclusively.** All TX goes through `CAN_SendMessage()` (priority queue, 64 slots). RX is fed from `HAL_CAN_RxFifo0MsgPendingCallback` into a 32-slot queue and dispatched via the static `g_dispatch[]` table in [Core/Src/can_manager.c](Core/Src/can_manager.c). The hardware filter only accepts IDs matching `MOBO_BASE_ID/MOBO_BASE_MASK` so foreign traffic never reaches the CPU.
- **`power_manager` owns the four relay GPIOs.** No other module writes them. State transitions go through `Power_RequestChannel` → settle window → confirmed `RELAY_ON`/`RELAY_OFF` (or `RELAY_FAULT` → `ERROR_RELAY_FAULT`).
- **`sensor_manager` owns ADC1.** It caches the active channel so back-to-back same-channel reads skip reconfigure; current channels are 16-sample averaged with calibration offset and noise floor sourced from `config_manager`.
- **`state_machine` `FAULT` is sticky.** `ErrorMgr_SetError()` of any bit in `CRITICAL_ERROR_MASK` calls `StateMachine_EnterFault()` → `PowerMgr_ForceSafe()` (all relays off). `FAULT` clears only when `error_flags` reaches zero.
- **All shared state is mutex-protected** with `osMutexAcquire(..., osWaitForever)` / `osMutexRelease`. Each module owns its own mutex.

### Persistent state (relay-state-after-power-cycle bug fix)

The previous firmware lost relay state on every reset. The new flow:

1. `power_manager` receives a relay command, applies it to the GPIO immediately.
2. After a 500 ms debounce window the new mask is written to `0x0803F800` via `Config_SaveDesiredRelayMask()` (CRC-checked, double-word programmed).
3. On next boot, `Config_Init()` validates magic + CRC and exposes the saved mask via `Config_GetDesiredRelayMask()`.
4. `PowerMgrTask` waits until `safety_monitor` promotes the system to `MOBO_STATE_ACTIVE`, then applies the saved mask once with `current_authority = PWR_SRC_BOOT_RESTORE`.

### RPI authority arbitration (lockout bug fix)

The previous firmware had no way to release RPI override once latched. The new flow:

- VCU commands are accepted only while `rpi_override_active == 0`.
- An RPI command with byte 0 bit 1 set asserts override (`PowerMgr_AssertRpiOverride`).
- Override clears on **either** an explicit release frame (`MOBO_RPI_OVERRIDE_REL_ID` with byte 0 = `0xA5`), **or** RPI silence exceeding `Config_GetRpiTimeoutMs()` (default 5000 ms, runtime-tunable via `MOBO_CONFIG_CMD_ID`).
- `WARNING_RPI_OVERRIDE_ACTIVE` is set/cleared in lockstep with the override flag.

---

## CAN Protocol

All MOBO IDs are 29-bit extended under the `0x00200xxx` prefix. Definitions live in [Core/Inc/can_ids.h](Core/Inc/can_ids.h). The hardware filter accepts only this prefix; everything else is rejected at the CAN peripheral.

### TX (MOBO → bus)

| Message              | ID           | Period | Payload summary                                               |
|----------------------|--------------|--------|---------------------------------------------------------------|
| `MOBO_Heartbeat`     | `0x00200000` | 100 ms | State, heartbeat counter, fault count, error summary, warnings flag |
| `MOBO_Errors`        | `0x00200001` | 500 ms | Full 32-bit error + 32-bit warning bitmasks                   |
| `MOBO_CAN_Stats`     | `0x00200002` | 1 s    | TX OK / TX fail / RX OK / RX drop counts (lower 16 bits)      |
| `MOBO_Power_Telemetry` | `0x00200010` | 200 ms | Battery, 5V rail, brake input (mV)                          |
| `MOBO_Current_Telemetry` | `0x00200020` | 100 ms | LV + HC current and running peaks (mA, signed)            |
| `MOBO_Safety_Status` | `0x00200030` | 100 ms | SDC1/2/3, BMS, BSPD, IMD — raw, debounced, latched           |
| `MOBO_Relay_Status`  | `0x00200040` | 100 ms | Commanded mask, actual mask, per-channel FSM, authority, ms-since-cmd |

### RX (bus → MOBO)

| Message                | ID           | Payload                                                       |
|------------------------|--------------|---------------------------------------------------------------|
| `VCU_MOBO_Command`     | `0x002001F0` | Byte 0 bit 0 = enable; byte 1 bits 0..3 = relay mask          |
| `RPI_MOBO_Command`     | `0x002001F1` | Byte 0 bit 0 = enable, bit 1 = override; byte 1 = relay mask  |
| `RPI_Override_Release` | `0x002001F2` | Byte 0 must be `0xA5`                                         |
| `MOBO_Reset_Command`   | `0x002001F7` | Bytes 0,1 must be `0x5A`,`0xA5`                               |
| `MOBO_Config_Command`  | `0x002001F8` | Byte 0 = param ID (`CONFIG_PARAM_*`); bytes 1..4 = int32 LE   |

### Relay mask bit layout (used in both VCU/RPI command byte 1 and the persisted state)

| Bit | Channel  |
|-----|----------|
| 0   | Pump     |
| 1   | DRS      |
| 2   | Fans     |
| 3   | Radiator |

### State machine values (`System_State` field)

| Value | State        | Meaning                                              |
|-------|--------------|------------------------------------------------------|
| 0     | `INIT`       | Boot, peripherals coming up                          |
| 1     | `STANDBY`    | Init complete, waiting for clean safety inputs       |
| 2     | `ACTIVE`     | Normal operation, commands accepted                  |
| 3     | `FAULT`      | Critical error latched; relays forced off            |
| 4     | `RECOVERY`   | Errors cleared, returning to ACTIVE on next clean tick |

### Error / warning bit layout

32-bit space partitioned by category. See [Core/Inc/error_manager.h](Core/Inc/error_manager.h) for the full list.

| Byte | Category          | Examples                                              |
|------|-------------------|-------------------------------------------------------|
| 0    | Power / Relay     | `ERROR_RELAY_FAULT`, `ERROR_OVER_VOLTAGE`             |
| 1    | Current / Sensor  | `ERROR_LV_OVERCURRENT`, `ERROR_ADC_FAULT`             |
| 2    | Safety            | `ERROR_SDC1_OPEN`, `ERROR_BMS_FAULT`, `ERROR_IMD_FAULT` |
| 3    | Comm / System     | `ERROR_CAN_BUS_OFF`, `ERROR_WATCHDOG`, `ERROR_FLASH_FAULT` |

`CRITICAL_ERROR_MASK` is the OR of every bit that latches `StateMachine_EnterFault`.

---

## Conventions

- **C only**, `arm-none-eabi-gcc`, `-Wall -Og`. Build is **warning-free** as of last commit.
- **Naming**: `ModulePrefix_FunctionName()` (PascalCase, module-prefixed). Types `_t` suffixed. Macros `UPPER_SNAKE_CASE`. Globals `g_` prefix. File names `module_name.{c,h}`.
- **Headers**: `#ifndef __HEADER_H` guards, `extern "C"` wrap, full Doxygen on public API.
- **CubeMX-managed files** ([Core/Src/main.c](Core/Src/main.c), [Core/Src/freertos.c](Core/Src/freertos.c), [Core/Src/stm32l4xx_it.c](Core/Src/stm32l4xx_it.c), [Core/Src/stm32l4xx_hal_msp.c](Core/Src/stm32l4xx_hal_msp.c)) only edit inside `/* USER CODE BEGIN */` … `/* USER CODE END */` markers. Anything outside is wiped on `.ioc` regeneration.
- **No printf, no UART** — observability is via CAN telemetry frames.
- **No dynamic allocation outside task creation.** FreeRTOS heap is shared; queues and threads are created once at boot.
- **Mutexes always use `osWaitForever`.** Never hold one across a flash write or other long operation; copy out first, release, then act.
- **Lock order**: external module mutex → `error_manager` mutex. Never set errors while holding `power_manager` or `config_manager` mutexes (those modules can be invoked from inside `StateMachine_EnterFault`).

---

## Adding a new CAN message

1. Add the ID constant to [Core/Inc/can_ids.h](Core/Inc/can_ids.h) (must keep the `0x00200xxx` prefix).
2. **TX**: write a builder in [Core/Src/can_manager.c](Core/Src/can_manager.c) and add a `tick_at(...)` line in `CAN_ManagerTask`. **RX**: write a `bool Mod_MatchX(const CAN_Message_t *)` and `void Mod_HandleX(const CAN_Message_t *)` pair in the owning module, then add an entry to `g_dispatch[]`.
3. Update the DBC source ([tools/generate_dbc.py](tools/generate_dbc.py)) and regenerate: `python tools/generate_dbc.py`.

## Adding a new persistent config field

1. Add the field to `Config_Record_t` in [Core/Inc/config_manager.h](Core/Inc/config_manager.h) (append at end; do not reorder).
2. Add a default constant and assign it in `Config_LoadDefaults()`.
3. Add a getter and a `CONFIG_PARAM_*` ID, then handle it in `Config_SetParam()`.
4. Either bump `CONFIG_MAGIC` to invalidate old records, or accept that field-level fall-through to the new default is fine on first boot.

---

## Key Files

| File | Purpose |
|------|---------|
| [Core/Inc/can_ids.h](Core/Inc/can_ids.h) | All CAN message IDs and command magic bytes |
| [Core/Inc/can_manager.h](Core/Inc/can_manager.h) / [.c](Core/Src/can_manager.c) | CAN1 owner, TX/RX queues, dispatch table, telemetry |
| [Core/Inc/power_manager.h](Core/Inc/power_manager.h) / [.c](Core/Src/power_manager.c) | Relay FSM, authority arbitration, boot restore |
| [Core/Inc/sensor_manager.h](Core/Inc/sensor_manager.h) / [.c](Core/Src/sensor_manager.c) | ADC1 owner, calibration, peaks |
| [Core/Inc/safety_monitor.h](Core/Inc/safety_monitor.h) / [.c](Core/Src/safety_monitor.c) | SDC/BMS/BSPD/IMD debounce, error mapping, ACTIVE promotion |
| [Core/Inc/state_machine.h](Core/Inc/state_machine.h) / [.c](Core/Src/state_machine.c) | System state with sticky FAULT |
| [Core/Inc/error_manager.h](Core/Inc/error_manager.h) / [.c](Core/Src/error_manager.c) | 32-bit error + 32-bit warning bitmasks |
| [Core/Inc/config_manager.h](Core/Inc/config_manager.h) / [.c](Core/Src/config_manager.c) | Flash-backed config (page 127) |
| [Core/Inc/watchdog.h](Core/Inc/watchdog.h) / [.c](Core/Src/watchdog.c) | IWDG with per-task heartbeats |
| [Core/Src/main.c](Core/Src/main.c) | CubeMX shell + module init in USER blocks |
| [tools/generate_dbc.py](tools/generate_dbc.py) | Source-of-truth for the DBC |
| [tools/Baby_MOBO.dbc](tools/Baby_MOBO.dbc) | Generated DBC (do not edit by hand) |
| [Makefile](Makefile) / [STM32Make.make](STM32Make.make) | Build rules (single bank) |
| [dual_build.mk](dual_build.mk) | Builds bank A + bank B in sequence |
