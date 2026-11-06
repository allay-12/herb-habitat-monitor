#ifndef HERB_SD_DIAG_H
#define HERB_SD_DIAG_H
#include <stdint.h>
/* SWD mailbox, all fields little-endian uint32_t. Commands: 1 read-only check;
   2 create category directories and verify an exclusively-created 4KiB file. */
typedef struct {
    uint32_t magic,version,command,nonce,state,result,stage,sequence;
    uint32_t card_sectors,free_sectors,cluster_sectors,fat_type;
    uint32_t test_bytes,crc_written,crc_read,created_dirs,root_entries,hal_error;
    uint32_t dirs_present;
    char test_path[48];
} SDTestReport;
extern volatile SDTestReport g_sd_test;
void SDTest_Poll(void);
#endif
