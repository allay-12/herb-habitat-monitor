#include "display_test.h"
#include "history.h"
#include "history_fs.h"
#include "app_model.h"
#include "app_ui.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
volatile DisplayTestReport g_display_test={.magic=0x44545331u,.version=1};
static char readback[1024];
static uint32_t crc32(const void *bytes,unsigned n)
{
    const uint8_t *p=bytes;uint32_t c=0xffffffffu;
    while(n--){c^=*p++;for(unsigned bit=0;bit<8;bit++)c=(c>>1)^(0xedb88320u & (0u-(c&1u)));}
    return ~c;
}
static FRESULT mkdir_checked(const char *path)
{
    FRESULT r=f_mkdir(path);FILINFO info;
    if(r==FR_EXIST){r=f_stat(path,&info);if(r==FR_OK && !(info.fattrib&AM_DIR))r=FR_EXIST;}
    return r;
}
static FRESULT write_part(FIL *f,const void *data,unsigned n)
{UINT done;FRESULT r=f_write(f,data,n,&done);return r==FR_OK && done!=n?FR_DISK_ERR:r;}
static FRESULT roundtrip(const char *path,const char *data,unsigned n,bool csv,uint32_t expected_crc,bool write)
{
    FIL f;FRESULT r;FRESULT close;
    if(write) {
        r=f_open(&f,path,FA_WRITE|FA_CREATE_ALWAYS);if(r!=FR_OK)return r;
        if(csv)r=write_part(&f,HISTORY_CSV_HEADER "\r\n",sizeof(HISTORY_CSV_HEADER "\r\n")-1);
        if(r==FR_OK)r=write_part(&f,data,n);
        if(r==FR_OK)r=f_sync(&f);
        close=f_close(&f);if(r!=FR_OK)return r;if(close!=FR_OK)return close;
    }
    r=f_open(&f,path,FA_READ);if(r!=FR_OK)return r;
    if(csv && (!f_gets(readback,sizeof(readback),&f) || strcmp(readback,HISTORY_CSV_HEADER "\r\n")))r=FR_INT_ERR;
    UINT got=0;
    if(r==FR_OK)r=f_read(&f,readback,n,&got);
    if(r==FR_OK && (got!=n || f_tell(&f)!=f_size(&f) || crc32(readback,n)!=expected_crc || memcmp(data,readback,n)))r=FR_INT_ERR;
    close=f_close(&f);return r==FR_OK?close:r;
}
static FRESULT apply(bool write)
{
    unsigned jn=g_display_test.json_size,cn=g_display_test.csv_size;
    if(!jn || jn>=sizeof(g_display_test.json) || !cn || cn>=sizeof(g_display_test.csv) ||
       crc32((const void*)g_display_test.json,jn)!=g_display_test.json_crc ||
       crc32((const void*)g_display_test.csv,cn)!=g_display_test.csv_crc)return FR_INVALID_PARAMETER;
    if(!memchr((const void*)g_display_test.sample_id,0,sizeof(g_display_test.sample_id)))return FR_INVALID_PARAMETER;
    History_Cancel();
    if(HistoryFS_Mount()!=HFS_OK)return (FRESULT)HistoryFS_LastResult();
    g_display_test.stage=1;
    FRESULT r=FR_OK;
    if(write){r=mkdir_checked("0:/TESTDATA");if(r!=FR_OK)return r;}
    static const char *groups[]={"air","light","soil"};
    for(unsigned i=0;i<3;i++) {
        char path[48];snprintf(path,sizeof(path),"0:/TESTDATA/%s",groups[i]);
        if(write){r=mkdir_checked(path);if(r!=FR_OK)return r;}
        snprintf(path,sizeof(path),"0:/TESTDATA/%s/%08lu.CSV",groups[i],(unsigned long)g_display_test.date);
        r=roundtrip(path,(const char*)g_display_test.csv,cn,true,g_display_test.csv_crc,write);if(r!=FR_OK)return r;
    }
    g_display_test.stage=2;
    /* readback currently contains the bytes actually read from the last CSV. */
    readback[cn]=0;readback[strcspn(readback,"\r\n")]=0;
    float values[10];uint16_t valid;uint32_t stamp;
    if(!History_ParseRow(readback,&stamp,values,&valid) || stamp!=g_display_test.stamp || valid!=0x3ff)return FR_INT_ERR;
    HistoryDate date;HistoryDate_FromSeconds(stamp,&date);
    if((uint32_t)date.year*10000u+date.month*100u+date.day!=g_display_test.date)return FR_INVALID_PARAMETER;
    r=roundtrip("0:/TESTDATA/RECORD.JSN",(const char*)g_display_test.json,jn,false,g_display_test.json_crc,write);
    if(r!=FR_OK)return r;
    g_display_test.stage=3;
    char clock[16];snprintf(clock,sizeof(clock),"%02u:%02u:%02u",date.hour,date.minute,date.second);
    if(!Model_SetDisplayTest(values,clock))return FR_INVALID_PARAMETER;
    HistoryFS_SetTestMode(true);g_display_test.valid_mask=valid;
    UI_ShowDataPage(UI_PAGE_OVERVIEW);g_display_test.stage=4;return FR_OK;
}
void DisplayTest_Poll(void)
{
    uint32_t command=g_display_test.command;if(!command)return;
    g_display_test.command=0;g_display_test.state=1;FRESULT r=FR_OK;
    if(command==1 || command==6)r=apply(command==1);
    else if(command>=2 && command<=4 && Model_IsDisplayTest())UI_ShowDataPage((UIPage_t)(command-2));
    else if(command==5) {
        History_Cancel();HistoryFS_SetTestMode(false);Model_ClearDisplayTest();UI_ShowDataPage(UI_PAGE_OVERVIEW);
    } else if(command==7) {
        /* Read-only navigation for verifying live history via SWD. Never
           fabricate values or enter the separate TESTDATA preview. */
        History_Cancel();HistoryFS_SetTestMode(false);Model_ClearDisplayTest();UI_ShowDataPage(UI_PAGE_HISTORY);
    } else r=FR_INVALID_PARAMETER;
    g_display_test.result=r;g_display_test.state=r==FR_OK?2:3;
}
