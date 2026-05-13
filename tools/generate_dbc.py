"""Generate tools/Baby_MOBO.dbc for the rewritten MOBO firmware.

The CAN protocol mirrors Core/Inc/can_ids.h. All IDs are 29-bit extended.
Run from anywhere: `python tools/generate_dbc.py`.
"""

from pathlib import Path


# --- IDs (must mirror Core/Inc/can_ids.h) -----------------------------------
MOBO_HEARTBEAT_ID         = 0x00200000
MOBO_ERRORS_ID            = 0x00200001
MOBO_CAN_STATS_ID         = 0x00200002
MOBO_POWER_TELEMETRY_ID   = 0x00200010
MOBO_CURRENT_TELEMETRY_ID = 0x00200020
MOBO_SAFETY_STATUS_ID     = 0x00200030
MOBO_RELAY_STATUS_ID      = 0x00200040

MOBO_VCU_POWER_CMD_ID     = 0x002001F0
MOBO_RESET_CMD_ID         = 0x002001F7
MOBO_CONFIG_CMD_ID        = 0x002001F8


def ext(can_id: int) -> str:
    """DBC representation of a 29-bit extended ID."""
    return str(0x80000000 | can_id)


def msg(can_id: int, name: str, dlc: int, sender: str, signals: list[str]) -> list[str]:
    out = [f"BO_ {ext(can_id)} {name}: {dlc} {sender}"]
    out.extend(signals)
    out.append("")
    return out


# Signal helpers -----------------------------------------------------------
def sig(name: str, start: int, length: int, signed: bool, factor: float, offset: float,
        smin: float, smax: float, unit: str, receiver: str) -> str:
    sign = "-" if signed else "+"
    fmt = (f' SG_ {name} : {start}|{length}@1{sign} ({factor},{offset}) '
           f'[{smin}|{smax}] "{unit}" {receiver}')
    return fmt


def bit(name: str, start: int, receiver: str) -> str:
    return f' SG_ {name} : {start}|1@1+ (1,0) [0|1] "" {receiver}'


