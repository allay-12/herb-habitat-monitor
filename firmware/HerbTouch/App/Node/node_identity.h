#ifndef HERB_NODE_IDENTITY_H
#define HERB_NODE_IDENTITY_H
#include <stdint.h>
#include <stdbool.h>
#define NODE_MAX_NUMBER 9999u
enum {IDENTITY_PENDING=0,IDENTITY_BOUND,IDENTITY_NODE_USED,IDENTITY_DEVICE_USED,IDENTITY_DENIED};
void NodeIdentity_Init(uint32_t uid0,uint32_t uid1,uint32_t uid2);
bool NodeIdentity_Select(unsigned number,bool saved);
const char *NodeIdentity_Id(void);
const char *NodeIdentity_UID(void);
const char *NodeIdentity_Username(void);
const char *NodeIdentity_Password(void);
const char *NodeIdentity_Client(void);
const char *NodeIdentity_Telemetry(void);
const char *NodeIdentity_BindTopic(void);
const char *NodeIdentity_AckTopic(void);
const char *NodeIdentity_DataRoot(void);
const char *NodeIdentity_OutboxRoot(void);
unsigned NodeIdentity_Number(void);
bool NodeIdentity_Saved(void);
bool NodeIdentity_CredentialsReady(void);
unsigned NodeIdentity_Binding(void);
void NodeIdentity_SetBinding(unsigned status);
uint32_t NodeIdentity_Revision(void);
#endif
