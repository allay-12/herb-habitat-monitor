/* Reuse the real orchestration/HAL harness with the shipping boot profile.
   No NODE command is sent to start acquisition or the modem in this test. */
#define main manual_profile_regression_main
#include "test_node_app.c"
#undef main

int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    TestDisk_SetImage(argv[1]);TestDisk_AllowWrites(1);History_Init();Model_Init();
    allow_modem=true;
    Node_Init();advance(100);
    CHECK(!g_node.sensor_enabled&&!g_node.modem_enabled&&!uart2_init&&!uart3_init,
          "unconfigured first boot leaves the node selection available");
    CHECK(NodeSettings_Save(1)==NODECFG_OK,"prepare a saved node identity on the real FAT volume");
    Node_Init();
    CHECK(NodeIdentity_Saved()&&NodeIdentity_Number()==1,"boot restores the saved node");
    CHECK(g_node.sensor_enabled&&g_node.modem_enabled&&uart3_init==1&&uart2_init==1,
          "saved node automatically starts sensor and modem communication without a command");
    CHECK(!power_writes,"automatic startup never drives MOS power pins");
    if(!g_node.sensor_enabled||!g_node.modem_enabled)goto done;
    nonce_ready=true;nonce[0]=1;nonce[1]=2;nonce[2]=3;
    advance(10);
    modem_step("AT\r","OK\r\n");modem_step("ATE0","OK\r\n");modem_step("AT+CMEE","OK\r\n");
    modem_step("AT+CGMR","host-firmware\r\nOK\r\n");modem_step("AT+CPIN?","+CPIN: READY\r\nOK\r\n");
    modem_step("AT+CEREG?","+CEREG:0,1\r\nOK\r\n");modem_step("AT+CGATT?","+CGATT:1\r\nOK\r\n");
    modem_step("AT+CCLK?","+CCLK: \"26/11/04,20:00:00+32\"\r\nOK\r\n");
    modem_step("AT+MDISCONNECT","ERROR\r\n");modem_step("AT+MIPCLOSE","ERROR\r\n");
    modem_step("AT+FSCREATE","ERROR\r\n");modem_step("AT+FSWRITE","> ");
    modem_step("-----BEGIN CERTIFICATE-----","OK\r\n");
    modem_step("AT+SSLCFG=\"cacert\"","OK\r\n");modem_step("AT+SSLCFG=\"seclevel\"","OK\r\n");
    modem_step("AT+SSLCFG=\"ignorelocaltime\"","OK\r\n");modem_step("AT+SSLCFG=\"sslversion\"","OK\r\n");
    modem_step("AT+MQTTMODE","OK\r\n");modem_step("AT+MQTTMSGSET","OK\r\n");
    modem_step("AT+MCONFIG","OK\r\n");modem_step("AT+SSLMIPSTART","OK\r\nCONNECTOK\r\n");
    modem_step("AT+MCONNECT","OK\r\nCONNACK OK\r\n");modem_step("AT+MSUB","OK\r\nSUBACK\r\n");
    advance(1100);advance(10);modem_step("AT+MPUBEX","> ");bind_reply("bound");
    CHECK(g_node.online&&g_node.clock_valid&&NodeIdentity_Binding()==IDENTITY_BOUND,
          "automatic modem flow obtains clock and server binding");
    CHECK(!requests&&!g_node.samples,"automatic boot still respects sensor warm-up");
    advance(NODE_SENSOR_WARMUP_MS+100);
    CHECK(requests==1&&request[0]==2,"automatic boot polls air first");response(false);
    advance(NODE_SENSOR_GAP_MS+10);
    CHECK(requests==2&&request[0]==1,"automatic boot polls soil on the same bus");response(true);
    advance(1100);advance(10);
    CHECK(g_node.samples==1&&capture.air_valid&&capture.soil_valid,
          "both sensor replies create one durable real snapshot without a start command");
    CHECK(modem.payload_size&&!modem.binding_mode&&!strstr(modem.payload,"\"_test\""),
          "automatic sample enters the normal upload path without a test marker");
    modem_step("AT+MPUBEX","> ");
    char ack[240],urc[400];
    snprintf(ack,sizeof(ack),"{\"sample_id\":\"%s\",\"stored\":true}",modem.sample_id);
    snprintf(urc,sizeof(urc),"+MSUB:%s,%u,%s\r\n",NodeIdentity_AckTopic(),(unsigned)strlen(ack),ack);
    modem_reply(urc);modem_reply("OK\r\nPUBACK\r\n");advance(1100);
    CHECK(g_node.stored==1&&!power_writes,"automatic upload completes only with matched stored ACK and no MOS writes");
done:
    printf("Node automatic boot/SD/upload regression failures: %u\n",failures);return failures?1:0;
}
