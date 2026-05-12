#include "drivers/debug.h"

void Debug_HandleCanReset(CAN_Message_t *message) {
    if ((message == NULL) || (message->id != CAN_RESET_CMD)) {
        return;
    }

    NVIC_SystemReset();
}
