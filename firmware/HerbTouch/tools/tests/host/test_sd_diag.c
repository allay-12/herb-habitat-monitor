#include "sd_diag.h"
#include "history.h"
#include "history_fs.h"
#include "disk_image.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
#define CHECK(c,msg) do {if(!(c)){printf("FAIL: %s\n",msg);failures++;}} while(0)
uint32_t HAL_GetTick(void){return 0;}
static bool exists_dir(const char *path){FILINFO f;return f_stat(path,&f)==FR_OK&&(f.fattrib&AM_DIR);}
static void command(unsigned n,unsigned nonce){g_sd_test.nonce=nonce;g_sd_test.command=n;SDTest_Poll();}
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    TestDisk_SetImage(argv[1]);TestDisk_AllowWrites(1);History_Init();
    command(1,0);CHECK(g_sd_test.state==2&&g_sd_test.card_sectors>0,"read-only real FatFs volume inspection succeeds");
    CHECK(test_disk_writes==0,"read-only inspection issues zero writes");
    command(2,0x12345678);
    CHECK(g_sd_test.state==2&&g_sd_test.result==0&&g_sd_test.test_bytes==4096,"setup and 4KiB write/read complete");
    CHECK(g_sd_test.dirs_present==63,"all six directory entries verified by FatFs");
    CHECK(g_sd_test.crc_read==strtoul(argv[2],0,16)&&g_sd_test.crc_written==g_sd_test.crc_read,"file checksum matches independent Python CRC32");
    CHECK(exists_dir("0:/data/air")&&exists_dir("0:/data/light")&&exists_dir("0:/data/soil")&&exists_dir("0:/data/power")&&exists_dir("0:/data/system"),"all category directories created");
    FILINFO info;CHECK(f_stat("0:/data/system/12345678.TMP",&info)==FR_NO_FILE,"only temporary test file removed after success");
    FIL f;char text[64];UINT n;
    CHECK(f_open(&f,"0:/data/README.TXT",FA_READ)==FR_OK,"original card file remains");f_read(&f,text,63,&n);text[n]=0;f_close(&f);
    CHECK(strstr(text,"Synthetic test card")!=0,"original card contents unchanged");
    command(2,0x12345679);CHECK(g_sd_test.state==2&&g_sd_test.created_dirs==0,"repeating setup preserves existing directories and index");
    CHECK(f_open(&f,"0:/data/system/AABBCCDD.TMP",FA_CREATE_NEW|FA_WRITE)==FR_OK,"preexisting collision fixture created");f_write(&f,"KEEP",4,&n);f_close(&f);
    command(2,0xaabbccdd);CHECK(g_sd_test.state==3&&g_sd_test.result==FR_EXIST,"test refuses to overwrite an existing filename");
    CHECK(f_open(&f,"0:/data/system/AABBCCDD.TMP",FA_READ)==FR_OK,"collision file still exists");f_read(&f,text,4,&n);f_close(&f);CHECK(n==4&&memcmp(text,"KEEP",4)==0,"collision contents remain untouched");
    printf("SD layout/integrity regression failures: %u\n",failures);return failures?1:0;
}
