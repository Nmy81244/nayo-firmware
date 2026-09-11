#ifndef SYSTEM_AT32F402_405_H
#define SYSTEM_AT32F402_405_H

#ifdef __cplusplus
extern "C" {
#endif
void system_init(void);
#ifdef __cplusplus
}
#endif

#include "../Drivers/CMSIS/cm4/Src/at32f402_405.h"

void system_clock_config(void);

#endif
