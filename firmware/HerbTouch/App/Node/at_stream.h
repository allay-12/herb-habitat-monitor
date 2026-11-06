#ifndef HERB_AT_STREAM_H
#define HERB_AT_STREAM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef enum { AT_LINE,AT_PROMPT,AT_MESSAGE,AT_BAD_STREAM } ATEvent;
typedef void (*ATListener)(ATEvent event,const char *text,const char *topic,void *context);
typedef struct {
    char line[384],topic[128],payload[384];
    unsigned used,remaining,payload_used;
    bool message,discard;
    ATListener listener;void *context;
} ATStream;
void ATStream_Init(ATStream *s,ATListener listener,void *context);
void ATStream_Feed(ATStream *s,const uint8_t *data,size_t size);
bool ATStoredAck(const char *json,const char *sample_id);
int ATBindingAck(const char *json,const char *request_id,const char *uid,unsigned node);
#endif
