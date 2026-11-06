#include "history_fs.h"
#include "history.h"
#include "ff.h"
#include "main.h"
#include "node_identity.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static FATFS fs;
static FIL file;
static DIR directory;
static bool opened,listing,discarding;
static uint8_t list_phase;
static const char *category="air";
static FRESULT last_result;
static uint32_t mount_generation;
static bool test_mode;
#define root_folder (test_mode?"0:/TESTDATA":NodeIdentity_DataRoot())
void HistoryFS_SetTestMode(bool enabled){test_mode=enabled;}
uint32_t HistoryFS_Generation(void){return mount_generation;}
void HistoryFS_SelectMetric(uint8_t metric){category=metric==2?"light":metric<2?"air":"soil";}
static HistoryFSResult result(FRESULT r)
{
    last_result=r;
    if(r==FR_OK)return HFS_OK;
    if(r==FR_NO_FILE || r==FR_NO_PATH)return HFS_MISSING;
    if(r==FR_NOT_READY)return HFS_NO_CARD;
    return HFS_ERROR;
}
void HistoryFS_Close(void){if(opened)f_close(&file);opened=false;discarding=false;}
void HistoryFS_ListEnd(void){if(listing)f_closedir(&directory);listing=false;}
HistoryFSResult HistoryFS_Mount(void)
{
    mount_generation++;
    HistoryFS_Close();HistoryFS_ListEnd();
    f_mount(0,"0:",0);
    return result(f_mount(&fs,"0:",1));
}
HistoryFSResult HistoryFS_ListBegin(void)
{
    char path[64];FRESULT r;
    snprintf(path,sizeof(path),"%s/%s",root_folder,category);list_phase=0;
    r=f_opendir(&directory,path);
    if(r==FR_NO_PATH || r==FR_NO_FILE){list_phase=1;r=f_opendir(&directory,root_folder);}
    listing=r==FR_OK;return result(r);
}
HistoryFSResult HistoryFS_ListNext(uint32_t *date)
{
    FILINFO info;FRESULT r;
    if(!listing)return HFS_ERROR;
    r=f_readdir(&directory,&info);
    if(r!=FR_OK)return result(r);
    if(!info.fname[0]) {
        if(list_phase==0) {
            f_closedir(&directory);list_phase=1;
            r=f_opendir(&directory,root_folder);listing=r==FR_OK;
            return r==FR_OK?HFS_SKIP:result(r);
        }
        return HFS_END;
    }
    if(!(info.fattrib&AM_DIR) && strlen(info.fname)==12 && info.fname[8]=='.' &&
       toupper((unsigned char)info.fname[9])=='C' && toupper((unsigned char)info.fname[10])=='S' && toupper((unsigned char)info.fname[11])=='V') {
        char text[20];
        for(unsigned i=0;i<8;i++)if(info.fname[i]<'0'||info.fname[i]>'9')return HFS_SKIP;
        snprintf(text,sizeof(text),"%.4s-%.2s-%.2s 00:00:00",info.fname,info.fname+4,info.fname+6);
        if(HistoryDate_Parse(text,date)) {
            /* A legacy combined file must not be counted again when its
               category file already exists. OpenDate uses the same priority. */
            if(list_phase==1) {
                char preferred[64];FILINFO category_info;
                snprintf(preferred,sizeof(preferred),"%s/%s/%s",root_folder,category,info.fname);
                FRESULT check=f_stat(preferred,&category_info);
                if(check==FR_OK && !(category_info.fattrib&AM_DIR))return HFS_SKIP;
                if(check!=FR_OK && check!=FR_NO_FILE && check!=FR_NO_PATH)return result(check);
            }
            return HFS_OK;
        }
    }
    return HFS_SKIP;
}
HistoryFSResult HistoryFS_OpenDate(uint32_t date)
{
    char path[64];HistoryDate d;FRESULT r;
    HistoryFS_Close();HistoryDate_FromSeconds(date,&d);
    snprintf(path,sizeof(path),"%s/%s/%04u%02u%02u.csv",root_folder,category,d.year,d.month,d.day);
    r=f_open(&file,path,FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) {
        snprintf(path,sizeof(path),"%s/%04u%02u%02u.csv",root_folder,d.year,d.month,d.day);
        r=f_open(&file,path,FA_READ);
    }
    opened=r==FR_OK;return result(r);
}
HistoryFSResult HistoryFS_ReadLine(char *line,uint32_t cap)
{
    uint32_t n;bool skipped=discarding;
    if(!opened)return HFS_ERROR;
    if(!f_gets(line,(int)cap,&file))return f_error(&file)?HFS_ERROR:HFS_END;
    n=(uint32_t)strlen(line);
    if(!n || line[n-1]!='\n') {
        /* Drain oversized lines in bounded chunks. A final record without a
           newline may be a torn write, so it must never become a data point. */
        discarding=true;return skipped?HFS_DRAIN:HFS_SKIP;
    }
    discarding=false;line[--n]=0;if(n && line[n-1]=='\r')line[--n]=0;
    return skipped?HFS_DRAIN:HFS_OK;
}
uint32_t HistoryFS_Tick(void){return HAL_GetTick();}
uint32_t HistoryFS_LastResult(void){return (uint32_t)last_result;}
