

#ifndef __CANMSG_H__
#define __CANMSG_H__

#include "machine.h"

void CanMsgInit(void);
int8_t CanMsgManager(uint8_t tick, machine_status *machine);
void CanMsgEnableForce(void);

#endif
