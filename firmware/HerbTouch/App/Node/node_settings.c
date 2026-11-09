#include "node_settings.h"
#include "node_identity.h"
#include "node_app.h"
#include "history.h"
#include "history_fs.h"
#include "ff.h"
#include <string.h>
static unsigned active_slot;static uint32_t generation;
static NodeMoveIntent pending;
/* Previous node uses at most 14 bits. Keep the existing 64-byte SD record. */
#define MOVE_CARRY_SNAPSHOT (1u << 31)
typedef struct {
    uint32_t magic,version,generation,number;
    char uid[24];
    uint32_t move_target,move_previous,move_nonce[2];
    uint32_t crc,commit;
} Record;
typedef char record_must_be_64_bytes[(sizeof(Record)==64)?1:-1];
static const char *paths[]={"0:/SYS/NODEA.CFG","0:/SYS/NODEB.CFG"};
static uint32_t crc32(const void *p,unsigned n)
{
    const uint8_t *b=p;uint32_t c=0xffffffffu;
    while(n--){c^=*b++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u & (0u-(c&1u)));}return ~c;
}
static bool history_idle(void)
{
    HistoryStatus s=History_Get()->status;
    return s!=HISTORY_FINDING && s!=HISTORY_READING;
}
static bool idle(void)
{
    return !g_node.sensor_enabled && !g_node.modem_enabled && history_idle();
}
static unsigned read_slot(unsigned slot,Record *r)
{
    FIL f;UINT n;FRESULT x=f_open(&f,paths[slot],FA_READ);
    if(x==FR_NO_FILE || x==FR_NO_PATH)return NODECFG_MISSING;
    if(x!=FR_OK)return NODECFG_IO;
    x=f_read(&f,r,sizeof(*r),&n);bool size=f_size(&f)==sizeof(*r);FRESULT closed=f_close(&f);
    if(x!=FR_OK || closed!=FR_OK)return NODECFG_IO;
    if(n!=sizeof(*r)||!size || r->magic!=0x4e434647u || (r->version!=1 && r->version!=2) ||
       (r->version==1 && r->number==0) || r->number>NODE_MAX_NUMBER ||
       (r->version==2 && (r->move_target>NODE_MAX_NUMBER || (r->move_previous&~MOVE_CARRY_SNAPSHOT)>NODE_MAX_NUMBER)) ||
       r->commit!=0x434f4d54u || r->crc!=crc32(r,56))return NODECFG_CORRUPT;
    return memcmp(r->uid,NodeIdentity_UID(),24)?NODECFG_OTHER_DEVICE:NODECFG_OK;
}
unsigned NodeSettings_Load(void)
{
    if(!idle())return NODECFG_BUSY;
    memset(&pending,0,sizeof(pending));
    if(HistoryFS_Mount()!=HFS_OK)return NODECFG_NO_CARD;
    Record a,b;unsigned x=read_slot(0,&a),y=read_slot(1,&b);const Record *r=0;
    if(x==NODECFG_OK && (y!=NODECFG_OK || (int32_t)(a.generation-b.generation)>0)){r=&a;active_slot=0;}
    else if(y==NODECFG_OK){r=&b;active_slot=1;}
    if(r) {
        generation=r->generation;
        if(r->number)NodeIdentity_Select(r->number,true);
        if(r->version==2) {
            pending.target=r->move_target;
            pending.previous=r->move_previous&~MOVE_CARRY_SNAPSHOT;
            memcpy(pending.nonce,r->move_nonce,sizeof(pending.nonce));
            pending.carry_snapshot=(r->move_previous&MOVE_CARRY_SNAPSHOT)!=0;
        }
        return r->number || pending.target?NODECFG_OK:NODECFG_MISSING;
    }
    generation=0;active_slot=1;
    if(x==NODECFG_OTHER_DEVICE || y==NODECFG_OTHER_DEVICE)return NODECFG_OTHER_DEVICE;
    if(x==NODECFG_CORRUPT || y==NODECFG_CORRUPT)return NODECFG_CORRUPT;
    return (x==NODECFG_MISSING && y==NODECFG_MISSING)?NODECFG_MISSING:NODECFG_IO;
}
static unsigned write_record(unsigned number,const NodeMoveIntent *intent)
{
    if(!history_idle())return NODECFG_BUSY;
    if(HistoryFS_Mount()!=HFS_OK)return NODECFG_NO_CARD;
    FRESULT x=f_mkdir("0:/SYS");if(x!=FR_OK && x!=FR_EXIST)return NODECFG_IO;
    Record record;memset(&record,0,sizeof(record));
    record.magic=0x4e434647u;record.version=intent || !number?2:1;
    record.generation=generation+1;record.number=number;
    if(intent) {
        record.move_target=intent->target;
        record.move_previous=intent->previous | (intent->carry_snapshot?MOVE_CARRY_SNAPSHOT:0);
        memcpy(record.move_nonce,intent->nonce,sizeof(record.move_nonce));
    }
    memcpy(record.uid,NodeIdentity_UID(),24);record.crc=crc32(&record,56);record.commit=0x434f4d54u;
    unsigned slot=active_slot^1u;FIL f;UINT n=0;x=f_open(&f,paths[slot],FA_WRITE|FA_CREATE_ALWAYS);
    if(x!=FR_OK)return NODECFG_IO;
    x=f_write(&f,&record,sizeof(record),&n);if(x==FR_OK && n==sizeof(record))x=f_sync(&f);
    FRESULT closed=f_close(&f);if(x!=FR_OK || closed!=FR_OK || n!=sizeof(record))return NODECFG_IO;
    Record verify;if(read_slot(slot,&verify)!=NODECFG_OK || memcmp(&verify,&record,sizeof(record)))return NODECFG_IO;
    generation=record.generation;active_slot=slot;
    if(intent)pending=*intent;else memset(&pending,0,sizeof(pending));
    return NODECFG_OK;
}
unsigned NodeSettings_Save(unsigned number)
{
    if(number<1 || number>NODE_MAX_NUMBER)return NODECFG_CORRUPT;
    if(!idle() || pending.target)return NODECFG_BUSY;
    unsigned result=write_record(number,0);
    if(result==NODECFG_OK)NodeIdentity_Select(number,true);
    return result;
}
const NodeMoveIntent *NodeSettings_PendingMove(void){return &pending;}
unsigned NodeSettings_BeginMove(const NodeMoveIntent *intent)
{
    if(!intent || !intent->target || intent->target>NODE_MAX_NUMBER || intent->previous>NODE_MAX_NUMBER)return NODECFG_CORRUPT;
    if(pending.target)return NODECFG_BUSY;
    return write_record(NodeIdentity_Saved()?NodeIdentity_Number():0,intent);
}
unsigned NodeSettings_FinishMove(bool accepted)
{
    if(!pending.target)return NODECFG_CORRUPT;
    unsigned number=accepted?pending.target:NodeIdentity_Saved()?NodeIdentity_Number():0;
    unsigned result=write_record(number,0);
    if(result==NODECFG_OK && accepted)NodeIdentity_Select(number,true);
    return result;
}
