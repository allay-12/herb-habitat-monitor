#ifndef HERB_NODE_UPLOAD_TEST_H
#define HERB_NODE_UPLOAD_TEST_H
#include <stdint.h>
/* command=1 requests ONE synthetic, explicitly marked transport check.
 * State:0 idle,1 pending SD,2 queued,3 application-ACKed,4 rejected.
 * Result:0 OK,1 not online/bound/clock-ready or sensors active,2 busy,
 *        3 RNG/encoding failure,4 SD failure. sd_error is raw FatFs result.
 */
typedef struct {
    uint32_t command,state,result,stamp,attempts,sd_error;
    char sample_id[128],archive_path[48];
} NodeUploadTest;
extern volatile NodeUploadTest g_upload_test;
#endif
