# MOBO Dashboard — Required Changes (Firmware Rewrite)

**Date:** 2026-05-13
**Firmware branch:** `complete_rewrite`
**DBC:** [tools/Baby_MOBO.dbc](../tools/Baby_MOBO.dbc) (regenerate with `python tools/generate_dbc.py`)

This document lists the behavioral changes shipped in the MOBO firmware that
the control-dashboard agent must adapt to. The high-level summary: **MOBO is
now a dumb relay + telemetry module.** It does not self-fault, does not force
itself into safe state, and only accepts commands from the VCU.

---

## 1. Removed CAN messages

The following IDs no longer exist on the bus. Remove any TX paths, RX
handlers, UI widgets, and DBC references for them.

| Old ID         | Name                   | Replacement |
|----------------|------------------------|-------------|
| `0x002001F1`   | `RPI_MOBO_Command`     | None — send all relay commands via `VCU_MOBO_Command` (`0x002001F0`) |
| `0x002001F2`   | `RPI_Override_Release` | None — override system removed entirely |

The dashboard should **no longer transmit either of the above frames** under
any circumstance. The firmware filter still accepts the MOBO ID prefix
`0x002001xx`, but the dispatcher will silently drop these IDs.

---

## 2. Changed message: `MOBO_Relay_Status` (`0x00200040`)

Byte 4 has been repurposed from `Authority` to reserved (always `0`).

### Old layout

| Byte | Field                                |
|------|--------------------------------------|
| 0    | Commanded mask                       |
| 1    | Actual mask                          |
| 2    | Pump_State (low nibble), DRS_State (high nibble) |
| 3    | Fans_State (low nibble), Radiator_Fans_State (high nibble) |
| 4    | **Authority** (0=NONE,1=VCU,2=RPI,3=FORCE_SAFE,4=BOOT_RESTORE) |
| 5–6  | Ms_Since_Cmd (uint16 LE)             |
| 7    | Reserved                             |

### New layout

| Byte | Field                                |
|------|--------------------------------------|
| 0    | Commanded mask                       |
| 1    | Actual mask                          |
| 2    | Pump_State / DRS_State               |
| 3    | Fans_State / Radiator_Fans_State     |
| 4    | **Reserved (always 0)**              |
| 5–6  | Ms_Since_Cmd (uint16 LE)             |
| 7    | Reserved                             |

**Dashboard action:** Remove the "Command authority" indicator. If you want
to still show *who last commanded*, the dashboard already knows — it is the
only command source.

---

## 3. Changed message: `MOBO_Heartbeat` (`0x00200000`)

Byte 0 (`System_State`) value range is now `{0, 1, 2}` only.

| Value | State    | Notes                                |
|-------|----------|--------------------------------------|
| 0     | INIT     | Booting; expect this very briefly    |
| 1     | STANDBY  | Init done, promoting to ACTIVE       |
| 2     | ACTIVE   | Normal operation                     |
| ~~3~~ | ~~FAULT~~     | **Removed** — MOBO no longer self-faults |
| ~~4~~ | ~~RECOVERY~~  | **Removed**                          |

**Dashboard action:** Any state-coloring logic that treated `FAULT` as
critical-red should now derive that information from `MOBO_Errors`
(`0x00200001`) error bitmask directly. Treat `System_State` purely as a
liveness indicator.

---

## 4. Removed warning bit

`WARNING_RPI_OVERRIDE_ACTIVE` (`1 << 25`) is gone. Bit 25 of the warning
flags in `MOBO_Errors` is now reserved (always 0).

**Dashboard action:** Remove any "RPI override active" badge.

---

## 5. Removed config parameter (still exposed, but no-op)

`CONFIG_PARAM_RPI_TIMEOUT_MS` (param ID `0x05`) is still accepted by
`MOBO_Config_Command` and still persists to flash for backward compatibility,
but it has no behavioral effect. You may remove its UI control.

---

## 6. Error/fault model — important behavior change

**MOBO no longer takes any action on its own errors.** Setting any bit in
the error bitmask used to:

1. Move the state machine to `FAULT`.
2. Force all relays OFF.
3. Block all incoming relay commands until errors cleared.

**None of that happens anymore.** Errors are pure telemetry. If a critical
condition (e.g. SDC open, BMS fault, overcurrent) is detected:

- Bits in `MOBO_Errors.Error_Flags` will set as before.
- `Fault_Count` in the heartbeat will increment as before.
- **Relays remain in whatever state the VCU last commanded.**

This means **the VCU is now solely responsible** for deciding what to do
when MOBO reports an error. The dashboard should:

- Continue showing the error flags prominently.
- **Stop assuming relays are off when errors are present** — read
  `MOBO_Relay_Status.Actual_Mask` for ground truth.

---

## 7. Things that did *not* change

- All TX message IDs/periods other than the byte-4 rework of
  `MOBO_Relay_Status`.
- `MOBO_Reset_Command` (`0x002001F7`) — still magic-byte-armed (`0x5A 0xA5`).
- `MOBO_Config_Command` (`0x002001F8`) — same param ID space.
- `VCU_MOBO_Command` (`0x002001F0`) payload layout (byte 0 bit 0 = enable,
  byte 1 bits 0..3 = relay mask).
- Relay mask bit order: bit 0 Pump, bit 1 DRS, bit 2 Fans, bit 3 Radiator Fans.
- Persistent relay state on power cycle (still restored from flash after
  boot once safety is clean).
- Auto-coolant-pump (firmware ORs the pump bit in when `INV_Coolant_Temp ≥
  30 °C` while RTD active, regardless of VCU command).

---

## 8. Action checklist for the dashboard agent

- [ ] Delete `RPI_MOBO_Command` and `RPI_Override_Release` TX paths.
- [ ] Delete "RPI override active" UI badge.
- [ ] Delete authority indicator widget on the relay status panel.
- [ ] Restrict `System_State` legend to INIT/STANDBY/ACTIVE.
- [ ] Update DBC ingestion to the regenerated [Baby_MOBO.dbc](../tools/Baby_MOBO.dbc).
- [ ] Re-validate any "system OK" composite indicator — it should now be
      driven by `(error_flags == 0)` rather than `(state != FAULT)`.
- [ ] If the dashboard previously implemented an "emergency stop" by
      asserting an RPI override, replace that with a `VCU_MOBO_Command`
      frame carrying `enable = 1, mask = 0` (or routed via the VCU).
