/* The real FatFs reads this FAT16 image. The production SDIO port is not used. */
#include "diskio.h"
#include "disk_image.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static FILE *image;
static char image_path[1024];
static int writable;
int test_disk_fail_reads;
unsigned test_disk_writes,test_disk_reads;
void TestDisk_SetImage(const char *p)
{
    if(image)fclose(image);
    image=0;
    snprintf(image_path,sizeof(image_path),"%s",p?p:"");test_disk_fail_reads=0;
}
void TestDisk_AllowWrites(int allow){writable=allow;}
DSTATUS disk_initialize(BYTE d)
{
    if(d)return STA_NOINIT;
    if(image)fclose(image);
    image=fopen(image_path,writable?"r+b":"rb");return image?0:STA_NOINIT;
}
DSTATUS disk_status(BYTE d){return d||!image?STA_NOINIT:0;}
DRESULT disk_read(BYTE d,BYTE *b,DWORD sector,UINT n)
{
    test_disk_reads++;
    if(d||!image||test_disk_fail_reads)return RES_NOTRDY;
    if(fseek(image,(long)(sector*512),SEEK_SET))return RES_ERROR;
    return fread(b,512,n,image)==n?RES_OK:RES_ERROR;
}
DRESULT disk_write(BYTE d,const BYTE *b,DWORD s,UINT n)
{
    test_disk_writes++;
    if(!writable)return RES_WRPRT;
    if(d||!image||test_disk_fail_reads)return RES_NOTRDY;
    if(fseek(image,(long)(s*512),SEEK_SET))return RES_ERROR;
    if(fwrite(b,512,n,image)!=n || fflush(image))return RES_ERROR;
    return RES_OK;
}
uint32_t SDDisk_CapacitySectors(void)
{
    if(!image)return 0;
    long at=ftell(image);fseek(image,0,SEEK_END);long size=ftell(image);fseek(image,at,SEEK_SET);
    return (uint32_t)(size/512);
}
uint32_t SDDisk_LastError(void){return test_disk_fail_reads?1u:0u;}
DRESULT disk_ioctl(BYTE d,BYTE cmd,void *buf)
{
    if(d||!image)return RES_NOTRDY;
    if(cmd==CTRL_SYNC)return RES_OK;
    if(cmd==GET_SECTOR_SIZE){*(WORD*)buf=512;return RES_OK;}
    return RES_PARERR;
}
