#ifndef __SATA_MANAGER_H
#define __SATA_MANAGER_H

#include "jasmine.h"

BOOL8 sata_manager_can_accept_read_task();
BOOL8 sata_manager_can_accept_write_task();

UINT32 sata_manager_accept_read_task();
UINT32 sata_manager_accept_write_task();

void sata_manager_finish_read_task(UINT32 const rid);
void sata_manager_finish_write_task(UINT32 const wid);

BOOL8 sata_manager_are_all_tasks_finished();

#endif
