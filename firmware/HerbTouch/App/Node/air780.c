#include "air780.h"
#include "node_config.h"
#include "node_identity.h"
#include "server_config.h"
#include "history.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
enum {
    S_STOP,S_AT,S_ECHO,S_CMEE,S_ID,S_SIM,S_REG,S_ATTACH,S_CLOCK,
    S_DISCONNECT,S_CLOSE,S_CREATE,S_CA_PROMPT,S_CA_DATA,S_CA_FILE,
    S_VERIFY,S_TIME_VERIFY,S_TLS_VERSION,S_ASCII,S_DIRECT,S_CONFIG,
    S_CONNECT,S_MQTT,S_SUBSCRIBE,S_ONLINE,S_PUBLISH_PROMPT,S_PUBLISH_DATA,
    S_ACK,S_BACKOFF
};
static uint32_t now_tick;
static bool due(uint32_t now,uint32_t when){return (int32_t)(now-when)>=0;}
static void copy(char *d,unsigned cap,const char *s){snprintf(d,cap,"%s",s);}
static void next(Air780 *m,unsigned step)
{m->step=step;m->waiting=false;m->ready=false;m->command_ok=false;}
void Air780_Fault(Air780 *m,const char *reason,uint32_t now)
{
    copy(m->fault,sizeof(m->fault),reason);m->online=false;m->waiting=false;
    m->step=S_BACKOFF;m->failures++;m->reconnects++;
    unsigned delay=m->failures>4?120000u:7500u*(1u<<m->failures);
    m->deadline=now+delay;
}
static bool clock_line(Air780 *m,const char *line)
{
    unsigned y,mo,d,h,mi,s,tz;char sign;int used=0;
    if(sscanf(line,"+CCLK: \"%u/%u/%u,%u:%u:%u%c%u\"%n",&y,&mo,&d,&h,&mi,&s,&sign,&tz,&used)!=8 ||
       !used || line[used] || y<24 || y>99 || tz>56 || (sign!='+' && sign!='-'))return false;
    HistoryDate date={(uint16_t)(2000+y),(uint8_t)mo,(uint8_t)d,(uint8_t)h,(uint8_t)mi,(uint8_t)s};
    if(mo>12 || d>31 || h>23 || mi>59 || s>59)return false;
    uint32_t seconds;if(!HistoryDate_ToSeconds(&date,&seconds))return false;
    int32_t offset=(sign=='+'?1:-1)*(int32_t)tz*900;
    m->wall_seconds=seconds-offset+8*3600;m->wall_tick=now_tick;m->clock_valid=true;
    return true;
}
static void event(ATEvent type,const char *text,const char *topic,void *context)
{
    Air780 *m=context;
    if(type==AT_BAD_STREAM){Air780_Fault(m,text,now_tick);return;}
    if(type==AT_MESSAGE) {
        if(m->payload_size && !strcmp(topic,NodeIdentity_AckTopic()) && m->binding_mode) {
            int result=ATBindingAck(text,m->sample_id,NodeIdentity_UID(),m->binding_target);
            if(result>=1){m->binding_result=(unsigned)result;m->acknowledged=true;}
        }
        else if(m->payload_size && !strcmp(topic,NodeIdentity_AckTopic()) && ATStoredAck(text,m->sample_id)) {
            /* ACK can precede PUBACK. Keep it latched across command states. */
            if(!m->acknowledged)m->stored++;
            m->acknowledged=true;
        }
        return;
    }
    if(type==AT_PROMPT) {
        if(m->step==S_CA_PROMPT)next(m,S_CA_DATA);
        else if(m->step==S_PUBLISH_PROMPT)next(m,S_PUBLISH_DATA);
        return;
    }
    if(!strstr(text,"MCONFIG"))copy(m->last_line,sizeof(m->last_line),text);
    if(!strcmp(text,"CLOSED") || strstr(text,"PDP DEACT") || strstr(text,"PDPDEACT") ||
       !strcmp(text,"DISCONNECT") || !strcmp(text,"CONNECT FAIL") || !strcmp(text,"CONNACK FAIL")) {
        Air780_Fault(m,text,now_tick);return;
    }
    bool error=!strcmp(text,"ERROR") || !strncmp(text,"+CME ERROR:",11) || !strcmp(text,"TimeOut");
    if(error) {
        /* Cleanup of a nonexistent session / creation of an existing CA is benign.
           A following FSWRITE and TLS verification must still succeed. */
        if(m->step==S_CLOSE && m->ca_ready)next(m,S_CA_FILE);
        else if(m->step==S_DISCONNECT || m->step==S_CLOSE || m->step==S_CREATE)next(m,m->step+1);
        else Air780_Fault(m,text,now_tick);
        return;
    }
    if(!m->waiting)return;
    if(m->step==S_SIM && !strcmp(text,"+CPIN: READY"))m->ready=true;
    if(m->step==S_SIM && !strcmp(text,"+CPIN:READY"))m->ready=true;
    if(m->step==S_REG && !strncmp(text,"+CEREG:",7)) {
        unsigned n,stat;int count=sscanf(text+7,"%u,%u",&n,&stat);
        if(count==1)stat=n;
        if(count>0 && (stat==1 || stat==5))m->ready=true;
    }
    if(m->step==S_ATTACH && !strncmp(text,"+CGATT:",7)) {
        unsigned attached;if(sscanf(text+7,"%u",&attached)==1 && attached==1)m->ready=true;
    }
    if(m->step==S_CLOCK && !strncmp(text,"+CCLK:",6))m->ready=clock_line(m,text);
    if((!strcmp(text,"CONNECT OK") || !strcmp(text,"CONNECTOK")) && m->step==S_CONNECT){next(m,S_MQTT);return;}
    if((!strcmp(text,"CONNACK OK") || !strcmp(text,"CONNACKOK")) && m->step==S_MQTT){next(m,S_SUBSCRIBE);return;}
    if(!strcmp(text,"SUBACK") && m->step==S_SUBSCRIBE){m->online=true;m->failures=0;m->fault[0]=0;next(m,S_ONLINE);return;}
    if(!strcmp(text,"PUBACK") && (m->step==S_PUBLISH_DATA || m->step==S_ACK)) {
        m->published++;next(m,S_ACK);m->deadline=now_tick+30000;m->waiting=true;return;
    }
    if(strcmp(text,"OK"))return;
    m->command_ok=true;
    if(m->step==S_CA_PROMPT || m->step==S_PUBLISH_PROMPT)return; /* raw bytes require '>' */
    if(m->step==S_CONNECT || m->step==S_MQTT || m->step==S_SUBSCRIBE || m->step==S_PUBLISH_DATA || m->step==S_ACK)return;
    if(m->step==S_SIM || m->step==S_REG || m->step==S_ATTACH || m->step==S_CLOCK) {
        if(!m->ready){Air780_Fault(m,"SIM/network/time not ready",now_tick);return;}
    }
    if(m->step==S_CA_DATA){m->ca_ready=true;next(m,S_CA_FILE);}
    else if(m->step==S_CLOSE && m->ca_ready)next(m,S_CA_FILE);
    else next(m,m->step+1);
}
void Air780_Init(Air780 *m,ModemSend send)
{memset(m,0,sizeof(*m));m->send=send;ATStream_Init(&m->stream,event,m);}
void Air780_Start(Air780 *m,uint32_t now)
{
    if(!NodeIdentity_CredentialsReady()){copy(m->fault,sizeof(m->fault),"Device credential/UID not provisioned");return;}
    m->enabled=true;m->online=false;m->acknowledged=false;now_tick=now;
    ATStream_Init(&m->stream,event,m);next(m,S_AT);
}
void Air780_Feed(Air780 *m,const uint8_t *bytes,unsigned size)
{ATStream_Feed(&m->stream,bytes,size);}
bool Air780_Offer(Air780 *m,const char *id,const char *json)
{
    if(m->payload_size || !id || !*id || strlen(id)>=sizeof(m->sample_id) || !json || strlen(json)>=sizeof(m->payload))return false;
    copy(m->sample_id,sizeof(m->sample_id),id);copy(m->payload,sizeof(m->payload),json);
    m->payload_size=(unsigned)strlen(json);m->acknowledged=false;m->binding_mode=false;m->binding_result=0;return true;
}
bool Air780_OfferBinding(Air780 *m,const char *id,const char *json)
{return Air780_OfferNodeRequest(m,NodeIdentity_Number(),id,json);}
bool Air780_OfferNodeRequest(Air780 *m,unsigned target,const char *id,const char *json)
{
    if(!Air780_Offer(m,id,json))return false;
    m->binding_mode=true;m->binding_target=target;
    return true;
}
void Air780_Release(Air780 *m)
{
    m->payload_size=0;m->sample_id[0]=0;m->acknowledged=false;m->binding_mode=false;
    if(m->step==S_ACK || m->step==S_PUBLISH_DATA)next(m,S_ONLINE);
}
bool Air780_CommitReady(const Air780 *m){return m->step==S_ACK && m->acknowledged;}
bool Air780_Time(const Air780 *m,uint32_t now,uint32_t *seconds)
{
    /* No RTC: expire network time after six hours, well before tick wrap. */
    uint32_t elapsed=now-m->wall_tick;
    if(!m->clock_valid || elapsed>21600000u)return false;
    *seconds=m->wall_seconds+elapsed/1000;return true;
}
void Air780_Poll(Air780 *m,uint32_t now)
{
    now_tick=now;if(!m->enabled)return;
    if(m->step==S_BACKOFF) {
        if(due(now,m->deadline)){ATStream_Init(&m->stream,event,m);next(m,S_AT);}
        return;
    }
    if(m->step==S_ACK && m->acknowledged)return; /* caller syncs deletion first */
    if(m->waiting){if(due(now,m->deadline))Air780_Fault(m,"AT/ACK timeout",now);return;}
    if(m->step==S_ONLINE) {
        uint32_t ignored;
        if(!Air780_Time(m,now,&ignored)){Air780_Fault(m,"Network clock expired",now);return;}
        if(m->payload_size)next(m,S_PUBLISH_PROMPT);else return;
    }
    char command[320];const char *p=command;unsigned timeout=10000;
    switch(m->step) {
    case S_AT:p="AT\r\n";timeout=3000;break;
    case S_ECHO:p="ATE0\r\n";break;
    case S_CMEE:p="AT+CMEE=2\r\n";break;
    case S_ID:p="AT+CGMR\r\n";break;
    case S_SIM:p="AT+CPIN?\r\n";break;
    case S_REG:p="AT+CEREG?\r\n";break;
    case S_ATTACH:p="AT+CGATT?\r\n";break;
    case S_CLOCK:p="AT+CCLK?\r\n";break;
    case S_DISCONNECT:p="AT+MDISCONNECT\r\n";break;
    case S_CLOSE:p="AT+MIPCLOSE\r\n";break;
    case S_CREATE:p="AT+FSCREATE=\"herb-ca.crt\"\r\n";break;
    case S_CA_PROMPT:snprintf(command,sizeof(command),"AT+FSWRITE=\"herb-ca.crt\",0,%u,30\r\n",(unsigned)NODE_CA_SIZE);break;
    case S_CA_DATA:
        if(m->send(node_ca,NODE_CA_SIZE)){m->waiting=true;m->deadline=now+35000;}
        return;
    case S_CA_FILE:p="AT+SSLCFG=\"cacert\",88,\"herb-ca.crt\"\r\n";break;
    case S_VERIFY:p="AT+SSLCFG=\"seclevel\",88,1\r\n";break;
    case S_TIME_VERIFY:p="AT+SSLCFG=\"ignorelocaltime\",88,0\r\n";break;
    case S_TLS_VERSION:p="AT+SSLCFG=\"sslversion\",88,3\r\n";break;
    case S_ASCII:p="AT+MQTTMODE=0\r\n";break;
    case S_DIRECT:p="AT+MQTTMSGSET=0\r\n";break;
    case S_CONFIG:snprintf(command,sizeof(command),"AT+MCONFIG=\"%s\",\"%s\",\"%s\"\r\n",NodeIdentity_Client(),NodeIdentity_Username(),NodeIdentity_Password());break;
    case S_CONNECT:snprintf(command,sizeof(command),"AT+SSLMIPSTART=\"%s\",%u\r\n",NODE_HOST,NODE_TLS_PORT);timeout=65000;break;
    case S_MQTT:p="AT+MCONNECT=1,60\r\n";timeout=30000;break;
    case S_SUBSCRIBE:snprintf(command,sizeof(command),"AT+MSUB=\"%s\",1\r\n",NodeIdentity_AckTopic());timeout=30000;break;
    case S_PUBLISH_PROMPT:snprintf(command,sizeof(command),"AT+MPUBEX=\"%s\",1,0,%u\r\n",m->binding_mode?NodeIdentity_BindTopic():NodeIdentity_Telemetry(),m->payload_size);break;
    case S_PUBLISH_DATA:
        if(m->send((const uint8_t*)m->payload,m->payload_size)){m->waiting=true;m->deadline=now+30000;}
        return;
    default:return;
    }
    if(m->send((const uint8_t*)p,(unsigned)strlen(p))){m->waiting=true;m->deadline=now+timeout;}
}
