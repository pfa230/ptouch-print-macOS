/* Bluetooth Classic (RFCOMM/SPP) transport for P-Touch printers, see btMac.m */
#ifndef BTMAC_H
#define BTMAC_H

#include <stddef.h>
#include <stdint.h>

int ptbt_open(const char *want, int debug, char *found_name, size_t found_len);
int ptbt_write(const uint8_t *data, size_t len);
int ptbt_flush(void);
int ptbt_read(uint8_t *buf, int len, int timeout_ms, int *tx);
void ptbt_discard_input(void);
void ptbt_close(void);
int ptbt_rerun_as_responsible(int argc, char **argv);

#endif
