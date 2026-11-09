#include "node_settings.h"
#include "node_identity.h"
#include "node_app.h"
#include "history.h"
#include "ff.h"
#include "disk_image.h"
#include <stdio.h>
#include <string.h>
volatile NodeReport g_node;static unsigned failures;
#define CHECK(c,msg) do{if(!(c)){printf("FAIL: %s (line%d)\n",msg,__LINE__);failures++;}}while(0)
uint32_t HAL_GetTick(void){return 0;}
static void uid(void){NodeIdentity_Init(0x11111111,0x22222222,0x33333333);}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    TestDisk_SetImage(argv[1]);TestDisk_AllowWrites(1);History_Init();uid();
    CHECK(NodeSettings_Load()==NODECFG_MISSING&&!NodeIdentity_Saved(),"missing config never becomes a confirmed identity");
    CHECK(NodeSettings_Save(2)==NODECFG_OK&&NodeIdentity_Number()==2&&NodeIdentity_Saved(),"manual node selection persists with UID");
    CHECK(!strcmp(NodeIdentity_DataRoot(),"0:/nodes/node02/data")&&!strcmp(NodeIdentity_OutboxRoot(),"0:/nodes/node02/outbox"),"node folders remain isolated");
    unsigned writes=test_disk_writes;g_node.sensor_enabled=1;
    CHECK(NodeSettings_Save(4)==NODECFG_BUSY&&NodeIdentity_Number()==2&&test_disk_writes==writes,"running sensors cannot be relabeled");g_node.sensor_enabled=0;
    CHECK(NodeSettings_Save(3)==NODECFG_OK,"second save uses alternate record");
    uid();CHECK(NodeSettings_Load()==NODECFG_OK&&NodeIdentity_Number()==3,"selection restores after runtime reset");
    FIL file;UINT done;
    CHECK(f_open(&file,"0:/SYS/NODEB.CFG",FA_WRITE)==FR_OK,"open latest config for torn-write fixture");
    CHECK(f_write(&file,"BAD!",4,&done)==FR_OK&&f_close(&file)==FR_OK,"simulate incomplete latest record");
    uid();CHECK(NodeSettings_Load()==NODECFG_OK&&NodeIdentity_Number()==2,"bad latest record falls back to earlier CRC-valid record");
    NodeIdentity_Init(0x44444444,0x55555555,0x66666666);
    CHECK(NodeSettings_Load()==NODECFG_OTHER_DEVICE&&!NodeIdentity_Saved()&&!NodeIdentity_CredentialsReady(),"copied card and firmware credentials cannot adopt another board UID");
    CHECK(!NodeIdentity_Select(0,true)&&!NodeIdentity_Select(10000,true),"invalid logical nodes rejected");
    uid();CHECK(NodeSettings_Load()==NODECFG_OK&&NodeIdentity_Number()==2,"original identity can still read its configuration");
    NodeMoveIntent move={9999,2,{1,2},true};
    g_node.sensor_enabled=g_node.modem_enabled=1;
    CHECK(NodeSettings_BeginMove(&move)==NODECFG_OK&&NodeIdentity_Number()==2,"persist intent before changing the live identity");
    g_node.sensor_enabled=g_node.modem_enabled=0;
    uid();CHECK(NodeSettings_Load()==NODECFG_OK&&NodeIdentity_Number()==2,"load pending request after reset");
    const NodeMoveIntent *restored=NodeSettings_PendingMove();
    CHECK(restored->target==move.target && restored->previous==move.previous &&
          !memcmp(restored->nonce,move.nonce,sizeof(move.nonce)) && restored->carry_snapshot,
          "reset restores request identity and same-site mode");
    TestDisk_AllowWrites(0);
    CHECK(NodeSettings_FinishMove(true)!=NODECFG_OK&&NodeIdentity_Number()==2&&NodeSettings_PendingMove()->target==9999,
          "failed SD save holds the original identity and pending request");
    TestDisk_AllowWrites(1);
    CHECK(NodeSettings_FinishMove(true)==NODECFG_OK&&NodeIdentity_Number()==9999&&!NodeSettings_PendingMove()->target,
          "accepted move persists the expanded node number");
    uid();CHECK(NodeSettings_Load()==NODECFG_OK&&!strcmp(NodeIdentity_Id(),"node9999"),"expanded node restores after reset");
    printf("Node settings persistence/identity regression failures: %u\n",failures);return failures?1:0;
}
