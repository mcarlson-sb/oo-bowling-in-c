#ifndef GAME_SHELL_PORTS_H
#define GAME_SHELL_PORTS_H

/* Private to the shell: what its interrupt side, game_shell_isr.c, posts to. The interrupt side
 * has a file of its own so that its stack tripwire can be the interrupt's. */

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

typedef struct {
    QueueHandle_t pinsetter;
    QueueHandle_t lost_report;
    TaskHandle_t game_task;
} GameShellPorts;

const GameShellPorts *GameShell_Ports(void);

/* Before the scheduler starts: the interrupt side's count of lost rolls back to 0. */
void GameShell_ResetIsr(void);

#endif /* GAME_SHELL_PORTS_H */
