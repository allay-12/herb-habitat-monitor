#include "node_store.h"
#include "node_identity.h"
#include "history.h"
#include "history_fs.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
static FRESULT error;
static uint32_t corrupt;
static DIR scan;
static bool scanning;
static bool scan_corrupt,queue_drained;
static uint32_t scan_generation;
static char upload_test_archive[48];
static void end_scan(void)
{
    if(scanning && scan_generation==HistoryFS_Generation())f_closedir(&scan);
    scanning=false;
}
static void decimal(char *out,unsigned cap,int value)
{snprintf(out,cap,"%s%d.%d",value<0?"-":"",abs(value)/10,abs(value)%10);}
static void date_text(uint32_t stamp,char *text,unsigned cap)
{
    HistoryDate d;HistoryDate_FromSeconds(stamp,&d);
    snprintf(text,cap,"%04u-%02u-%02u %02u:%02u:%02u",d.year,d.month,d.day,d.hour,d.minute,d.second);
}
bool NodeRecord_Build(NodeRecord *r,const char *id,const AirReading *air,uint32_t stamp)
{
    return air && NodeRecord_BuildSensors(r,id,air,0,stamp);
}
bool NodeRecord_BuildSensors(NodeRecord *r,const char *id,const AirReading *air,const SoilReading *soil,uint32_t stamp)
{
    if(!r || (!air && !soil) || !id || !*id || strlen(id)>=sizeof(r->id) || stamp<757382400u || stamp>3155759999u)return false;
    if(air && (air->temperature_x10 < -400 || air->temperature_x10>1200 || air->humidity_x10>1000 || air->lux>200000))return false;
    if(soil && !Soil_Valid(soil))return false;
    /* IDs generated below contain only ASCII hex and dashes. */
    for(const char *p=id;*p;p++)if(!isalnum((unsigned char)*p) && *p!='-')return false;
    memset(r,0,sizeof(*r));strcpy(r->id,id);r->stamp=stamp;
    r->air_valid=air!=0;r->soil_valid=soil!=0;
    char temp[16],humi[16],date[24],air_json[192]="",soil_json[384]="";
    if(air) {
        r->reading=*air;decimal(temp,sizeof(temp),air->temperature_x10);decimal(humi,sizeof(humi),air->humidity_x10);
        snprintf(air_json,sizeof(air_json),",\"air_temperature_c\":%s,\"air_humidity_pct\":%s,\"illuminance_lux\":%lu",temp,humi,(unsigned long)air->lux);
    }
    if(soil) {
        char ph[16];r->soil=*soil;decimal(temp,sizeof(temp),soil->temperature_x10);
        decimal(humi,sizeof(humi),soil->moisture_x10);decimal(ph,sizeof(ph),soil->ph_x10);
        snprintf(soil_json,sizeof(soil_json),",\"soil_temperature_c\":%s,\"soil_moisture_pct\":%s,\"soil_ec_us_cm\":%u,\"soil_ph\":%s,\"soil_n_mg_kg\":%u,\"soil_p_mg_kg\":%u,\"soil_k_mg_kg\":%u,\"soil_npk_source\":\"device_reference\"",
            temp,humi,soil->ec_us_cm,ph,soil->n_mg_kg,soil->p_mg_kg,soil->k_mg_kg);
    }
    date_text(stamp,date,sizeof(date));date[10]='T';
    int n=snprintf(r->json,sizeof(r->json),"{\"device_id\":\"%s\",\"device_uid\":\"%s\",\"sample_id\":\"%s\",\"timestamp\":\"%s+08:00\"%s%s}",NodeIdentity_Id(),NodeIdentity_UID(),id,date,air_json,soil_json);
    return n>0 && (unsigned)n<sizeof(r->json);
}
static bool available(void)
{
    end_scan();
    HistoryStatus s=History_Get()->status;
    if(s==HISTORY_FINDING || s==HISTORY_READING){error=FR_LOCKED;return false;}
    if(HistoryFS_Mount()!=HFS_OK){error=(FRESULT)HistoryFS_LastResult();return false;}
    return true;
}
static bool directory(const char *path)
{
    error=f_mkdir(path);if(error==FR_OK)return true;
    FILINFO info;if(error!=FR_EXIST)return false;
    error=f_stat(path,&info);return error==FR_OK && (info.fattrib&AM_DIR);
}
static bool write_all(FIL *f,const void *p,unsigned n)
{UINT done;error=f_write(f,p,n,&done);if(error==FR_OK && done!=n)error=FR_DISK_ERR;return error==FR_OK;}
static bool closed(FIL *f)
{FRESULT c=f_close(f);if(error==FR_OK)error=c;return error==FR_OK;}
static uint32_t crc32(const uint8_t *p,unsigned n)
{
    uint32_t c=0xffffffffu;
    while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u & (0u-(c&1u)));}
    return ~c;
}
static bool csv(const NodeRecord *r,const char *group)
{
    char path[80],row[384],t[16],h[16],date[24];HistoryDate d;FIL f;
    HistoryDate_FromSeconds(r->stamp,&d);date_text(r->stamp,date,sizeof(date));
    snprintf(path,sizeof(path),"%s/%s/%04u%02u%02u.CSV",NodeIdentity_DataRoot(),group,d.year,d.month,d.day);
    error=f_open(&f,path,FA_READ|FA_WRITE|FA_OPEN_ALWAYS);if(error!=FR_OK)return false;
    /* Remove only the uncommitted tail after the last complete newline. Never
       turn a torn numeric value into a valid measurement by adding newline. */
    if(f_size(&f)) {
        UINT got;FSIZE_t end=f_size(&f),start=end>sizeof(row)?end-sizeof(row):0;
        error=f_lseek(&f,start);
        if(error==FR_OK)error=f_read(&f,row,(UINT)(end-start),&got);
        if(error==FR_OK && got!=end-start)error=FR_DISK_ERR;
        if(error==FR_OK) {
            unsigned keep=got;while(keep && row[keep-1]!='\n')keep--;
            if(!keep && start)error=FR_INT_ERR;
            else {
                error=f_lseek(&f,start+keep);
                if(error==FR_OK && start+keep<end)error=f_truncate(&f);
            }
        }
    }
    if(error==FR_OK && !f_size(&f))write_all(&f,HISTORY_CSV_HEADER "\r\n",sizeof(HISTORY_CSV_HEADER "\r\n")-1);
    int n;
    if(!strcmp(group,"soil")) {
        char ph[16];decimal(t,sizeof(t),r->soil.temperature_x10);decimal(h,sizeof(h),r->soil.moisture_x10);decimal(ph,sizeof(ph),r->soil.ph_x10);
        n=snprintf(row,sizeof(row),"%s,,,,%s,%s,%u,%s,%u,%u,%u\r\n",date,t,h,r->soil.ec_us_cm,ph,r->soil.n_mg_kg,r->soil.p_mg_kg,r->soil.k_mg_kg);
    } else {
        decimal(t,sizeof(t),r->reading.temperature_x10);decimal(h,sizeof(h),r->reading.humidity_x10);
        n=!strcmp(group,"air")?snprintf(row,sizeof(row),"%s,%s,%s,,,,,,,,\r\n",date,t,h):
            snprintf(row,sizeof(row),"%s,,,%lu,,,,,,,\r\n",date,(unsigned long)r->reading.lux);
    }
    if(n<0 || (unsigned)n>=sizeof(row)){error=FR_INT_ERR;return closed(&f);}
    if(error==FR_OK)write_all(&f,row,(unsigned)n);
    if(error==FR_OK)error=f_sync(&f);
    return closed(&f);
}
static bool archive_test(const NodeRecord *r,uint32_t key)
{
    upload_test_archive[0]=0;
    if(!directory("0:/TESTDATA") || !directory("0:/TESTDATA/UPLINK"))return false;
    char path[48],temporary[48];FIL f;FILINFO info;
    for(unsigned i=0;i<64;i++,key++) {
        snprintf(path,sizeof(path),"0:/TESTDATA/UPLINK/%08lX.JSN",(unsigned long)key);
        error=f_stat(path,&info);if(error==FR_OK)continue;
        if(error!=FR_NO_FILE)return false;
        snprintf(temporary,sizeof(temporary),"0:/TESTDATA/UPLINK/%08lX.TMP",(unsigned long)key);
        error=f_open(&f,temporary,FA_WRITE|FA_CREATE_NEW);
        if(error==FR_EXIST)continue;
        if(error!=FR_OK)return false;
        write_all(&f,r->json,(unsigned)strlen(r->json));
        if(error==FR_OK)error=f_sync(&f);
        if(!closed(&f))return false;
        error=f_rename(temporary,path);if(error!=FR_OK)return false;
        error=f_open(&f,path,FA_READ);if(error!=FR_OK)return false;
        unsigned length=(unsigned)strlen(r->json),offset=0;char block[64];UINT got;
        if(f_size(&f)!=length)error=FR_INT_ERR;
        while(error==FR_OK && offset<length) {
            unsigned count=length-offset;if(count>sizeof(block))count=sizeof(block);
            error=f_read(&f,block,count,&got);
            if(error==FR_OK && (got!=count || memcmp(block,r->json+offset,count)))error=FR_INT_ERR;
            offset+=count;
        }
        if(!closed(&f))return false;
        strcpy(upload_test_archive,path);return true;
    }
    error=FR_EXIST;return false;
}
static bool save_record(NodeRecord *r,uint32_t key,bool test)
{
    if(!r || (!r->air_valid && !r->soil_valid))return false;
    if(!available())return false;
    char path[80];
    if(NodeIdentity_Number()!=1) {
        if(!directory("0:/nodes"))return false;
        snprintf(path,sizeof(path),"0:/nodes/%s",NodeIdentity_Id());if(!directory(path))return false;
    }
    if(!directory(NodeIdentity_OutboxRoot()))return false;
    const char *groups[]={"air","light","soil"};
    if(!test) {
        if(!directory(NodeIdentity_DataRoot()))return false;
        for(unsigned i=0;i<3;i++){snprintf(path,sizeof(path),"%s/%s",NodeIdentity_DataRoot(),groups[i]);if(!directory(path))return false;}
    }
    FIL f;char temporary[48],header[176];
    /* CREATE_NEW never overwrites another sample. CRC covers exact JSON bytes.
       A .TMP becomes publishable only after sync, close, and rename. */
    for(unsigned i=0;i<64;i++,key++) {
        snprintf(r->path,sizeof(r->path),"%s/%08lX.MSG",NodeIdentity_OutboxRoot(),(unsigned long)key);
        FILINFO info;error=f_stat(r->path,&info);if(error==FR_OK)continue;
        if(error!=FR_NO_FILE)return false;
        snprintf(temporary,sizeof(temporary),"%s/%08lX.TMP",NodeIdentity_OutboxRoot(),(unsigned long)key);
        error=f_open(&f,temporary,FA_WRITE|FA_CREATE_NEW);
        if(error==FR_EXIST)continue;
        if(error!=FR_OK)return false;
        unsigned n=(unsigned)strlen(r->json);
        int hn=snprintf(header,sizeof(header),"HERB1 %lu %08lX %s\n",(unsigned long)n,(unsigned long)crc32((const uint8_t*)r->json,n),r->id);
        write_all(&f,header,(unsigned)hn);
        if(error==FR_OK)write_all(&f,r->json,n);
        if(error==FR_OK)error=f_sync(&f);
        if(!closed(&f))return false;
        error=f_rename(temporary,r->path);if(error!=FR_OK)return false;
        if(test) {
            /* Keep an isolated exact JSON copy after the ACK removes outbox.
               Archive failure must not undo a durable publishable message. */
            archive_test(r,key);return true;
        }
        /* CSV is local history; a failure must not undo a durable outbox. */
        bool air_ok=true,light_ok=true,soil_ok=true;
        if(r->air_valid){air_ok=csv(r,"air");light_ok=csv(r,"light");}
        if(r->soil_valid)soil_ok=csv(r,"soil");
        if(!air_ok || !light_ok || !soil_ok){if(error==FR_OK)error=FR_DISK_ERR;}
        if((r->air_valid && (air_ok || light_ok)) || (r->soil_valid && soil_ok))History_NotifyWrite(r->stamp);
        return true;
    }
    error=FR_EXIST;return false;
}
bool NodeStore_Save(NodeRecord *r,uint32_t key){return save_record(r,key,false);}
bool NodeStore_SaveUploadTest(NodeRecord *r,uint32_t key)
{
    if(!r || !strstr(r->json,"\"_test\":true") || !strstr(r->json,"\"_display_test\":true")) {
        error=FR_INVALID_PARAMETER;return false;
    }
    return save_record(r,key,true);
}
const char *NodeStore_UploadTestArchive(void){return upload_test_archive;}
static bool load_file(NodeRecord *r)
{
    FIL f;UINT got;char header[176],extra;unsigned long n,crc;int used=0;
    error=f_open(&f,r->path,FA_READ);if(error!=FR_OK)return false;
    if(!f_gets(header,sizeof(header),&f) || sscanf(header,"HERB1 %lu %lx %127s%n",&n,&crc,r->id,&used)!=3 ||
       header[used]!='\n' || header[used+1] || n==0 || n>=sizeof(r->json)) {error=FR_INT_ERR;closed(&f);return false;}
    error=f_read(&f,r->json,(UINT)n,&got);if(error==FR_OK && got!=n)error=FR_INT_ERR;
    if(error==FR_OK){error=f_read(&f,&extra,1,&got);if(error==FR_OK && got)error=FR_INT_ERR;}
    r->json[n]=0;
    if(error==FR_OK && crc32((const uint8_t*)r->json,(unsigned)n)!=(uint32_t)crc)error=FR_INT_ERR;
    return closed(&f);
}
bool NodeStore_Load(NodeRecord *r)
{
    queue_drained=false;
    HistoryStatus status=History_Get()->status;
    if(status==HISTORY_FINDING || status==HISTORY_READING){end_scan();error=FR_LOCKED;return false;}
    if(scanning && scan_generation!=HistoryFS_Generation())scanning=false;
    if(!scanning) {
        if(!available())return false;
        error=f_opendir(&scan,NodeIdentity_OutboxRoot());
        if(error==FR_NO_PATH || error==FR_NO_FILE){error=FR_OK;queue_drained=true;return false;}
        if(error!=FR_OK)return false;
        scanning=true;scan_corrupt=false;scan_generation=HistoryFS_Generation();
    }
    FILINFO info;
    /* At most16 entries per call. History can remount between calls; discard
       the old iterator in that case rather than using invalid FatFs objects. */
    for(unsigned i=0;i<16;i++) {
        error=f_readdir(&scan,&info);
        if(error!=FR_OK || !info.fname[0]) {
            queue_drained=error==FR_OK && !scan_corrupt;
            if(error==FR_OK && scan_corrupt)error=FR_INT_ERR;
            end_scan();
            return false;
        }
        if((info.fattrib&AM_DIR) || strlen(info.fname)!=12 || strcmp(info.fname+8,".MSG"))continue;
        snprintf(r->path,sizeof(r->path),"%s/%s",NodeIdentity_OutboxRoot(),info.fname);
        if(load_file(r)) {
            char owner[96];snprintf(owner,sizeof(owner),"{\"device_id\":\"%s\",\"device_uid\":\"%s\",",NodeIdentity_Id(),NodeIdentity_UID());
            if(!strncmp(r->json,owner,strlen(owner))){end_scan();return true;}
            /* Keep legacy/other-board records, but never send them under this
               board's account. Changing node does not relabel old samples. */
        } else {corrupt++;scan_corrupt=true;}
    }
    return false;
}
bool NodeStore_QueueDrained(void){return queue_drained;}
bool NodeStore_Commit(const NodeRecord *r)
{
    if(!available())return false;
    /* Only a matching application ACK permits deletion of this queue file. */
    error=f_unlink(r->path);return error==FR_OK || error==FR_NO_FILE;
}
uint32_t NodeStore_Error(void){return (uint32_t)error;}
uint32_t NodeStore_Corrupt(void){return corrupt;}
