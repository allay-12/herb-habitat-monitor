/* Real orchestration, serial response validation, model and FatFs. */
#define main previous_profile_main
#include "test_node_app.c"
#undef main

static void reply(bool soil,unsigned turn)
{
    uint8_t frame[21]={0};
    unsigned length=soil?19u:21u;
    frame[0]=soil?1:2;frame[1]=3;frame[2]=soil?14:16;
    uint16_t values[8]={0};
    if(soil) {
        values[0]=turn?300:100;values[1]=(uint16_t)(turn?-99:-101);
        values[2]=100;values[3]=65;values[4]=10;values[5]=20;values[6]=30;
    } else {
        values[0]=turn?700:500;values[1]=turn?300:100;
        uint32_t lux=turn?100000u:50000u;
        values[6]=(uint16_t)(lux>>16);values[7]=(uint16_t)lux;
    }
    for(unsigned i=0;i<(soil?7u:8u);i++) {
        frame[3+i*2]=(uint8_t)(values[i]>>8);frame[4+i*2]=(uint8_t)values[i];
    }
    uint16_t crc=Air_CRC(frame,length-2);
    frame[length-2]=(uint8_t)crc;frame[length-1]=(uint8_t)(crc>>8);
    for(unsigned i=0;i<length;i++){sensor_serial.rx=frame[i];HAL_UART_RxCpltCallback(&huart3);}
    advance(10);advance(20);
}

