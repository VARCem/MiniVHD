#ifndef MINIVHD_UTIL_H
#define MINIVHD_UTIL_H

#include <stdint.h>
#define MVHD_START_TS 946684800

bool mvhd_is_conectix_str(const void* buffer);
void mvhd_generate_uuid(uint8_t *uuid);
uint32_t vhd_calc_timestamp(void);
time_t vhd_get_created_time(MVHDMeta *vhdm);

#endif