#include "node_identity.h"
#include "device_secrets.h"
#include <stdio.h>
#include <string.h>
static char uid[25]="000000000000000000000000",id[9]="node01";
static char username[32],client[32],telemetry[80],bind_topic[80],ack[80];
static char data_root[40]="0:/data",outbox_root[40]="0:/outbox";
static unsigned number=1,binding;static bool saved;static uint32_t revision;
void NodeIdentity_Init(uint32_t a,uint32_t b,uint32_t c)
{
    snprintf(uid,sizeof(uid),"%08lX%08lX%08lX",(unsigned long)a,(unsigned long)b,(unsigned long)c);
    snprintf(username,sizeof(username),"hw_%s",uid);snprintf(client,sizeof(client),"herb-%s",uid);
    snprintf(telemetry,sizeof(telemetry),"herb/devices/%s/telemetry",uid);
    snprintf(bind_topic,sizeof(bind_topic),"herb/devices/%s/bind",uid);
    snprintf(ack,sizeof(ack),"herb/devices/%s/ack",uid);
    NodeIdentity_Select(1,false);
}
bool NodeIdentity_Select(unsigned n,bool persisted)
{
    if(n<1 || n>NODE_MAX_NUMBER)return false;
    number=n;saved=persisted;binding=IDENTITY_PENDING;revision++;
    snprintf(id,sizeof(id),"node%02u",n);
    if(n==1){strcpy(data_root,"0:/data");strcpy(outbox_root,"0:/outbox");}
    else {
        snprintf(data_root,sizeof(data_root),"0:/nodes/%s/data",id);
        snprintf(outbox_root,sizeof(outbox_root),"0:/nodes/%s/outbox",id);
    }
    return true;
}
const char *NodeIdentity_Id(void){return id;}
const char *NodeIdentity_UID(void){return uid;}
const char *NodeIdentity_Username(void){return username;}
const char *NodeIdentity_Password(void){return NodeIdentity_CredentialsReady()?DEVICE_MQTT_PASSWORD:"";}
const char *NodeIdentity_Client(void){return client;}
const char *NodeIdentity_Telemetry(void){return telemetry;}
const char *NodeIdentity_BindTopic(void){return bind_topic;}
const char *NodeIdentity_AckTopic(void){return ack;}
const char *NodeIdentity_DataRoot(void){return data_root;}
const char *NodeIdentity_OutboxRoot(void){return outbox_root;}
unsigned NodeIdentity_Number(void){return number;}
bool NodeIdentity_Saved(void){return saved;}
bool NodeIdentity_CredentialsReady(void){return !strcmp(uid,DEVICE_UID) && DEVICE_MQTT_PASSWORD[0];}
unsigned NodeIdentity_Binding(void){return binding;}
void NodeIdentity_SetBinding(unsigned s){if(s<=IDENTITY_DENIED && s!=binding){binding=s;revision++;}}
uint32_t NodeIdentity_Revision(void){return revision;}
