from pathlib import Path


def dbc_extended_id(can_id: int) -> int:
    return 0x80000000 | can_id


def fmt_extended_id(can_id: int) -> str:
    return str(dbc_extended_id(can_id))


def build_dbc() -> str:
    mobo_summary_id = 0x002001F0
    mobo_power_info_id = 0x002000EE
    mobo_lc_summary_id = 0x002002F0
    mobo_hc_summary_id = 0x002003F0
    reset_cmd_id = 0x004001F7

    lines = [
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
        "BU_: Baby_MOBO Host_Tool",
        "",
        f"BO_ {fmt_extended_id(mobo_summary_id)} MOBO_Summary: 8 Baby_MOBO",
        ' SG_ Heartbeat_Toggle : 0|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ SDC_1 : 8|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ SDC_2 : 9|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ SDC_3 : 10|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ BMS : 11|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ BSPD : 12|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ IMD : 13|1@1+ (1,0) [0|1] "" Host_Tool',
        "",
        f"BO_ {fmt_extended_id(mobo_power_info_id)} MOBO_Power_Info: 8 Baby_MOBO",
        ' SG_ Battery_Voltage : 0|16@1+ (0.001,0) [0|65.535] "V" Host_Tool',
        "",
        f"BO_ {fmt_extended_id(mobo_lc_summary_id)} MOBO_LC_Summary: 8 Baby_MOBO",
        ' SG_ FiveV_Sense : 0|16@1+ (0.001,0) [0|65.535] "V" Host_Tool',
        ' SG_ Brake_Input : 16|16@1+ (0.001,0) [0|65.535] "V" Host_Tool',
        ' SG_ LV_Current : 32|16@1- (0.001,0) [-32.768|32.767] "A" Host_Tool',
        "",
        f"BO_ {fmt_extended_id(mobo_hc_summary_id)} MOBO_HC_Summary: 8 Baby_MOBO",
        ' SG_ HC_Current : 0|16@1- (0.001,0) [-32.768|32.767] "A" Host_Tool',
        ' SG_ Pump_Cmd : 16|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ DRS_Cmd : 17|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ Fans_Cmd : 18|1@1+ (1,0) [0|1] "" Host_Tool',
        ' SG_ Rad_Cmd : 19|1@1+ (1,0) [0|1] "" Host_Tool',
        "",
        f"BO_ {fmt_extended_id(reset_cmd_id)} CAN_Reset_Command: 8 Host_Tool",
        ' SG_ Reset_Command_Raw : 0|64@1+ (1,0) [0|18446744073709551615] "" Baby_MOBO',
        "",
        "CM_ BO_ "
        + fmt_extended_id(mobo_summary_id)
        + ' "Periodic MOBO summary frame from CAN_MOBO_Summary(); only heartbeat toggle in byte 0 is currently active.";',
        "CM_ BO_ "
        + fmt_extended_id(mobo_power_info_id)
        + ' "Battery sense reading from ADC1_IN5 with only bytes 0-1 used for battery voltage.";',
        "CM_ BO_ "
        + fmt_extended_id(mobo_lc_summary_id)
        + ' "Low-current summary from ADC1_IN7, ADC1_IN6, and ADC1_IN8 with only FiveV, Brake, and LV current bytes active.";',
        "CM_ BO_ "
        + fmt_extended_id(mobo_hc_summary_id)
        + ' "High-current summary from ADC1_IN11 with current in bytes 0-1 and output command bits in byte 2.";',
        "CM_ BO_ "
        + fmt_extended_id(reset_cmd_id)
        + ' "Send this extended ID to trigger NVIC_SystemReset() in CAN_ProcessRXQueue().";',
        "",
        'VAL_ '
        + fmt_extended_id(mobo_summary_id)
        + " Heartbeat_Toggle 0 \"Off\" 1 \"On\";",
        'VAL_ '
        + fmt_extended_id(mobo_summary_id)
        + " SDC_1 0 \"Inactive\" 1 \"Active\";",
        'VAL_ '
        + fmt_extended_id(mobo_summary_id)
        + " SDC_2 0 \"Inactive\" 1 \"Active\";",
        'VAL_ '
        + fmt_extended_id(mobo_summary_id)
        + " SDC_3 0 \"Inactive\" 1 \"Active\";",
        'VAL_ '
        + fmt_extended_id(mobo_summary_id)
        + " BMS 0 \"Inactive\" 1 \"Active\";",
        'VAL_ '
        + fmt_extended_id(mobo_summary_id)
        + " BSPD 0 \"Inactive\" 1 \"Active\";",
        'VAL_ '
        + fmt_extended_id(mobo_summary_id)
        + " IMD 0 \"Inactive\" 1 \"Active\";",
        'VAL_ '
        + fmt_extended_id(mobo_hc_summary_id)
        + " Pump_Cmd 0 \"Off\" 1 \"On\";",
        'VAL_ '
        + fmt_extended_id(mobo_hc_summary_id)
        + " DRS_Cmd 0 \"Off\" 1 \"On\";",
        'VAL_ '
        + fmt_extended_id(mobo_hc_summary_id)
        + " Fans_Cmd 0 \"Off\" 1 \"On\";",
        'VAL_ '
        + fmt_extended_id(mobo_hc_summary_id)
        + " Rad_Cmd 0 \"Off\" 1 \"On\";",
        "",
        "BA_DEF_ BO_  \"GenMsgCycleTime\" INT 0 65535;",
        "BA_ \"GenMsgCycleTime\" BO_ " + fmt_extended_id(mobo_summary_id) + " 1000;",
        "BA_ \"GenMsgCycleTime\" BO_ " + fmt_extended_id(mobo_power_info_id) + " 1000;",
        "BA_ \"GenMsgCycleTime\" BO_ " + fmt_extended_id(mobo_lc_summary_id) + " 1000;",
        "BA_ \"GenMsgCycleTime\" BO_ " + fmt_extended_id(mobo_hc_summary_id) + " 1000;",
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
