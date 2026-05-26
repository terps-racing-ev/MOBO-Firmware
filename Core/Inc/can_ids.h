/**
  ******************************************************************************
  * @file    can_ids.h
  * @brief   MOBO CAN message ID definitions
  *
  * All MOBO frames are 29-bit extended IDs under the base 0x002000XX..0x002001XX.
  * - 0x00200000..0x00200FFF: MOBO-originated telemetry/status (TX)
  * - 0x002001F0..0x002001FF: MOBO-bound commands (RX)
  ******************************************************************************
  */

#ifndef __CAN_IDS_H
#define __CAN_IDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* MOBO base prefix and mask -------------------------------------------------*/
/* All MOBO IDs share the high bits 0x00200xxx; the hardware filter accepts
 * only this prefix so foreign traffic never reaches the RX queue. */
#define MOBO_BASE_ID       0x00200000U  /**< Common high bits */
#define MOBO_BASE_MASK     0x1FFFF000U  /**< Mask covering everything except low 12 bits */

/* TX: telemetry / status (MOBO -> bus) -------------------------------------*/
#define MOBO_HEARTBEAT_ID          0x00200000U  /**< 100 ms : state, fault count, error summary */
#define MOBO_ERRORS_ID             0x00200001U  /**< 500 ms / on change : full error+warning bitmasks */
#define MOBO_CAN_STATS_ID          0x00200002U  /**< 1000 ms : tx/rx counters */
#define MOBO_POWER_TELEMETRY_ID    0x00200010U  /**< 200 ms : battery, 5V rail, brake */
#define MOBO_CURRENT_TELEMETRY_ID  0x00200020U  /**< 100 ms : LV+HC current and peaks */
#define MOBO_SAFETY_STATUS_ID      0x00200030U  /**< 100 ms : safety input bitmasks */
#define MOBO_RELAY_STATUS_ID       0x00200040U  /**< 100 ms : commanded vs actual relay state */

/* RX: commands (bus -> MOBO) -----------------------------------------------*/
#define MOBO_VCU_POWER_CMD_ID      0x002001F0U  /**< VCU relay command */
#define MOBO_RESET_CMD_ID          0x002001F7U  /**< System reset (payload ignored) */
#define MOBO_CONFIG_CMD_ID         0x002001F8U  /**< Runtime config write */

/* External (non-MOBO) frames consumed by MOBO ------------------------------*/
/* Inverter Temperatures_3 (standard 11-bit). Carries INV_Coolant_Temp at
 * bytes 0..1, signed little-endian, scale 0.1 degC. */
#define INV_TEMPERATURES_3_ID      0x0A2U       /* standard 11-bit */
/* VCU_Summary (extended 29-bit). Carries VCU_RTD_Active at bit 32 (byte 4
 * bit 0). */
#define VCU_SUMMARY_ID             0x0D1001F0U  /* extended 29-bit */
/* HVC ACC_Summary (extended 29-bit). Carries Acc_Temp_Max_C at bits 48..63,
 * signed little-endian, scale 0.1 degC. Used to OR a temperature-based
 * override into the Acc Fans alternation. */
#define HVC_ACC_SUMMARY_ID         0x004001F5U  /* extended 29-bit */

#ifdef __cplusplus
}
#endif

#endif /* __CAN_IDS_H */
