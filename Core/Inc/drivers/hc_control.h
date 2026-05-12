#ifndef DRIVERS_HC_CONTROL_H
#define DRIVERS_HC_CONTROL_H

#include "data/can.h"

void HC_Control_HandleRpiCommand(CAN_Message_t *message);
void HC_Control_HandleVcuCommand(CAN_Message_t *message);

#endif /* DRIVERS_HC_CONTROL_H */
