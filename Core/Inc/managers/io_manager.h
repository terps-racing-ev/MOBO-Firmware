#ifndef IO_MANAGER_H
#define IO_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#define IO_REFRESH_FREQ_MS 1000

#define LV_CURR_AVG_SAMPLES 16U
#define LV_CURR_ZERO_RAW 2048U
#define LV_CURR_OFFSET_MA 200

void IO_ManagerTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* IO_MANAGER_H */
