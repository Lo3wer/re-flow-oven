#pragma once

void tc_init(void);
// Returns temperature in tenths of a degree C, or -1 if the thermocouple is
// open / read failed.
int tc_read_tenths(void);