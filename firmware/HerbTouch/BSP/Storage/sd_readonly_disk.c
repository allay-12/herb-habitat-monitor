/* SDIO port. History opens files read-only; explicit setup/logging may write.
   Legacy filename retained for existing Keil project compatibility. */
#include "sdio.h"
#include "diskio.h"
#include "sd_disk_status.h"
#include <string.h>
static DSTATUS state=STA_NOINIT;
static uint32_t sector_buffer[128];
DSTATUS disk_initialize(BYTE drive)
{
    if(drive)return STA_NOINIT;
    state=STA_NOINIT;
    if(hsd.Instance==SDIO)HAL_SD_DeInit(&hsd);
    memset(&hsd,0,sizeof(hsd));
    hsd.Instance=SDIO;
    hsd.Init.ClockEdge=SDIO_CLOCK_EDGE_RISING;
    hsd.Init.ClockBypass=SDIO_CLOCK_BYPASS_DISABLE;
    hsd.Init.ClockPowerSave=SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide=SDIO_BUS_WIDE_1B;
    hsd.Init.HardwareFlowControl=SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv=22;
    if(HAL_SD_Init(&hsd)==HAL_OK)state=0;
    return state;
}
DSTATUS disk_status(BYTE drive){return drive?STA_NOINIT:state;}
DRESULT disk_read(BYTE drive,BYTE *data,DWORD sector,UINT count)
{
    if(drive || !data || !count)return RES_PARERR;
    if(state&STA_NOINIT)return RES_NOTRDY;
    /* HAL's polling reader uses word accesses: an aligned bounce buffer also
       handles arbitrary FatFs byte-buffer alignment safely. */
    for(UINT i=0;i<count;i++) {
        if(HAL_SD_ReadBlocks(&hsd,(uint8_t*)sector_buffer,sector+i,1,100)!=HAL_OK) {
            state=STA_NOINIT;return RES_ERROR;
        }
        memcpy(data+i*512,sector_buffer,512);
    }
    return RES_OK;
}
DRESULT disk_write(BYTE d,const BYTE *b,DWORD s,UINT n)
{
    if(d || !b || !n)return RES_PARERR;
    if(state&STA_NOINIT)return RES_NOTRDY;
    for(UINT i=0;i<n;i++) {
        memcpy(sector_buffer,b+i*512,512);
        if(HAL_SD_WriteBlocks(&hsd,(uint8_t*)sector_buffer,s+i,1,250)!=HAL_OK) {
            state=STA_NOINIT;return RES_ERROR;
        }
        uint32_t began=HAL_GetTick();
        while(HAL_SD_GetCardState(&hsd)!=HAL_SD_CARD_TRANSFER) {
            if(HAL_GetTick()-began>=500){state=STA_NOINIT;return RES_ERROR;}
        }
    }
    return RES_OK;
}
uint32_t SDDisk_CapacitySectors(void)
{
    HAL_SD_CardInfoTypeDef info;
    return hsd.Instance==SDIO && HAL_SD_GetCardInfo(&hsd,&info)==HAL_OK?info.LogBlockNbr:0;
}
uint32_t SDDisk_LastError(void){return hsd.ErrorCode;}
DRESULT disk_ioctl(BYTE drive,BYTE cmd,void *buffer)
{
    HAL_SD_CardInfoTypeDef info;
    if(drive || (state&STA_NOINIT))return RES_NOTRDY;
    if(cmd==CTRL_SYNC)return RES_OK;
    if(!buffer)return RES_PARERR;
    if(cmd==GET_SECTOR_SIZE){*(WORD*)buffer=512;return RES_OK;}
    if(cmd==GET_BLOCK_SIZE){*(DWORD*)buffer=1;return RES_OK;}
    if(cmd==GET_SECTOR_COUNT && HAL_SD_GetCardInfo(&hsd,&info)==HAL_OK){*(DWORD*)buffer=info.LogBlockNbr;return RES_OK;}
    return RES_PARERR;
}
