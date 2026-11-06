#include "air_sensor.h"
#include "at_stream.h"
#include "air780.h"
#include "server_config.h"
#include "node_store.h"
#include "history.h"
#include "history_fs.h"
#include "ff.h"
#include "disk_image.h"
#include "node_identity.h"
#undef NODE_ACK
#define NODE_ACK "herb/devices/111111112222222233333333/ack"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned failures,lines,messages,bad,prompts;
static char sent[4096];static unsigned sent_size;
static uint32_t tick;
#define CHECK(c,label) do {if(!(c)){printf("FAIL: %s (%d)\n",label,__LINE__);failures++;}}while(0)
uint32_t HAL_GetTick(void){return tick;}
static bool send_bytes(const uint8_t *p,unsigned n)
{CHECK(n<sizeof(sent),"bounded TX");memcpy(sent,p,n);sent[n]=0;sent_size=n;return true;}
static void on_stream(ATEvent e,const char *text,const char *topic,void *context)
{
    (void)context;
    if(e==AT_LINE)lines++;
    if(e==AT_PROMPT)prompts++;
    if(e==AT_BAD_STREAM)bad++;
    if(e==AT_MESSAGE){messages++;CHECK(!strcmp(topic,NODE_ACK),"MSUB topic");CHECK(ATStoredAck(text,"node01-ABCD"),"MSUB length framing");}
}
static void input(Air780 *m,const char *s)
{Air780_Feed(m,(const uint8_t*)s,(unsigned)strlen(s));}
static void step(Air780 *m,const char *expected,const char *reply)
{
    sent_size=0;tick+=10;Air780_Poll(m,tick);
    CHECK(!strncmp(sent,expected,strlen(expected)) && sent_size,"expected AT command");
    input(m,reply);
}
static void protocols(void)
{
    uint8_t request[8],frame[21]={2,3,16,2,0x92,0xff,0x9b,0,0,0,0,0,0,0,0,0,1,0x86,0xa0};
    uint16_t crc=Air_CRC(frame,19);frame[19]=crc;frame[20]=crc>>8;
    AirReading r;Air_Request(2,request);
    CHECK(Air_CRC(request,8)==0 && request[0]==2 && request[3]==0xf4 && request[5]==8,"addr2 request + CRC");
    CHECK(Air_Decode(2,frame,sizeof(frame),&r) && r.temperature_x10==-101 && r.humidity_x10==658 && r.lux==100000,"signed temperature and high/low 32-bit lux");
    CHECK(!Air_Decode(1,frame,21,&r) && !Air_Decode(2,frame,20,&r),"wrong address / truncated frame");
    frame[5]^=1;CHECK(!Air_Decode(2,frame,21,&r),"bad CRC discarded");
    frame[5]^=1;frame[3]=0xff;crc=Air_CRC(frame,19);frame[19]=crc;frame[20]=crc>>8;
    CHECK(!Air_Decode(2,frame,21,&r),"invalid range discarded");
    CHECK(ATStoredAck(" { \"stored\":true,\"sample_id\":\"node01-ABCD\" } ","node01-ABCD"),"ACK order and whitespace");
    CHECK(!ATStoredAck("{\"stored\":false,\"sample_id\":\"node01-ABCD\"}","node01-ABCD"),"negative ACK");
    CHECK(!ATStoredAck("{\"stored\":true,\"sample_id\":\"other\"}","node01-ABCD"),"wrong sample ACK");
    CHECK(!ATStoredAck("{\"stored\":true,\"sample_id\":\"node01-ABCD\"}garbage","node01-ABCD"),"trailing JSON rejected");
    CHECK(!ATStoredAck("{\"stored\":true,\"stored\":true}","node01-ABCD"),"duplicate fields rejected");
    const char *payload="{\n\"sample_id\":\"node01-ABCD\",\"stored\":true}";
    char urc[512];snprintf(urc,sizeof(urc),"\r\n+CSQ: 20,99\r\n+MSUB: \"" NODE_ACK "\",%u,%s\r\nOK\r\n> ",(unsigned)strlen(payload),payload);
    for(unsigned variant=0;variant<3;variant++) {
      snprintf(urc,sizeof(urc),"\r\n+CSQ: 20,99\r\n+MSUB: \"" NODE_ACK "\",%u%s,%s\r\nOK\r\n> ",(unsigned)strlen(payload),variant==0?"":variant==1?"byte":" byte",payload);
      for(unsigned split=0;split<=strlen(urc);split++) {
        ATStream stream;ATStream_Init(&stream,on_stream,0);messages=lines=bad=prompts=0;
        ATStream_Feed(&stream,(const uint8_t*)urc,split);
        ATStream_Feed(&stream,(const uint8_t*)urc+split,strlen(urc)-split);
        CHECK(messages==1 && lines==2 && prompts==1 && !bad,"all UART chunk boundaries / multiline JSON");
      }
    }
    ATStream stream;ATStream_Init(&stream,on_stream,0);bad=lines=0;
    char large[900];snprintf(large,sizeof(large),"+MSUB:" NODE_ACK ",400,");
    unsigned prefix=strlen(large);memset(large+prefix,'X',400);strcpy(large+prefix+400,"\r\nOK\r\n");
    ATStream_Feed(&stream,(uint8_t*)large,strlen(large));CHECK(bad==1 && lines==1,"oversized payload drains exactly before next OK");
}
static void modem_flow(void)
{
    Air780 m;Air780_Init(&m,send_bytes);Air780_Start(&m,0);
    step(&m,"AT\r","\r\nOK\r\n");step(&m,"ATE0","OK\r\n");step(&m,"AT+CMEE","OK\r\n");
    step(&m,"AT+CGMR","firmware-test\r\nOK\r\n");
    step(&m,"AT+CPIN?","+CPIN: READY\r\nOK\r\n");
    step(&m,"AT+CEREG?","+CEREG: 0,1\r\nOK\r\n");
    step(&m,"AT+CGATT?","+CGATT:1\r\nOK\r\n");
    step(&m,"AT+CCLK?","+CCLK: \"26/11/01,10:30:00+32\"\r\nOK\r\n");
    uint32_t stamp;CHECK(Air780_Time(&m,tick,&stamp),"network time acquired");
    char date[32];HistoryDate_Format(stamp,date,sizeof(date));CHECK(!strcmp(date,"2026-11-01 10:30"),"CCLK UTC+8 conversion");
    step(&m,"AT+MDISCONNECT","ERROR\r\n");step(&m,"AT+MIPCLOSE","ERROR\r\n");
    step(&m,"AT+FSCREATE","ERROR\r\n");step(&m,"AT+FSWRITE","> ");
    sent_size=0;Air780_Poll(&m,++tick);CHECK(sent_size==NODE_CA_SIZE && !memcmp(sent,node_ca,NODE_CA_SIZE),"CA raw exact-length transfer");input(&m,"OK\r\n");
    step(&m,"AT+SSLCFG=\"cacert\"","OK\r\n");
    step(&m,"AT+SSLCFG=\"seclevel\",88,1","OK\r\n");
    step(&m,"AT+SSLCFG=\"ignorelocaltime\",88,0","OK\r\n");
    step(&m,"AT+SSLCFG=\"sslversion\"","OK\r\n");
    step(&m,"AT+MQTTMODE=0","OK\r\n");step(&m,"AT+MQTTMSGSET=0","OK\r\n");
    step(&m,"AT+MCONFIG","OK\r\n");step(&m,"AT+SSLMIPSTART","OK\r\nCONNECTOK\r\n");
    step(&m,"AT+MCONNECT=1,60","OK\r\nCONNACK OK\r\n");
    step(&m,"AT+MSUB=\"" NODE_ACK "\",1","OK\r\nSUBACK\r\n");CHECK(m.online,"MQTT ONLINE only after subscription");
    const char *json="{\"device_id\":\"node01\",\"sample_id\":\"node01-ABCD\",\"_test\":true}";
    CHECK(Air780_Offer(&m,"node01-ABCD",json),"queue offer");
    step(&m,"AT+MPUBEX",">\r\n");Air780_Poll(&m,++tick);
    CHECK(sent_size==strlen(json) && !memcmp(sent,json,sent_size),"MPUBEX sends JSON without extra bytes");
    const char *ack="{\"sample_id\":\"node01-ABCD\",\"stored\":true}";
    char urc[256];snprintf(urc,sizeof(urc),"+MSUB:" NODE_ACK ",%u,%s\r\n",(unsigned)strlen(ack),ack);
    input(&m,urc);CHECK(m.acknowledged && !Air780_CommitReady(&m),"early storage ACK latched until publish completes");
    input(&m,"OK\r\nPUBACK\r\n");CHECK(Air780_CommitReady(&m),"application ACK + PUBACK permits commit");
    Air780_Release(&m);CHECK(!m.payload_size && m.online,"commit releases single pending payload");
    CHECK(Air780_Offer(&m,"node01-RETRY",json),"second offer");step(&m,"AT+MPUBEX",">\r\n");Air780_Poll(&m,++tick);
    tick+=31000;Air780_Poll(&m,tick);CHECK(!m.online && m.payload_size && !strcmp(m.sample_id,"node01-RETRY"),"timeout keeps original ID/payload");
    unsigned sent_before=sent_size;Air780_Poll(&m,tick+1);CHECK(sent_size==sent_before,"backoff avoids tight reconnect loop");
    CHECK(!Air780_Time(&m,tick+21600001u,&stamp),"clock expires without RTC");
    const char *bound="{\"request_id\":\"bind-test\",\"device_uid\":\"111111112222222233333333\",\"node\":1,\"status\":\"bound\"}";
    CHECK(ATBindingAck(bound,"bind-test",NodeIdentity_UID(),1)==IDENTITY_BOUND,"binding ACK matches request, UID and selected node");
    CHECK(ATBindingAck(bound,"other",NodeIdentity_UID(),1)<0&&ATBindingAck(bound,"bind-test",NodeIdentity_UID(),2)<0,"stale or wrong-node binding response cannot activate upload");
    CHECK(ATBindingAck("{\"request_id\":\"bind-test\",\"device_uid\":\"111111112222222233333333\",\"node\":1,\"status\":\"node_in_use\"}","bind-test",NodeIdentity_UID(),1)==IDENTITY_NODE_USED,"node conflict has a distinct UI status");
}
static void storage(const char *image)
{
    TestDisk_SetImage(image);TestDisk_AllowWrites(1);History_Init();
    HistoryDate d={2026,11,1,10,30,0};uint32_t stamp;CHECK(HistoryDate_ToSeconds(&d,&stamp),"timestamp");
    AirReading air={-5,658,100000};NodeRecord record,loaded;
    CHECK(NodeRecord_Build(&record,"node01-ABCD",&air,stamp),"real record encoding");
    CHECK(strstr(record.json,"-0.5") && strstr(record.json,"+08:00") && !strstr(record.json,"battery_pct"),"negative fraction / time zone / unknown battery omitted");
    CHECK(NodeStore_Save(&record,0x12345678u),"atomic outbox save and CSV append");
    CHECK(NodeStore_Load(&loaded) && !strcmp(record.id,loaded.id) && !strcmp(record.json,loaded.json),"durable replay is byte-identical");
    /* CSV is unchanged 11-column format and both category files are readable. */
    for(unsigned group=0;group<2;group++) {
        FIL f;char path[64],line[384];snprintf(path,sizeof(path),"0:/data/%s/20261101.CSV",group?"light":"air");
        CHECK(f_open(&f,path,FA_READ)==FR_OK,"open category CSV");
        CHECK(f_gets(line,sizeof(line),&f)!=0 && !strncmp(line,"timestamp,",10),"CSV header");
        CHECK(f_gets(line,sizeof(line),&f)!=0,"CSV measurement row");
        line[strcspn(line,"\r\n")]=0; /* same normalization as HistoryFS_ReadLine */
        float values[10];uint16_t valid;uint32_t timestamp;
        CHECK(History_ParseRow(line,&timestamp,values,&valid) && timestamp==stamp && (valid&(group?4:3))==(group?4:3),"existing history parser accepts generated rows");
        f_close(&f);
    }
    CHECK(NodeStore_Commit(&loaded),"ACK removes specific record");
    CHECK(!NodeStore_Load(&loaded),"committed queue empty");
    SoilReading soil={-101,658,1000,56,30,20,50};
    HistoryDate other={2026,11,2,10,30,0};uint32_t combined_stamp;HistoryDate_ToSeconds(&other,&combined_stamp);
    NodeRecord combined;
    CHECK(NodeRecord_BuildSensors(&combined,"node01-BOTH",&air,&soil,combined_stamp),"combined record from both real drivers");
    CHECK(strstr(combined.json,"\"soil_temperature_c\":-10.1")&&strstr(combined.json,"\"soil_npk_source\":\"device_reference\"")&&strstr(combined.json,"\"soil_ph\":5.6"),"JSON preserves soil scale, negative temperatures and reference provenance");
    CHECK(NodeStore_Save(&combined,0x34567890u),"combined sample saves all three category files");
    CHECK(NodeStore_Load(&loaded)&&!strcmp(loaded.json,combined.json)&&NodeStore_Commit(&loaded),"combined outbox replay/ACK stays byte identical");
    FIL combined_file;char combined_line[384];
    CHECK(f_open(&combined_file,"0:/data/soil/20261102.CSV",FA_READ)==FR_OK,"soil category created");
    CHECK(f_gets(combined_line,sizeof(combined_line),&combined_file)!=0&&f_gets(combined_line,sizeof(combined_line),&combined_file)!=0,"read soil row after header");
    combined_line[strcspn(combined_line,"\r\n")]=0;
    uint16_t combined_valid;float combined_values[10];uint32_t parsed_stamp;
    CHECK(History_ParseRow(combined_line,&parsed_stamp,combined_values,&combined_valid)&&combined_valid==0x3f8&&parsed_stamp==combined_stamp&&combined_values[5]==1000&&combined_values[7]==30,"soil CSV preserves exactly seven columns without inventing air zeros");f_close(&combined_file);
    CHECK(NodeRecord_BuildSensors(&combined,"node01-SOIL",0,&soil,combined_stamp+86400)&&!strstr(combined.json,"air_temperature_c"),"soil alone remains usable with air disconnected");
    CHECK(NodeStore_Save(&combined,0x34567891u),"soil-only sample persists");
    FILINFO absent;CHECK(f_stat("0:/data/air/20261103.CSV",&absent)==FR_NO_FILE,"soil-only record creates no fake air row");
    CHECK(NodeStore_Load(&loaded)&&NodeStore_Commit(&loaded),"soil-only ACK clears exact message");
    CHECK(!NodeRecord_BuildSensors(&combined,"node01-NONE",0,0,combined_stamp),"no record is built from two missing sensors");
    FIL torn;UINT tail_written;
    CHECK(f_open(&torn,"0:/data/air/20261101.CSV",FA_WRITE)==FR_OK && f_lseek(&torn,f_size(&torn))==FR_OK,"append torn CSV tail");
    const char *tail="2026-11-01 10:31:00,99";
    CHECK(f_write(&torn,tail,strlen(tail),&tail_written)==FR_OK,"simulate interrupted append");f_close(&torn);
    CHECK(NodeStore_Save(&record,0x12345679u),"save for corruption test");
    CHECK(f_open(&torn,"0:/data/air/20261101.CSV",FA_READ)==FR_OK,"reopen repaired history");
    char rows[1024];UINT row_bytes;CHECK(f_read(&torn,rows,sizeof(rows)-1,&row_bytes)==FR_OK,"read repaired history");
    rows[row_bytes]=0;CHECK(!strstr(rows,tail),"torn tail removed rather than accepted as a number");f_close(&torn);
    FIL f;CHECK(f_open(&f,record.path,FA_WRITE)==FR_OK,"open queue to inject torn/corrupt bytes");UINT written;
    CHECK(f_lseek(&f,f_size(&f)-1)==FR_OK && f_write(&f,"x",1,&written)==FR_OK,"inject CRC mismatch");f_close(&f);
    CHECK(!NodeStore_Load(&loaded) && NodeStore_Corrupt()>0,"corrupt queue never published or deleted");
    NodeIdentity_Select(2,true);NodeRecord scoped;
    CHECK(NodeRecord_BuildSensors(&scoped,"node02-SCOPED",&air,&soil,combined_stamp)&&strstr(scoped.json,"\"device_id\":\"node02\""),"selected node controls telemetry identity");
    CHECK(NodeStore_Save(&scoped,0x45678901u)&&strstr(scoped.path,"0:/nodes/node02/outbox/"),"node02 queue and CSV use isolated directories");
    CHECK(f_stat("0:/nodes/node02/data/soil/20261102.CSV",&absent)==FR_OK,"node02 soil history is physically separate");
    CHECK(NodeStore_Load(&loaded)&&!strcmp(loaded.json,scoped.json),"selected node loads its exact queue bytes");
    NodeIdentity_Init(0x44444444,0x55555555,0x66666666);NodeIdentity_Select(2,true);
    CHECK(!NodeStore_Load(&loaded)&&f_stat(scoped.path,&absent)==FR_OK,"other-board records are retained but never adopted for upload");
    NodeIdentity_Init(0x11111111,0x22222222,0x33333333);
    NodeIdentity_Select(3,true);
    NodeRecord diagnostic;
    CHECK(NodeRecord_Build(&diagnostic,"node03-UPLINK-CHECK",&air,combined_stamp),"build diagnostic fixture");
    unsigned writes_before=test_disk_writes;
    CHECK(!NodeStore_SaveUploadTest(&diagnostic,0xabc001u)&&test_disk_writes==writes_before,"unmarked sample cannot enter diagnostic storage path");
    unsigned diagnostic_size=(unsigned)strlen(diagnostic.json);
    strcpy(diagnostic.json+diagnostic_size-1,",\"_test\":true,\"_display_test\":true}");
    CHECK(NodeStore_SaveUploadTest(&diagnostic,0xabc001u)&&!NodeStore_Error(),"marked diagnostic saves durable outbox and isolated archive");
    char archive[48];strcpy(archive,NodeStore_UploadTestArchive());
    CHECK(archive[0]&&f_stat(archive,&absent)==FR_OK,"test archive exists before upload");
    CHECK(f_stat("0:/nodes/node03/data",&absent)==FR_NO_PATH||f_stat("0:/nodes/node03/data",&absent)==FR_NO_FILE,"diagnostic never creates production history directory");
    CHECK(NodeStore_Load(&loaded)&&!strcmp(loaded.json,diagnostic.json),"test outbox carries exact marked bytes");
    CHECK(NodeStore_Commit(&loaded)&&f_stat(archive,&absent)==FR_OK,"ACK removes outbox but preserves test archive");
    FIL saved;UINT saved_size;char archived[NODE_PAYLOAD_MAX];
    CHECK(f_open(&saved,archive,FA_READ)==FR_OK,"open retained diagnostic archive");
    CHECK(f_read(&saved,archived,sizeof(archived)-1,&saved_size)==FR_OK,"read retained diagnostic archive");
    archived[saved_size]=0;f_close(&saved);
    CHECK(!strcmp(archived,diagnostic.json),"archive is byte-identical to queued JSON");
    CHECK(NodeStore_SaveUploadTest(&diagnostic,0xabc001u)&&strcmp(archive,NodeStore_UploadTestArchive()),"repeated key creates a separate archive without overwriting prior test");
    CHECK(NodeStore_Load(&loaded)&&NodeStore_Commit(&loaded),"second explicit diagnostic queue can be acknowledged");
    NodeIdentity_Select(1,false);
}
int main(int argc,char **argv)
{
    NodeIdentity_Init(0x11111111,0x22222222,0x33333333);
    protocols();modem_flow();if(argc>1)storage(argv[1]);
    printf("Node protocol, MQTT FSM and SD regression failures: %u\n",failures);return failures?1:0;
}