def build_dbc() -> str:
    HOST = "Host_Tool"
    VCU  = "VCU"
    MOBO = "Baby_MOBO"

    lines: list[str] = [
        'VERSION ""',
        "",
        "NS_ :",
        "    NS_DESC_",
        "    CM_",
        "    BA_DEF_",
        "    BA_",
        "    VAL_",
        "    CAT_DEF_",
        "    CAT_",
        "    FILTER",
        "    BA_DEF_DEF_",
        "    EV_DATA_",
        "    ENVVAR_DATA_",
        "    SGTYPE_",
        "    SGTYPE_VAL_",
        "    BA_DEF_SGTYPE_",
        "    BA_SGTYPE_",
        "    SIG_TYPE_REF_",
        "    VAL_TABLE_",
        "    SIG_GROUP_",
        "    SIG_VALTYPE_",
        "    SIGTYPE_VALTYPE_",
        "    BO_TX_BU_",
        "    BA_DEF_REL_",
        "    BA_REL_",
        "    BA_DEF_DEF_REL_",
        "    BU_SG_REL_",
        "    BU_EV_REL_",
        "    BU_BO_REL_",
        "    SG_MUL_VAL_",
        "",
        "BS_:",
        "",
        f"BU_: {MOBO} {HOST} {VCU}",
        "",
    ]

    # --- TX (MOBO -> bus) ---------------------------------------------------
    lines += msg(MOBO_HEARTBEAT_ID, "MOBO_Heartbeat", 8, MOBO, [
        sig("System_State",      0, 8, False, 1, 0, 0, 2,     "",  HOST),
        sig("Heartbeat_Counter", 8, 8, False, 1, 0, 0, 255,   "",  HOST),
        sig("Fault_Count",      16, 8, False, 1, 0, 0, 255,   "",  HOST),
        sig("Error_Summary",    24, 32, False, 1, 0, 0, 4294967295, "", HOST),
        sig("Has_Warnings",     56, 8, False, 1, 0, 0, 1,     "",  HOST),
    ])

    lines += msg(MOBO_ERRORS_ID, "MOBO_Errors", 8, MOBO, [
        sig("Error_Flags",   0, 32, False, 1, 0, 0, 4294967295, "", HOST),
        sig("Warning_Flags", 32, 32, False, 1, 0, 0, 4294967295, "", HOST),
    ])

    lines += msg(MOBO_CAN_STATS_ID, "MOBO_CAN_Stats", 8, MOBO, [
        sig("TX_Success",    0, 16, False, 1, 0, 0, 65535, "", HOST),
        sig("TX_Failures",  16, 16, False, 1, 0, 0, 65535, "", HOST),
        sig("RX_Messages",  32, 16, False, 1, 0, 0, 65535, "", HOST),
        sig("RX_Drops",     48, 16, False, 1, 0, 0, 65535, "", HOST),
    ])

    lines += msg(MOBO_POWER_TELEMETRY_ID, "MOBO_Power_Telemetry", 8, MOBO, [
        sig("Battery_Voltage", 0,  16, False, 0.001, 0, 0, 65.535, "V", HOST),
        sig("FiveV_Sense",    16,  16, False, 0.001, 0, 0, 65.535, "V", HOST),
        sig("Brake_Input",    32,  16, False, 0.001, 0, 0, 65.535, "V", HOST),
        sig("LV_Current_Raw", 48,  16, False, 1,     0, 0, 4095,   "count", HOST),
    ])

    lines += msg(MOBO_CURRENT_TELEMETRY_ID, "MOBO_Current_Telemetry", 8, MOBO, [
        sig("LV_Current",       0, 16, True, 0.001, 0, -32.768, 32.767, "A", HOST),
        sig("HC_Current",      16, 16, True, 0.001, 0, -32.768, 32.767, "A", HOST),
        sig("LV_Current_Peak", 32, 16, True, 0.001, 0, -32.768, 32.767, "A", HOST),
        sig("HC_Current_Peak", 48, 16, True, 0.001, 0, -32.768, 32.767, "A", HOST),
    ])

    lines += msg(MOBO_SAFETY_STATUS_ID, "MOBO_Safety_Status", 8, MOBO, [
        bit("SDC1_Raw",    0, HOST),
        bit("SDC2_Raw",    1, HOST),
        bit("SDC3_Raw",    2, HOST),
        bit("BMS_Raw",     3, HOST),
        bit("BSPD_Raw",    4, HOST),
        bit("IMD_Raw",     5, HOST),
        bit("SDC1_Debounced",  8, HOST),
        bit("SDC2_Debounced",  9, HOST),
        bit("SDC3_Debounced", 10, HOST),
        bit("BMS_Debounced",  11, HOST),
        bit("BSPD_Debounced", 12, HOST),
        bit("IMD_Debounced",  13, HOST),
        bit("SDC1_Latched",  16, HOST),
        bit("SDC2_Latched",  17, HOST),
        bit("SDC3_Latched",  18, HOST),
        bit("BMS_Latched",   19, HOST),
        bit("BSPD_Latched",  20, HOST),
        bit("IMD_Latched",   21, HOST),
    ])

    lines += msg(MOBO_RELAY_STATUS_ID, "MOBO_Relay_Status", 8, MOBO, [
        bit("Pump_Commanded", 0, HOST),
        bit("DRS_Commanded",  1, HOST),
        bit("Fans_Commanded", 2, HOST),
        bit("Rad_Commanded",  3, HOST),
        bit("Pump_Actual",    8, HOST),
        bit("DRS_Actual",     9, HOST),
        bit("Fans_Actual",   10, HOST),
        bit("Rad_Actual",    11, HOST),
        sig("Pump_State",     16, 4, False, 1, 0, 0, 4, "", HOST),
        sig("DRS_State",      20, 4, False, 1, 0, 0, 4, "", HOST),
        sig("Fans_State",     24, 4, False, 1, 0, 0, 4, "", HOST),
        sig("Rad_State",      28, 4, False, 1, 0, 0, 4, "", HOST),
        sig("Reserved_B4",    32, 8, False, 1, 0, 0, 255, "", HOST),
        sig("Ms_Since_Cmd",   40, 16, False, 1, 0, 0, 65535, "ms", HOST),
    ])

    # --- RX (commands) ------------------------------------------------------
    lines += msg(MOBO_VCU_POWER_CMD_ID, "VCU_MOBO_Command", 8, VCU, [
        bit("Enable",       0, MOBO),
        bit("Pump_Request", 8, MOBO),
        bit("DRS_Request",  9, MOBO),
        bit("Fans_Request",10, MOBO),
        bit("Rad_Request", 11, MOBO),
    ])

    lines += msg(MOBO_RESET_CMD_ID, "MOBO_Reset_Command", 8, HOST, [
        sig("Ignored", 0, 8, False, 1, 0, 0, 255, "", MOBO),
    ])

    lines += msg(MOBO_CONFIG_CMD_ID, "MOBO_Config_Command", 8, HOST, [
        sig("Param_ID", 0, 8, False, 1, 0, 0, 255, "", MOBO),
        sig("Value",    8, 32, True, 1, 0, -2147483648, 2147483647, "", MOBO),
    ])

    # --- Comments -----------------------------------------------------------
    lines += [
        f'CM_ BO_ {ext(MOBO_HEARTBEAT_ID)} "100 ms heartbeat. State (0=INIT,1=STANDBY,2=ACTIVE). MOBO never self-faults.";',
        f'CM_ BO_ {ext(MOBO_ERRORS_ID)} "Full 32-bit error and warning bitmasks (pure telemetry).";',
        f'CM_ BO_ {ext(MOBO_CAN_STATS_ID)} "CAN TX/RX counters (lower 16 bits).";',
        f'CM_ BO_ {ext(MOBO_POWER_TELEMETRY_ID)} "Battery, 5V rail, brake-input voltage, and raw LV current ADC count.";',
        f'CM_ BO_ {ext(MOBO_CURRENT_TELEMETRY_ID)} "LV and HC current with running peaks.";',
        f'CM_ BO_ {ext(MOBO_SAFETY_STATUS_ID)} "Safety inputs: raw, debounced, and latched (telemetry only).";',
        f'CM_ BO_ {ext(MOBO_RELAY_STATUS_ID)} "Commanded vs actual relay state with per-channel FSM.";',
        f'CM_ BO_ {ext(MOBO_VCU_POWER_CMD_ID)} "VCU relay command. Byte 0 bit 0 = enable; byte 1 bits 0..3 = relay mask. Only command source MOBO honors.";',
        f'CM_ BO_ {ext(MOBO_RESET_CMD_ID)} "System reset; any frame on this ID triggers a reset and payload is ignored.";',
        f'CM_ BO_ {ext(MOBO_CONFIG_CMD_ID)} "Runtime config write. Param_ID per CONFIG_PARAM_* in config_manager.h.";',
        "",
        # Cycle times
        'BA_DEF_ BO_  "GenMsgCycleTime" INT 0 65535;',
        f'BA_ "GenMsgCycleTime" BO_ {ext(MOBO_HEARTBEAT_ID)} 100;',
        f'BA_ "GenMsgCycleTime" BO_ {ext(MOBO_ERRORS_ID)} 500;',
        f'BA_ "GenMsgCycleTime" BO_ {ext(MOBO_CAN_STATS_ID)} 1000;',
        f'BA_ "GenMsgCycleTime" BO_ {ext(MOBO_POWER_TELEMETRY_ID)} 200;',
        f'BA_ "GenMsgCycleTime" BO_ {ext(MOBO_CURRENT_TELEMETRY_ID)} 100;',
        f'BA_ "GenMsgCycleTime" BO_ {ext(MOBO_SAFETY_STATUS_ID)} 100;',
        f'BA_ "GenMsgCycleTime" BO_ {ext(MOBO_RELAY_STATUS_ID)} 100;',
        "",
        # Value tables for state and relay-FSM enums
        f'VAL_ {ext(MOBO_HEARTBEAT_ID)} System_State 0 "INIT" 1 "STANDBY" 2 "ACTIVE";',
        f'VAL_ {ext(MOBO_RELAY_STATUS_ID)} Pump_State 0 "OFF" 1 "TURNING_ON" 2 "ON" 3 "TURNING_OFF" 4 "FAULT";',
        f'VAL_ {ext(MOBO_RELAY_STATUS_ID)} DRS_State  0 "OFF" 1 "TURNING_ON" 2 "ON" 3 "TURNING_OFF" 4 "FAULT";',
        f'VAL_ {ext(MOBO_RELAY_STATUS_ID)} Fans_State 0 "OFF" 1 "TURNING_ON" 2 "ON" 3 "TURNING_OFF" 4 "FAULT";',
        f'VAL_ {ext(MOBO_RELAY_STATUS_ID)} Rad_State  0 "OFF" 1 "TURNING_ON" 2 "ON" 3 "TURNING_OFF" 4 "FAULT";',
        "",
    ]

    return "\n".join(lines)


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    out_path = script_dir / "Baby_MOBO.dbc"
    out_path.write_text(build_dbc(), encoding="ascii")
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
