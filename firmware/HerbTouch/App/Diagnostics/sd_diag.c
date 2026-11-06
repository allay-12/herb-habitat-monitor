#include "sd_diag.h"
#include "history.h"
#include "history_fs.h"
#include "sd_disk_status.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

volatile SDTestReport g_sd_test={.magic=0x53445431u,.version=1};
static uint8_t buffer[256];
static uint32_t crc_update(uint32_t crc,const uint8_t *data,unsigned length)
{
    for(unsigned i=0;i<length;i++) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return crc;
}
static FRESULT make_directory(const char *name)
{
    FRESULT r=f_mkdir(name);
    if(r==FR_OK){g_sd_test.created_dirs++;return FR_OK;}
    if(r==FR_EXIST) {
        FILINFO info;r=f_stat(name,&info);
        if(r==FR_OK && !(info.fattrib&AM_DIR))return FR_EXIST;
    }
    return r;
}
static FRESULT setup(void)
{
    static const char *folders[]={"0:/data","0:/data/light","0:/data/air","0:/data/soil","0:/data/power","0:/data/system"};
    for(unsigned i=0;i<sizeof(folders)/sizeof(folders[0]);i++) {
        FRESULT r=make_directory(folders[i]);if(r!=FR_OK)return r;
        FILINFO info;r=f_stat(folders[i],&info);
        if(r!=FR_OK || !(info.fattrib&AM_DIR))return r==FR_OK?FR_INT_ERR:r;
        g_sd_test.dirs_present|=1u<<i;
    }
    FIL file;FRESULT r=f_open(&file,"0:/data/INDEX.TXT",FA_WRITE|FA_CREATE_NEW);
    if(r==FR_EXIST)return FR_OK; /* Never replace the user's existing index. */
    if(r!=FR_OK)return r;
    static const char index[]="Herb monitor local storage\r\nlight = illuminance\r\nair = air temperature and humidity\r\nsoil = temperature,moisture,EC,pH,N,P,K\r\npower = battery/power records\r\nsystem = diagnostics\r\nDaily files: YYYYMMDD.CSV; timezone UTC+08:00\r\nLegacy combined data/YYYYMMDD.CSV remains readable.\r\n";
    UINT written;r=f_write(&file,index,sizeof(index)-1,&written);
    if(r==FR_OK && written!=sizeof(index)-1)r=FR_DISK_ERR;
    if(r==FR_OK)r=f_sync(&file);
    FRESULT close=f_close(&file);return r==FR_OK?close:r;
}
static FRESULT verify_file(uint32_t nonce)
{
    FIL file;char path[48];FRESULT r;UINT transferred;
    uint32_t write_crc=0xffffffffu,read_crc=0xffffffffu;
    snprintf(path,sizeof(path),"0:/data/system/%08lX.TMP",(unsigned long)nonce);
    memcpy((void*)g_sd_test.test_path,path,strlen(path)+1);
    g_sd_test.stage=4;
    r=f_open(&file,path,FA_READ|FA_WRITE|FA_CREATE_NEW);
    if(r!=FR_OK)return r;
    for(unsigned offset=0;offset<4096;offset+=sizeof(buffer)) {
        for(unsigned i=0;i<sizeof(buffer);i++) {
            uint32_t n=offset+i;
            buffer[i]=(uint8_t)((n*37u)^(nonce>>((n&3)*8))^0xa5u);
        }
        write_crc=crc_update(write_crc,buffer,sizeof(buffer));
        r=f_write(&file,buffer,sizeof(buffer),&transferred);
        if(r!=FR_OK || transferred!=sizeof(buffer)){f_close(&file);return r==FR_OK?FR_DISK_ERR:r;}
    }
    r=f_sync(&file);FRESULT close=f_close(&file);
    if(r!=FR_OK)return r;
    if(close!=FR_OK)return close;
    g_sd_test.crc_written=write_crc^0xffffffffu;
    g_sd_test.stage=5;r=f_open(&file,path,FA_READ);if(r!=FR_OK)return r;
    if(f_size(&file)!=4096){f_close(&file);return FR_INT_ERR;}
    for(unsigned offset=0;offset<4096;offset+=sizeof(buffer)) {
        r=f_read(&file,buffer,sizeof(buffer),&transferred);
        if(r!=FR_OK || transferred!=sizeof(buffer)){f_close(&file);return r==FR_OK?FR_DISK_ERR:r;}
        read_crc=crc_update(read_crc,buffer,transferred);g_sd_test.test_bytes+=transferred;
    }
    r=f_close(&file);g_sd_test.crc_read=read_crc^0xffffffffu;
    if(r!=FR_OK)return r;
    if(g_sd_test.crc_read!=g_sd_test.crc_written)return FR_INT_ERR;
    g_sd_test.stage=6;return f_unlink(path); /* only the file we just created */
}
static FRESULT inspect_volume(void)
{
    FATFS *volume;DWORD free_clusters;FRESULT r=f_getfree("0:",&free_clusters,&volume);
    if(r!=FR_OK)return r;
    g_sd_test.card_sectors=SDDisk_CapacitySectors();
    g_sd_test.free_sectors=(uint32_t)free_clusters*volume->csize;
    g_sd_test.cluster_sectors=volume->csize;g_sd_test.fat_type=volume->fs_type;
    DIR dir;r=f_opendir(&dir,"0:/");if(r!=FR_OK)return r;
    FILINFO info;
    for(unsigned i=0;i<512;i++) {
        r=f_readdir(&dir,&info);if(r!=FR_OK || !info.fname[0])break;
        g_sd_test.root_entries++;
    }
    FRESULT close=f_closedir(&dir);return r==FR_OK?close:r;
}
void SDTest_Poll(void)
{
    uint32_t command=g_sd_test.command;if(!command)return;
    uint32_t nonce=g_sd_test.nonce,sequence=g_sd_test.sequence+1;
    memset((void*)&g_sd_test,0,sizeof(g_sd_test));
    g_sd_test.magic=0x53445431u;g_sd_test.version=1;g_sd_test.sequence=sequence;
    g_sd_test.nonce=nonce;g_sd_test.state=1;g_sd_test.stage=1;
    FRESULT r=FR_OK;HistoryStatus status=History_Get()->status;
    if(status==HISTORY_FINDING || status==HISTORY_READING)r=FR_LOCKED;
    else if(command!=1 && command!=2)r=FR_INVALID_PARAMETER;
    else {
        HistoryFSResult fs=HistoryFS_Mount();
        if(fs!=HFS_OK)r=(FRESULT)HistoryFS_LastResult();
        else {
            g_sd_test.stage=2;r=inspect_volume();
            if(r==FR_OK && command==2){g_sd_test.stage=3;r=setup();}
            if(r==FR_OK && command==2)r=verify_file(nonce);
        }
    }
    g_sd_test.card_sectors=SDDisk_CapacitySectors();g_sd_test.hal_error=SDDisk_LastError();
    g_sd_test.result=(uint32_t)r;g_sd_test.state=r==FR_OK?2:3;
}
