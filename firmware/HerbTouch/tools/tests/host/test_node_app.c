/* Runs the production orchestration, UART IRQ rings, model and real FatFs.
   Only MCU registers/interrupt HAL and the LCD page transition are replaced. */
#include "node_app.c"
#include "disk_image.h"
#include "ff.h"
#include <stdlib.h>
static unsigned failures,sensor_power,modem_power,power_writes,uart2_init,uart3_init,requests,page_changes;
static uint8_t request[8];static uint32_t tick;
static char console_input[256];static unsigned console_at;
static bool allow_modem;static char modem_output[4096];
USART_TypeDef test_usart1,test_usart2,test_usart3;
TestRNG test_rng={0,1,1};
UART_HandleTypeDef huart1={&test_usart1,{115200},0},huart2={&test_usart2,{115200},0},huart3={&test_usart3,{4800},0};
#define CHECK(c,msg) do{if(!(c)){printf("FAIL: %s (line%d)\n",msg,__LINE__);failures++;}}while(0)
uint32_t HAL_GetTick(void){return tick;}
uint32_t HAL_GetUIDw0(void){return 0x11111111;}
uint32_t HAL_GetUIDw1(void){return 0x22222222;}
uint32_t HAL_GetUIDw2(void){return 0x33333333;}
unsigned TestUART_Flag(UART_HandleTypeDef *u,unsigned flag)
{
    if(flag==UART_FLAG_TXE)return 1;
    if(flag==UART_FLAG_RXNE && u==&huart1 && console_input[console_at]){u->Instance->DR=(uint8_t)console_input[console_at++];return 1;}
    return 0;
}
void HAL_GPIO_WritePin(void *p,uint16_t pin,GPIO_PinState state)
{(void)pin;if(p==SENSOR_PWR_EN_GPIO_Port){sensor_power=state;power_writes++;}if(p==MODEM_PWR_EN_GPIO_Port){modem_power=state;power_writes++;}}
int HAL_UART_Init(UART_HandleTypeDef *u){u->gState=HAL_UART_STATE_READY;return HAL_OK;}
void MX_USART2_UART_Init(void){uart2_init++;}
void MX_USART3_UART_Init(void){uart3_init++;}
int HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t n){(void)u;(void)p;(void)n;return HAL_OK;}
int HAL_UART_Transmit_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t n)
{
    if(u==&huart2){CHECK(allow_modem&&n<sizeof(modem_output),"modem TX only in explicit mocked modem phase");memcpy(modem_output,p,n);modem_output[n]=0;}
    else {CHECK(u==&huart3&&n==8,"bounded sensor request");if(n==8){memcpy(request,p,8);requests++;}}
    return HAL_OK;
}
int HAL_UART_AbortReceive(UART_HandleTypeDef *u){(void)u;return HAL_OK;}
void HAL_NVIC_DisableIRQ(unsigned irq){(void)irq;}
void HAL_NVIC_EnableIRQ(unsigned irq){(void)irq;}
void UI_ShowDataPage(UIPage_t p){CHECK(p==UI_PAGE_OVERVIEW,"START leaves preview for live overview");page_changes++;}
static void advance(uint32_t ms){tick+=ms;Node_Poll(tick);}
static void command(const char *s){snprintf(console_input,sizeof(console_input),"%s\r\n",s);console_at=0;for(unsigned i=0;console_input[console_at]&&i<20;i++)advance(10);console_input[0]=0;}
static void modem_reply(const char *s)
{for(;*s;s++){modem_serial.rx=(uint8_t)*s;HAL_UART_RxCpltCallback(&huart2);}advance(10);}
static void modem_step(const char *expected,const char *reply)
{CHECK(!strncmp(modem_output,expected,strlen(expected)),"production app modem state advances through expected AT command");modem_reply(reply);}
static void bind_reply(const char *status)
{
    char json[300],urc[440];
    snprintf(json,sizeof(json),"{\"request_id\":\"%s\",\"device_uid\":\"%s\",\"node\":%u,\"status\":\"%s\"}",
             modem.sample_id,NodeIdentity_UID(),modem.binding_target,status);
    snprintf(urc,sizeof(urc),"+MSUB:%s,%u,%s\r\n",NodeIdentity_AckTopic(),(unsigned)strlen(json),json);
    modem_reply(urc);modem_reply("OK\r\nPUBACK\r\n");advance(1100);
}
static void response(bool soil)
{
    uint8_t f[21]={2,3,16,2,0x92,0xff,0x9b,0,0,0,0,0,0,0,0,0,1,0x86,0xa0,0,0};unsigned n=21;
    if(soil){const uint8_t s[]={1,3,14,0x02,0x92,0xff,0x9b,0x03,0xe8,0,56,0,30,0,20,0,50,0,0};memcpy(f,s,19);n=19;}
    uint16_t c=Air_CRC(f,n-2);f[n-2]=(uint8_t)c;f[n-1]=(uint8_t)(c>>8);
    for(unsigned i=0;i<n;i++){sensor_serial.rx=f[i];HAL_UART_RxCpltCallback(&huart3);}
    advance(10);advance(20);
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    TestDisk_SetImage(argv[1]);TestDisk_AllowWrites(1);History_Init();Model_Init();Node_Init();
    advance(100);CHECK(!sensor_power&&!modem_power&&!uart2_init&&!uart3_init&&!test_disk_writes,"boot is inert before sensor wiring is ready");
    command("NODE UPLOAD TEST");
    CHECK(g_upload_test.state==4&&g_upload_test.result==1&&!test_disk_writes&&!modem_power,"upload test requires explicit online binding and clock; never starts hardware");
    const float demo[]={25.6f,65.8f,12000,23.2f,45.1f,500,6.5f,30,20,50};Model_SetDisplayTest(demo,"12:00:00");
    command("NODE STATUS");CHECK(Model_IsDisplayTest()&&!sensor_power&&!test_disk_writes,"status works in preview without starting hardware");
    command("NODE SENSOR ON");CHECK(!Model_IsDisplayTest()&&page_changes==1&&g_node.sensor_enabled&&!sensor_power&&!modem_power&&!power_writes&&uart3_init==1&&!uart2_init&&huart3.Init.BaudRate==4800,"externally powered sensor start enables only shared bus without MOS writes and exits preview");
    advance(NODE_SENSOR_WARMUP_MS-20);CHECK(!requests,"no sensor request during nonblocking warm-up");
    advance(30);CHECK(requests==1&&request[0]==2&&!Air_CRC(request,8),"actual app sends addressed air request after warm-up");
    response(false);CHECK(g_node.air_frames==1&&Model_GetSnapshot()->air_temp_valid&&!Model_GetSnapshot()->soil_temp_valid,"air IRQ response updates only air model");
    CHECK(!g_node.clock_valid&&!test_disk_writes&&!g_node.samples,"no timestamp means no invented CSV or outbox records");
    nonce_ready=true;nonce[0]=1;nonce[1]=2;nonce[2]=3; /* Hardware RNG is tested on MCU, not emulated. */
    command("NODE TIME 2026-11-02 12:34:56");
    CHECK(g_node.clock_valid&&!g_node.samples&&!test_disk_writes,"first sample waits for soil's initial turn even with a valid clock");
    advance(NODE_SENSOR_GAP_MS);CHECK(request[0]==1&&request[5]==7,"actual app alternates to soil on same UART");
    response(true);CHECK(g_node.soil_frames==1&&Model_GetSnapshot()->soil_temp_valid&&Model_GetSnapshot()->soil_ec_us_cm==1000&&Model_GetSnapshot()->soil_n_mg_kg==30,"soil IRQ response updates all live soil fields");
    advance(1100);
    CHECK(g_node.clock_valid&&g_node.clock_source==NODE_CLOCK_MANUAL&&g_node.samples==1,"manual local clock permits SD sampling with 4G disconnected");
    CHECK(capture.air_valid&&capture.soil_valid&&strstr(capture.json,"device_reference"),"one snapshot carries both sources and reference provenance");
    NodeRecord loaded;CHECK(NodeStore_Load(&loaded)&&!strcmp(loaded.json,capture.json),"actual app capture survives durable outbox readback");
    CHECK(NodeStore_Commit(&loaded),"test-only explicit ACK releases first sample");
    unsigned seen=requests;
    for(unsigned i=0;i<120;i++){
        advance(250);
        if(requests!=seen){seen=requests;if(request[0]==2)response(false);}
    }
    CHECK(Model_GetSnapshot()->air_temp_valid&&!Model_GetSnapshot()->soil_temp_valid&&g_node.soil_errors>0,"missing soil expires without stopping healthy air");
    sample_due=tick;advance(1100);
    CHECK(g_node.samples==2&&capture.air_valid&&!capture.soil_valid&&!strstr(capture.json,"soil_temperature_c"),"stale soil omitted from next SD/server record");
    uint32_t start;HistoryDate date={2026,11,2,0,0,0};HistoryDate_ToSeconds(&date,&start);
    CHECK(History_Query(0,HISTORY_DAY,start,start+86400),"history enters active SD query");
    sample_due=tick;unsigned writes=test_disk_writes;advance(1100);
    CHECK(save_pending&&g_node.samples==2&&test_disk_writes==writes,"SD writes defer while history is reading without blocking sensor loop");
    char pending_id[128];strcpy(pending_id,capture.id);History_Cancel();advance(1100);
    CHECK(!save_pending&&g_node.samples==3&&!strcmp(pending_id,capture.id),"pending snapshot persists unchanged after history releases card");
    HAL_UART_ErrorCallback(&huart3);advance(10);CHECK(g_node.uart_errors==1,"UART error recovery runs in main loop");
    uint32_t valid_before=local_clock.seconds;command("NODE TIME 2026-02-30 00:00:00");
    CHECK(g_node.time_set_errors==1&&local_clock.seconds>=valid_before,"bad calendar date cannot replace known local time");
    CHECK(!modem_power&&!uart2_init,"entire sensor/SD integration leaves disconnected 4G disabled");
    /* A separate, explicitly simulated AT phase tests binding without sensors
       or a real modem/server; hardware pin calls above are test doubles. */
    allow_modem=true;NodeIdentity_Select(1,true);command("NODE MODEM ON");
    CHECK(g_node.modem_enabled&&uart2_init==1&&!modem_power&&!power_writes,"externally powered modem starts without MOS writes");
    modem_step("AT\r","OK\r\n");modem_step("ATE0","OK\r\n");modem_step("AT+CMEE","OK\r\n");
    modem_step("AT+CGMR","host-firmware\r\nOK\r\n");modem_step("AT+CPIN?","+CPIN: READY\r\nOK\r\n");
    modem_step("AT+CEREG?","+CEREG:0,1\r\nOK\r\n");modem_step("AT+CGATT?","+CGATT:1\r\nOK\r\n");
    modem_step("AT+CCLK?","+CCLK: \"26/11/03,12:00:00+32\"\r\nOK\r\n");
    modem_step("AT+MDISCONNECT","ERROR\r\n");modem_step("AT+MIPCLOSE","ERROR\r\n");modem_step("AT+FSCREATE","ERROR\r\n");
    modem_step("AT+FSWRITE","> ");modem_step("-----BEGIN CERTIFICATE-----","OK\r\n");
    modem_step("AT+SSLCFG=\"cacert\"","OK\r\n");modem_step("AT+SSLCFG=\"seclevel\"","OK\r\n");
    modem_step("AT+SSLCFG=\"ignorelocaltime\"","OK\r\n");modem_step("AT+SSLCFG=\"sslversion\"","OK\r\n");
    modem_step("AT+MQTTMODE","OK\r\n");modem_step("AT+MQTTMSGSET","OK\r\n");
    CHECK(strstr(modem_output,NodeIdentity_Username())!=0,"MQTT uses hardware-specific account, not selected node account");
    modem_step("AT+MCONFIG","OK\r\n");modem_step("AT+SSLMIPSTART","OK\r\nCONNECTOK\r\n");
    modem_step("AT+MCONNECT","OK\r\nCONNACK OK\r\n");modem_step("AT+MSUB","OK\r\nSUBACK\r\n");
    advance(1100);advance(10);
    CHECK(modem.binding_mode&&strstr(modem_output,"/bind"),"binding request is sent before any queued telemetry");
    modem_step("AT+MPUBEX","> ");bind_reply("bound");
    CHECK(NodeIdentity_Binding()==IDENTITY_BOUND&&!modem.binding_mode&&modem.payload_size&&g_node.stored==0,"successful bind unlocks queued telemetry without deleting a measurement for the bind ACK");
    Air780_Release(&modem);NodeIdentity_SetBinding(IDENTITY_PENDING);advance(1100);advance(10);
    CHECK(modem.binding_mode,"explicit retry issues a new binding transaction");
    modem_step("AT+MPUBEX","> ");bind_reply("node_in_use");
    CHECK(NodeIdentity_Binding()==IDENTITY_NODE_USED&&!modem.payload_size&&g_node.stored==0,"node conflict blocks telemetry and preserves SD outbox");
    g_node.sensor_enabled=0;sensor_power=0;
    NodeRecord old_queue;for(unsigned i=0;i<10&&NodeStore_Load(&old_queue);i++)NodeStore_Commit(&old_queue);
    NodeIdentity_SetBinding(IDENTITY_BOUND);
    unsigned real_samples=g_node.samples;
    command("NODE UPLOAD TEST");advance(1100);advance(10);
    CHECK(g_upload_test.state==2&&!g_upload_test.result&&g_upload_test.archive_path[0],"explicit upload test queues marked JSON and keeps a separate archive");
    CHECK(g_node.samples==real_samples&&!g_node.sensor_enabled&&strstr(modem.payload,"\"_test\":true")&&strstr(modem.payload,"f407_air780e_uplink"),"test does not count as a real sensor sample or start sensors");
    modem_step("AT+MPUBEX","> ");
    char test_ack[240],test_urc[400];
    snprintf(test_ack,sizeof(test_ack),"{\"sample_id\":\"wrong-id\",\"stored\":true}");
    snprintf(test_urc,sizeof(test_urc),"+MSUB:%s,%u,%s\r\n",NodeIdentity_AckTopic(),(unsigned)strlen(test_ack),test_ack);
    modem_reply(test_urc);modem_reply("OK\r\nPUBACK\r\n");advance(1100);
    CHECK(g_upload_test.state==2,"PUBACK and an unrelated application ACK cannot complete the upload test");
    snprintf(test_ack,sizeof(test_ack),"{\"sample_id\":\"%s\",\"stored\":true}",(const char*)g_upload_test.sample_id);
    snprintf(test_urc,sizeof(test_urc),"+MSUB:%s,%u,%s\r\n",NodeIdentity_AckTopic(),(unsigned)strlen(test_ack),test_ack);
    modem_reply(test_urc);advance(1100);
    FILINFO archive_info;
    CHECK(g_upload_test.state==3&&g_node.stored==1&&f_stat((const char*)g_upload_test.archive_path,&archive_info)==FR_OK,"only matching stored ACK completes test and leaves SD archive intact");

    unsigned online_step=modem.step;
    AirReading old_air={233,601,1234};
    CHECK(NodeRecord_Build(&old_queue,"before-move",&old_air,valid_before) &&
          NodeStore_Save(&old_queue,0x12345678),"queue a real old-node sample before moving");
    g_node.config_node_request=1234;g_node.command=6;
    advance(1100);advance(10);
    CHECK(modem.binding_target==1234&&strstr(modem.payload,"\"action\":\"check\""),"query targets a four-digit node");
    modem_step("AT+MPUBEX","> ");bind_reply("available");
    CHECK(g_node.config_result==NODECFG_AVAILABLE&&NodeIdentity_Number()==1&&!NodeSettings_PendingMove()->target,
          "availability does not save or switch nodes");

    g_node.command=7;advance(1100);
    CHECK(config_stage==CONFIG_DRAIN&&!NodeSettings_PendingMove()->target&&!strcmp(modem.sample_id,"before-move"),
          "move waits for the old sample to be acknowledged");
    modem_step("AT+MPUBEX","> ");
    modem_reply("OK\r\nPUBACK\r\n");advance(1100);
    CHECK(config_stage==CONFIG_DRAIN&&!NodeSettings_PendingMove()->target,"broker PUBACK alone cannot drain the queue");
    snprintf(test_ack,sizeof(test_ack),"{\"sample_id\":\"before-move\",\"stored\":true}");
    snprintf(test_urc,sizeof(test_urc),"+MSUB:%s,%u,%s\r\n",NodeIdentity_AckTopic(),(unsigned)strlen(test_ack),test_ack);
    modem_reply(test_urc);advance(1100);advance(1100);
    CHECK(NodeSettings_PendingMove()->target==1234&&strstr(modem.payload,"\"previous\":1")&&
          strstr(modem.payload,"\"carry_snapshot\":true")&&
          f_stat(old_queue.path,&archive_info)==FR_NO_FILE,"old queue drained before sending a durable move");
    char original_move[256];
    snprintf(original_move,sizeof(original_move),"%.255s",modem.payload);
    Node_Init();
    CHECK(g_node.modem_enabled&&NodeSettings_PendingMove()->target==1234&&NodeIdentity_Number()==1,
          "reboot recovers pending move while old node remains selected");
    modem.step=online_step;modem.online=true;modem.clock_valid=true;modem.wall_tick=tick;modem.wall_seconds=valid_before;
    advance(1100);advance(10);
    CHECK(!strcmp(modem.payload,original_move),"reconnect replays the same request without another claim");
    modem_step("AT+MPUBEX","> ");
    TestDisk_AllowWrites(0);bind_reply("bound");
    CHECK(g_node.config_result==NODECFG_RETRY_SAVE&&NodeIdentity_Number()==1&&NodeSettings_PendingMove()->target==1234,
          "SD write failure after ACK cannot resume old-node capture");
    TestDisk_AllowWrites(1);advance(1100);
    CHECK(g_node.config_result==NODECFG_OK&&NodeIdentity_Number()==1234&&NodeIdentity_Binding()==IDENTITY_BOUND&&
          !NodeSettings_PendingMove()->target,"durable ACK completion activates node1234");
    CHECK(f_stat("0:/data/air/20261102.CSV",&archive_info)==FR_OK,"moving preserves the old history file");
    CHECK(NodeRecord_Build(&old_queue,"after-move",&old_air,valid_before) &&
          NodeStore_Save(&old_queue,0x12345679) && strstr(old_queue.path,"/node1234/"),"new samples use the new node directory");
    CHECK(History_Query(0,HISTORY_DAY,valid_before-60,valid_before+60),"expanded node history opens");
    for(unsigned i=0;i<100&&History_Get()->status==HISTORY_READING;i++)History_Process();
    CHECK(History_Get()->status==HISTORY_READY&&History_Get()->rows==1,"expanded node history reads its own CSV");
    CHECK(!power_writes,"sensor polling, SD records, binding and upload ACK never drive reserved MOS pins");
    printf("Node app UART/model/offline-SD integration failures: %u\n",failures);return failures?1:0;
}
