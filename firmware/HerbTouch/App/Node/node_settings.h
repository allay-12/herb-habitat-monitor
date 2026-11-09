#ifndef HERB_NODE_SETTINGS_H
#define HERB_NODE_SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
enum {NODECFG_OK=0,NODECFG_NO_CARD,NODECFG_CORRUPT,NODECFG_OTHER_DEVICE,NODECFG_BUSY,NODECFG_MISSING,NODECFG_IO};
enum {
    NODECFG_CONNECTING=7,NODECFG_CHECKING,NODECFG_AVAILABLE,NODECFG_OCCUPIED,
    NODECFG_DRAINING,NODECFG_CONFIRMING,NODECFG_CONFLICT,NODECFG_DENIED,
    NODECFG_TIMEOUT,NODECFG_RETRY_SAVE,NODECFG_OWNED
};
typedef struct {
    uint32_t target;
    uint32_t previous;
    uint32_t nonce[2];
    bool carry_snapshot;
} NodeMoveIntent;
unsigned NodeSettings_Load(void);
unsigned NodeSettings_Save(unsigned number);
const NodeMoveIntent *NodeSettings_PendingMove(void);
unsigned NodeSettings_BeginMove(const NodeMoveIntent *intent);
unsigned NodeSettings_FinishMove(bool accepted);
#endif
