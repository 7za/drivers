#ifndef MISSILE_TENX_CMD_H
#define MISSILE_TENX_CMD_H

#include <linux/module.h>
#include "missile_tenx_type.h"

void missile_tenx_cmd(missile_tenx_driver_t * const ptr, int16_t const action);

void
missile_tenx_cmdlist(missile_tenx_driver_t * const ptr,
		     char buff[], size_t const len);

#endif
