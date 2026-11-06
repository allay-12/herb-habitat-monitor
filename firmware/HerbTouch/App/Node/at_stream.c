#include "at_stream.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
void ATStream_Init(ATStream *s,ATListener listener,void *context)
{memset(s,0,sizeof(*s));s->listener=listener;s->context=context;}
static void emit(ATStream *s,ATEvent e,const char *text)
{if(s->listener)s->listener(e,text,s->topic,s->context);}
/* +MSUB header is parsed before line framing; JSON may contain CR/LF and commas.
   Length is bytes, not strlen. Oversized payloads are consumed, never executed. */
static void header(ATStream *s)
{
    char *p=s->line+6,*end,*comma;unsigned n;
    while(*p==' ')p++;
    if(*p=='"') {p++;end=strchr(p,'"');if(!end)return;comma=end+1;if(*comma!=',')return;}
    else {end=strchr(p,',');if(!end)return;comma=end;}
    char *number=comma+1;while(*number==' ')number++;
    if(!isdigit((unsigned char)*number))return;
    unsigned length=0;char *q=number;
    while(isdigit((unsigned char)*q)) {
        if(length>6553u){emit(s,AT_BAD_STREAM,"invalid MSUB length");s->used=0;s->discard=true;return;}
        length=length*10u+(unsigned)(*q++-'0');
    }
    while(*q==' ')q++;
    /* Air780E documentation/examples use both N and N[ ]byte. */
    if(!strncmp(q,"byte",4)){q+=4;if(*q=='s')q++;while(*q==' ')q++;}
    if(*q!=',' || q[1])return;
    n=(unsigned)(end-p);
    if(n>=sizeof(s->topic) || length>65536u){emit(s,AT_BAD_STREAM,"bad MSUB header");s->used=0;s->discard=true;return;}
    memcpy(s->topic,p,n);s->topic[n]=0;s->remaining=length;s->payload_used=0;
    s->message=length!=0;s->discard=length>=sizeof(s->payload);s->used=0;
    if(!length){s->payload[0]=0;emit(s,AT_MESSAGE,s->payload);}
}
void ATStream_Feed(ATStream *s,const uint8_t *data,size_t size)
{
    while(size--) {
        uint8_t c=*data++;
        if(s->message) {
            if(!c)s->discard=true;
            if(!s->discard)s->payload[s->payload_used++]=(char)c;
            if(!--s->remaining) {
                s->message=false;
                if(s->discard)emit(s,AT_BAD_STREAM,"oversize MQTT message");
                else {s->payload[s->payload_used]=0;emit(s,AT_MESSAGE,s->payload);}
                s->discard=false;s->used=0;
            }
            continue;
        }
        if(c=='\n') {
            if(!s->discard && s->used){s->line[s->used]=0;emit(s,AT_LINE,s->line);}
            s->used=0;s->discard=false;continue;
        }
        if(s->discard || c=='\r')continue;
        if(c==' ' && s->used==0)continue;
        if(c=='>' && s->used==0){emit(s,AT_PROMPT,">");continue;}
        if(s->used+1>=sizeof(s->line)){s->used=0;s->discard=true;emit(s,AT_BAD_STREAM,"AT line overflow");continue;}
        s->line[s->used++]=(char)c;s->line[s->used]=0;
        if(s->used>=9 && !strncmp(s->line,"+MSUB:",6) && c==',')header(s);
    }
}
static void spaces(const char **p){while(isspace((unsigned char)**p))(*p)++;}
static bool string(const char **p,char *out,unsigned cap)
{
    unsigned n=0;spaces(p);if(*(*p)++!='"')return false;
    while(**p && **p!='"') {
        /* Server IDs are generated ASCII without escapes. Reject ambiguous
           escaped identities rather than accidentally acknowledging them. */
        if((unsigned char)**p<32 || **p=='\\' || n+1>=cap)return false;
        out[n++]=*(*p)++;
    }
    if(**p!='"')return false;
    (*p)++;out[n]=0;return true;
}
bool ATStoredAck(const char *p,const char *id)
{
    char key[32],value[128];bool have_id=false,have_stored=false;
    if(!p || !id || !*id)return false;
    spaces(&p);if(*p++!='{')return false;
    for(unsigned fields=0;fields<2;fields++) {
        if(!string(&p,key,sizeof(key)))return false;
        spaces(&p);if(*p++!=':')return false;spaces(&p);
        if(!strcmp(key,"sample_id")) {
            if(have_id || !string(&p,value,sizeof(value)) || strcmp(value,id))return false;
            have_id=true;
        } else if(!strcmp(key,"stored")) {
            if(have_stored || strncmp(p,"true",4))return false;
            p+=4;have_stored=true;
        } else return false;
        spaces(&p);if(fields==0){if(*p++!=',')return false;}
    }
    if(*p++!='}')return false;
    spaces(&p);return !*p && have_id && have_stored;
}
int ATBindingAck(const char *p,const char *id,const char *uid,unsigned node)
{
    if(!p||!id||!uid||node<1||node>9999)return -1;
    char key[32],value[128];unsigned seen=0;int status=-1;
    spaces(&p);if(*p++!='{')return -1;
    for(unsigned i=0;i<4;i++) {
        if(!string(&p,key,sizeof(key)))return -1;
        spaces(&p);if(*p++!=':')return -1;spaces(&p);
        unsigned bit=0;
        if(!strcmp(key,"node")) {
            bit=4;
            unsigned number=0,digits=0;
            if(*p<'1'||*p>'9')return -1;
            while(*p>='0'&&*p<='9') {
                if(++digits>4)return -1;
                number=number*10u+(unsigned)(*p++-'0');
            }
            if(number!=node)return -1;
        }
        else {
            if(!string(&p,value,sizeof(value)))return -1;
            if(!strcmp(key,"request_id")){bit=1;if(strcmp(value,id))return -1;}
            else if(!strcmp(key,"device_uid")){bit=2;if(strcmp(value,uid))return -1;}
            else if(!strcmp(key,"status")) {
                bit=8;
                if(!strcmp(value,"bound"))status=1;else if(!strcmp(value,"node_in_use"))status=2;
                else if(!strcmp(value,"device_in_use"))status=3;else if(!strcmp(value,"denied"))status=4;
                else if(!strcmp(value,"available"))status=5;else if(!strcmp(value,"conflict"))status=6;
                else return -1;
            } else return -1;
        }
        if(seen&bit)return -1;
        seen|=bit;spaces(&p);
        if(i<3 && *p++!=',')return -1;
    }
    if(*p++!='}')return -1;
    spaces(&p);return !*p && seen==15?status:-1;
}