int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    TestDisk_SetImage(argv[1]);TestDisk_AllowWrites(1);History_Init();Model_Init();
    tick=0xffff0000u;Node_Init();
    CHECK(!Model_GetSnapshot()->air_temp_valid&&!Model_GetSnapshot()->soil_temp_valid,
          "boot shows no cached historical values");
    sensor_start(tick);
    uint32_t start=tick;
    nonce_ready=true;nonce[0]=1;nonce[1]=2;nonce[2]=3;
    CHECK(NodeClock_Set(&local_clock,846000000u,tick,NODE_CLOCK_MANUAL),"set window clock");
    advance(NODE_SENSOR_WARMUP_MS-1);
    CHECK(sensor_power&&!modem_power&&!requests&&!g_node.samples,"power on warms up without requests or snapshots");
    advance(1);reply(false,0);advance(NODE_SENSOR_GAP_MS+20);reply(true,0);
    advance(NODE_SENSOR_POLL_MS);reply(false,1);advance(NODE_SENSOR_GAP_MS+20);reply(true,1);
    CHECK(g_sensor_cycle.air_count==2&&g_sensor_cycle.soil_count==2,"count each fresh reply once");
    CHECK(!g_node.samples,"no per-minute snapshots inside a measurement window");
    float last_air=Model_GetSnapshot()->air_temperature_c;
    float last_soil=Model_GetSnapshot()->soil_temperature_c;
    tick=start+NODE_SENSOR_ACTIVE_MS;Node_Poll(tick);advance(1100);
    CHECK(!sensor_power&&!modem_power&&g_sensor_cycle.completed==1,"power off at five minutes across tick wrap");
    CHECK(g_node.samples==1&&capture.air_valid&&capture.soil_valid,"one complete average record per window");
    CHECK(capture.reading.temperature_x10==200&&capture.reading.humidity_x10==600&&capture.reading.lux==75000,
          "average air units and wide light values correctly");
    CHECK(capture.soil.temperature_x10==-100&&capture.soil.moisture_x10==200,"signed soil average rounds correctly");
    CHECK(strstr(capture.json,"\"aggregation\":\"mean\"")&&strstr(capture.json,"\"air_sample_count\":2"),
          "cloud payload identifies mean and sample count");
    NodeRecord loaded;
    CHECK(NodeStore_Load(&loaded)&&!strcmp(loaded.json,capture.json),"durable outbox retains exact mean JSON");
    CHECK(NodeStore_Commit(&loaded),"release test-owned first window message");
    CHECK(Model_GetSnapshot()->air_temp_valid&&Model_GetSnapshot()->soil_temp_valid&&
          Model_GetSnapshot()->air_temperature_c==last_air&&Model_GetSnapshot()->soil_temperature_c==last_soil,
          "rest shows last readings, separately from the stored mean");
    unsigned request_count=requests;
    tick=start+NODE_SENSOR_CYCLE_MS-1;Node_Poll(tick);
    CHECK(!sensor_power&&requests==request_count&&g_node.samples==1,"ten minute rest sends no requests and creates no duplicate samples");
    CHECK(Model_GetSnapshot()->air_temp_valid&&Model_GetSnapshot()->air_temperature_c==last_air,
          "screen retains last successful data throughout rest");
    advance(1);
    CHECK(sensor_power&&g_sensor_cycle.phase&&g_sensor_cycle.started_ms-start==NODE_SENSOR_CYCLE_MS,
          "second window begins fifteen minutes after the first start");
    CHECK(g_sensor_cycle.air_count==0&&g_sensor_cycle.soil_count==0,"old samples excluded from second average");
    advance(NODE_SENSOR_WARMUP_MS);reply(false,0);advance(NODE_SENSOR_GAP_MS+20);reply(true,0);
    TestDisk_AllowWrites(0);
    tick=g_sensor_cycle.started_ms+NODE_SENSOR_ACTIVE_MS;Node_Poll(tick);
    CHECK(!sensor_power&&save_pending&&g_node.samples==1,"SD failure preserves one pending mean after power off");
    char pending[1024];strcpy(pending,capture.json);
    advance(1100);CHECK(save_pending&&!strcmp(pending,capture.json),"retry preserves message identity and values");
    TestDisk_AllowWrites(1);advance(1100);
    CHECK(!save_pending&&g_node.samples==2&&capture.reading.temperature_x10==100,"recovered SD writes second mean exactly once");
    tick=g_sensor_cycle.next_start_ms;Node_Poll(tick);
    advance(NODE_SENSOR_WARMUP_MS);
    advance(NODE_SENSOR_TIMEOUT_MS+NODE_SENSOR_GAP_MS+10);
    advance(NODE_SENSOR_GAP_MS+10);reply(true,1);
    CHECK(g_sensor_cycle.air_count==0&&g_sensor_cycle.soil_count==1,"failed air probe does not reuse the last window");
    tick=g_sensor_cycle.started_ms+NODE_SENSOR_ACTIVE_MS;Node_Poll(tick);advance(1100);
    CHECK(g_node.samples==3&&!capture.air_valid&&capture.soil_valid&&
          !strstr(capture.json,"air_temperature_c"),"single-probe mean omits missing air instead of inserting an old value");
    tick=g_sensor_cycle.next_start_ms;Node_Poll(tick);
    tick=g_sensor_cycle.started_ms+NODE_SENSOR_ACTIVE_MS;Node_Poll(tick);advance(1100);
    CHECK(g_node.samples==3&&!g_sensor_cycle.average_pending,"empty window never manufactures a sample");
    NodeIdentity_Select(1,true);
    NodeMoveIntent fresh={2,1,{1,2},false};
    CHECK(NodeSettings_BeginMove(&fresh)==NODECFG_OK,"persist a change of sampling site");
    config_stage=CONFIG_MOVE;
    modem.binding_result=IDENTITY_BOUND;
    CHECK(finish_configuration(tick) && NodeIdentity_Number()==2 &&
          !Model_GetSnapshot()->soil_temp_valid,"fresh-site ACK clears old-site display values");
    advance(1);
    CHECK(sensor_power && g_sensor_cycle.phase && g_sensor_cycle.air_count==0 && g_sensor_cycle.soil_count==0,
          "new site starts a fresh powered cycle without waiting through the old rest period");
    /* Successful old values must disappear on a program restart. */
    Node_Init();
    CHECK(!Model_GetSnapshot()->air_temp_valid&&!Model_GetSnapshot()->soil_temp_valid,
          "restart clears retained readings even with CSV history on the card");
    printf("Sensor duty-cycle/mean/display regression failures: %u\n",failures);
    return failures?1:0;
}
