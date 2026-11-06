#ifndef HERB_DISPLAY_TEST_H
#define HERB_DISPLAY_TEST_H
#include <stdint.h>
/* Explicit SWD-only preview. All integers LE32;1560-byte layout.
   1=write/read card and show;2=overview;3=soil;4=history;5=leave preview;
   6=verify/read existing test files and restore preview, without card writes.
   7=show real local history, without test injection or card writes.
   No command starts a modem, powers a load, formats media or writes data/. */
typedef struct {
    uint32_t magic,version,command,state,result,stage,stamp,date,nonce;
    uint32_t json_size,csv_size,json_crc,csv_crc,valid_mask;
    char sample_id[96],json[1024],csv[384];
} DisplayTestReport;
extern volatile DisplayTestReport g_display_test;
void DisplayTest_Poll(void);
#endif
