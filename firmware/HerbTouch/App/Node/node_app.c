#include "node_app.h"
#include "node_config.h"
#include "air_sensor.h"
#include "sensor_bus.h"
#include "node_clock.h"
#include "air780.h"
#include "node_store.h"
#include "node_identity.h"
#include "node_settings.h"
#include "node_upload_test.h"
#include "app_model.h"
#include "app_ui.h"
#include "history.h"
#include "history_fs.h"
#include "main.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
volatile NodeReport g_node;
volatile NodeUploadTest g_upload_test;
volatile NodeSensorCycleReport g_sensor_cycle;
typedef struct {
    UART_HandleTypeDef *uart;volatile uint16_t head,tail;volatile bool overflow,error;
    uint8_t rx,ring[4096],tx[2048];
} Serial;
static Serial modem_serial,sensor_serial;
static Air780 modem;
static SensorBus sensors;
static NodeClock local_clock;
static uint32_t sample_due,queue_due,published_frames[2];
static bool save_pending,published_valid[2];
static NodeRecord queued,capture;
static NodeRecord upload_test_record;
static uint32_t upload_test_due;
static uint32_t nonce[3],sequence;static bool nonce_ready;
static enum {CONFIG_IDLE,CONFIG_CHECK,CONFIG_DRAIN,CONFIG_PREPARE,CONFIG_MOVE} config_stage;
static unsigned config_target;
static unsigned config_previous;
static uint32_t config_deadline;
static bool config_offered;
static char debug_line[40];static unsigned debug_size;
static char debug_tx[512];static unsigned debug_tx_size,debug_tx_pos;
static bool due(uint32_t now,uint32_t deadline){return (int32_t)(now-deadline)>=0;}
#if NODE_SENSOR_DUTY_CYCLE
static int64_t air_totals[3],soil_totals[7];
static AirReading average_air;
static SoilReading average_soil;
static uint32_t cycle_ended_ms;

static int32_t mean(int64_t total,uint32_t count)
{
    if(total<0)return (int32_t)((total-(int64_t)count/2)/count);
    return (int32_t)((total+(int64_t)count/2)/count);
}
static void cycle_begin(uint32_t now)
{
    memset(air_totals,0,sizeof(air_totals));
    memset(soil_totals,0,sizeof(soil_totals));
    g_sensor_cycle.air_count=g_sensor_cycle.soil_count=0;
    g_sensor_cycle.started_ms=now;
    g_sensor_cycle.next_start_ms=now+NODE_SENSOR_CYCLE_MS;
    sample_due=now+NODE_SENSOR_ACTIVE_MS;
    g_sensor_cycle.phase=1;
    sensors.waiting=false;sensors.size=0;
    sensors.valid[0]=sensors.valid[1]=false;
    sensor_serial.tail=sensor_serial.head;
    sensors.next_due[0]=sensors.next_due[1]=now+NODE_SENSOR_WARMUP_MS;
    sensors.quiet_until=now;sensors.next=0;
    HAL_GPIO_WritePin(SENSOR_PWR_EN_GPIO_Port,SENSOR_PWR_EN_Pin,GPIO_PIN_SET);
    Model_SetSensorCollecting(true);
}
static void cycle_finish(uint32_t now)
{
    if(!g_sensor_cycle.phase)return;
    HAL_GPIO_WritePin(SENSOR_PWR_EN_GPIO_Port,SENSOR_PWR_EN_Pin,GPIO_PIN_RESET);
    g_sensor_cycle.phase=0;g_sensor_cycle.completed++;
    sensors.waiting=false;sensors.size=0;
    sensors.valid[0]=sensors.valid[1]=false;
    sensor_serial.tail=sensor_serial.head;
    cycle_ended_ms=now;
    uint32_t n=g_sensor_cycle.air_count;
    if(n) {
        average_air.temperature_x10=(int16_t)mean(air_totals[0],n);
        average_air.humidity_x10=(uint16_t)mean(air_totals[1],n);
        average_air.lux=(uint32_t)((air_totals[2]+n/2)/n);
    }
    n=g_sensor_cycle.soil_count;
    if(n) {
        average_soil.temperature_x10=(int16_t)mean(soil_totals[0],n);
        average_soil.moisture_x10=(uint16_t)mean(soil_totals[1],n);
        average_soil.ec_us_cm=(uint16_t)mean(soil_totals[2],n);
        average_soil.ph_x10=(uint16_t)mean(soil_totals[3],n);
        average_soil.n_mg_kg=(uint16_t)mean(soil_totals[4],n);
        average_soil.p_mg_kg=(uint16_t)mean(soil_totals[5],n);
        average_soil.k_mg_kg=(uint16_t)mean(soil_totals[6],n);
    }
    g_sensor_cycle.average_pending=g_sensor_cycle.air_count || g_sensor_cycle.soil_count;
    Model_SetSensorCollecting(false);
}
static void cycle_poll(uint32_t now)
{
    if(g_sensor_cycle.phase && due(now,sample_due))
        cycle_finish(now);
    if(!g_sensor_cycle.phase && g_node.sensor_enabled &&
       config_stage==CONFIG_IDLE && !g_sensor_cycle.average_pending &&
       !save_pending && due(now,g_sensor_cycle.next_start_ms))
        cycle_begin(now);
}
static void cycle_collect(unsigned sensor)
{
    if(!g_sensor_cycle.phase || !sensors.valid[sensor])return;
    if(sensor==SENSOR_AIR) {
        air_totals[0]+=sensors.air.temperature_x10;
        air_totals[1]+=sensors.air.humidity_x10;
        air_totals[2]+=sensors.air.lux;
        g_sensor_cycle.air_count++;
    } else {
        soil_totals[0]+=sensors.soil.temperature_x10;
        soil_totals[1]+=sensors.soil.moisture_x10;
        soil_totals[2]+=sensors.soil.ec_us_cm;
        soil_totals[3]+=sensors.soil.ph_x10;
        soil_totals[4]+=sensors.soil.n_mg_kg;
        soil_totals[5]+=sensors.soil.p_mg_kg;
        soil_totals[6]+=sensors.soil.k_mg_kg;
        g_sensor_cycle.soil_count++;
    }
}
#endif
static void serial_init(Serial *s,UART_HandleTypeDef *uart)
{memset(s,0,sizeof(*s));s->uart=uart;HAL_UART_Receive_IT(uart,&s->rx,1);}
static bool transmit(Serial *s,const uint8_t *data,unsigned size)
{
    if(!s->uart || size>sizeof(s->tx) || s->uart->gState!=HAL_UART_STATE_READY)return false;
    memcpy(s->tx,data,size);return HAL_UART_Transmit_IT(s->uart,s->tx,(uint16_t)size)==HAL_OK;
}
static bool modem_send(const uint8_t *data,unsigned size){return transmit(&modem_serial,data,size);}
static bool sensor_send(const uint8_t *data,unsigned size)
{
    /* Old RX bytes have already been drained by sensor_poll. Discard anything
       arriving in the idle gap before starting the next addressed request. */
    sensor_serial.tail=sensor_serial.head;return transmit(&sensor_serial,data,size);
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    Serial *s=uart==modem_serial.uart?&modem_serial:uart==sensor_serial.uart?&sensor_serial:0;
    if(!s)return;
    uint16_t next=(s->head+1u)&4095u;
    if(next==s->tail)s->overflow=true;else{s->ring[s->head]=s->rx;__DMB();s->head=next;}
    if(HAL_UART_Receive_IT(uart,&s->rx,1)!=HAL_OK)s->error=true;
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    Serial *s=uart==modem_serial.uart?&modem_serial:uart==sensor_serial.uart?&sensor_serial:0;
    if(s)s->error=true;
}
static bool pop(Serial *s,uint8_t *b)
{
    if(s->tail==s->head)return false;
    *b=s->ring[s->tail];__DMB();s->tail=(s->tail+1u)&4095u;return true;
}
static bool recover(Serial *s)
{
    if(!s->uart || (!s->error && !s->overflow))return false;
    /* Run recovery outside the IRQ; discard an incomplete frame/AT stream. */
    HAL_NVIC_DisableIRQ(s->uart->Instance==USART2?USART2_IRQn:USART3_IRQn);
    HAL_UART_AbortReceive(s->uart);s->tail=s->head;s->error=false;s->overflow=false;
    __HAL_UART_CLEAR_OREFLAG(s->uart);HAL_UART_Receive_IT(s->uart,&s->rx,1);
    HAL_NVIC_EnableIRQ(s->uart->Instance==USART2?USART2_IRQn:USART3_IRQn);
    g_node.uart_errors++;return true;
}
static void leave_preview(void)
{
    if(!Model_IsDisplayTest())return;
    History_Cancel();HistoryFS_SetTestMode(false);Model_ClearDisplayTest();
    UI_ShowDataPage(UI_PAGE_OVERVIEW);
}
static void sensor_start(uint32_t now)
{
    leave_preview();
    if(g_node.sensor_enabled)return;
    MX_USART3_UART_Init();huart3.Init.BaudRate=NODE_AIR_BAUD;HAL_UART_Init(&huart3);
    serial_init(&sensor_serial,&huart3);
    /* Serial polling resumes with a new powered measurement window. */
    SensorBus_Init(&sensors,sensor_send,now);
    memset(published_frames,0,sizeof(published_frames));memset(published_valid,0,sizeof(published_valid));
    g_node.sensor_enabled=1;sample_due=now+NODE_SENSOR_WARMUP_MS;
#if NODE_SENSOR_DUTY_CYCLE
    if(config_stage==CONFIG_IDLE && !g_sensor_cycle.average_pending)cycle_begin(now);
#endif
}
static void modem_start(uint32_t now)
{
    if(g_node.modem_enabled)return;
    if((!NodeIdentity_Saved() && config_stage==CONFIG_IDLE) || !NodeIdentity_CredentialsReady()) {
        snprintf((char*)g_node.modem_fault,sizeof(g_node.modem_fault),"Save node config / provision device UID credential");return;
    }
    leave_preview();
    MX_USART2_UART_Init();huart2.Init.BaudRate=NODE_MODEM_BAUD;HAL_UART_Init(&huart2);
    serial_init(&modem_serial,&huart2);
    /* The carrier uses its independent 5V supply; PB1 stays reserved/low. */
    /* PA1 stays analog. Use the board's verified auto-start/manual power key. */
    g_node.modem_enabled=1;Air780_Start(&modem,now);
}
static bool random_nonce(void)
{
    __HAL_RCC_RNG_CLK_ENABLE();RNG->CR=RNG_CR_RNGEN;
    for(unsigned i=0;i<3;i++) {
        uint32_t start=HAL_GetTick();
        while(!(RNG->SR&RNG_SR_DRDY)) {
            if((RNG->SR&(RNG_SR_CECS|RNG_SR_SECS)) || HAL_GetTick()-start>5){RNG->CR=0;return false;}
        }
        if(RNG->SR&(RNG_SR_CECS|RNG_SR_SECS)){RNG->CR=0;return false;}
        nonce[i]=RNG->DR;
        if(i && nonce[i]==nonce[i-1]){RNG->CR=0;return false;}
    }
    RNG->CR=0;return true;
}
void Node_Init(void)
{
    memset((void*)&g_node,0,sizeof(g_node));
    NodeIdentity_Init(HAL_GetUIDw0(),HAL_GetUIDw1(),HAL_GetUIDw2());
    g_node.config_result=NodeSettings_Load();
    Model_Init();Model_SetNode(NodeIdentity_Id());
    memset((void*)&g_sensor_cycle,0,sizeof(g_sensor_cycle));
    queue_due=HAL_GetTick();
#if NODE_SENSOR_DUTY_CYCLE
    HAL_GPIO_WritePin(SENSOR_PWR_EN_GPIO_Port,SENSOR_PWR_EN_Pin,GPIO_PIN_RESET);
#endif
    Air780_Init(&modem,modem_send);NodeClock_Init(&local_clock,HAL_GetTick());
    config_stage=CONFIG_IDLE;
    config_offered=false;
    if(NodeSettings_PendingMove()->target) {
        config_stage=CONFIG_MOVE;
        config_target=NodeSettings_PendingMove()->target;
        g_node.config_node_request=config_target;
        g_node.config_result=NODECFG_CONFIRMING;
        modem_start(HAL_GetTick());
    }
    /* A new/unreadable SD identity must remain idle so the user can configure
       its node. A saved node resumes continuous externally powered sampling. */
    if(NODE_AUTO_SENSOR && NodeIdentity_Saved())sensor_start(HAL_GetTick());
    if(NODE_AUTO_MODEM)modem_start(HAL_GetTick());
}
static void request_configuration(unsigned target,bool move,uint32_t now)
{
    if(config_stage!=CONFIG_IDLE)return;
    if(target<1 || target>NODE_MAX_NUMBER){g_node.config_result=NODECFG_CORRUPT;return;}
    if(!NodeIdentity_CredentialsReady()){g_node.config_result=NODECFG_DENIED;return;}
    if(move && (g_upload_test.state==1 || (!NodeIdentity_Saved() && g_node.sensor_enabled))) {
        g_node.config_result=NODECFG_BUSY;
        return;
    }
#if NODE_SENSOR_DUTY_CYCLE
    if(move)cycle_finish(now);
#endif
    config_target=target;
    config_stage=move?CONFIG_DRAIN:CONFIG_CHECK;
    config_deadline=now+90000u;
    g_node.config_result=NODECFG_CONNECTING;
    modem_start(now);
}
static void console(uint32_t now)
{
    /* Debug USART1 remains 115200 8N1. No blocking reads and no secret echo. */
    for(unsigned i=0;i<32 && (__HAL_UART_GET_FLAG(&huart1,UART_FLAG_RXNE)!=RESET);i++) {
        char c=(char)(huart1.Instance->DR&255u);
        if(c=='\n' || c=='\r') {
            debug_line[debug_size]=0;
            bool recognized=true;
            if(!strcmp(debug_line,"NODE SENSOR ON"))sensor_start(now);
            else if(!strcmp(debug_line,"NODE MODEM ON"))modem_start(now);
            else if(!strcmp(debug_line,"NODE START")){sensor_start(now);modem_start(now);}
            else if(!strcmp(debug_line,"NODE BIND RETRY"))NodeIdentity_SetBinding(IDENTITY_PENDING);
            else if(!strcmp(debug_line,"NODE UPLOAD TEST"))g_upload_test.command=1;
            else if(!strncmp(debug_line,"NODE TIME ",10)) {
                uint32_t stamp;
                if(!HistoryDate_Parse(debug_line+10,&stamp) || !NodeClock_Set(&local_clock,stamp,now,NODE_CLOCK_MANUAL))g_node.time_set_errors++;
            }
            else if(strcmp(debug_line,"NODE STATUS"))recognized=false;
            if(recognized) {
                debug_tx_pos=0;
                debug_tx_size=(unsigned)snprintf(debug_tx,sizeof(debug_tx),
                    "%s sensor=%lu frames=%lu errors=%lu modem=%lu online=%lu time=%lu samples=%lu stored=%lu sd=%lu air=%lu soil=%lu airerr=%lu soilerr=%lu clock=%lu seterr=%lu bind=%u\r\nAT: %s\r\nFAULT: %s\r\n",
                    NodeIdentity_Id(),
                    (unsigned long)g_node.sensor_enabled,(unsigned long)g_node.sensor_frames,(unsigned long)g_node.sensor_errors,
                    (unsigned long)g_node.modem_enabled,(unsigned long)g_node.online,(unsigned long)g_node.clock_valid,
                    (unsigned long)g_node.samples,(unsigned long)g_node.stored,(unsigned long)g_node.sd_error,
                    (unsigned long)g_node.air_frames,(unsigned long)g_node.soil_frames,(unsigned long)g_node.air_errors,(unsigned long)g_node.soil_errors,
                    (unsigned long)local_clock.source,(unsigned long)g_node.time_set_errors,
                    NodeIdentity_Binding(),
                    (const char*)g_node.modem_last_line,(const char*)g_node.modem_fault);
                if(debug_tx_size>=sizeof(debug_tx))debug_tx_size=sizeof(debug_tx)-1;
            }
            debug_size=0;
        } else if(debug_size+1<sizeof(debug_line))debug_line[debug_size++]=c;else debug_size=0;
    }
    if(__HAL_UART_GET_FLAG(&huart1,UART_FLAG_ORE)!=RESET)__HAL_UART_CLEAR_OREFLAG(&huart1);
    if(debug_tx_pos<debug_tx_size && __HAL_UART_GET_FLAG(&huart1,UART_FLAG_TXE)!=RESET)
        huart1.Instance->DR=(uint8_t)debug_tx[debug_tx_pos++];
    uint32_t command=g_node.command;g_node.command=0;
    if(command==1 || command==3)sensor_start(now);
    if(command==2 || command==3)modem_start(now);
    if(command==4 && !NodeClock_Set(&local_clock,g_node.time_to_set,now,NODE_CLOCK_MANUAL))g_node.time_set_errors++;
    if(command==5) {
        unsigned old=NodeIdentity_Number();
        g_node.config_result=NodeSettings_Save(g_node.config_node_request);
        if(g_node.config_result==NODECFG_OK) {
            if(old!=NodeIdentity_Number()){HistoryFS_SetTestMode(false);Model_Init();}
            Model_SetNode(NodeIdentity_Id());
        }
    }
    if(command==6 || command==7)request_configuration(g_node.config_node_request,command==7,now);
}
static void sensor_poll(uint32_t now)
{
    if(!g_node.sensor_enabled)return;
    if(recover(&sensor_serial))SensorBus_ResetLink(&sensors,now);
    uint8_t c;
    for(unsigned i=0;i<256 && pop(&sensor_serial,&c);i++)SensorBus_Feed(&sensors,&c,1,now);
#if NODE_SENSOR_DUTY_CYCLE
    if(g_sensor_cycle.phase)SensorBus_Poll(&sensors,now);
#else
    SensorBus_Poll(&sensors,now);
#endif
    for(unsigned i=0;i<2;i++) {
        if(published_frames[i]!=sensors.frames[i] || published_valid[i]!=sensors.valid[i]) {
#if NODE_SENSOR_DUTY_CYCLE
            if(published_frames[i]!=sensors.frames[i])cycle_collect(i);
            if(!g_sensor_cycle.phase) {
                published_frames[i]=sensors.frames[i];published_valid[i]=sensors.valid[i];
                continue;
            }
#endif
            if(i==SENSOR_AIR)Model_SetAir(sensors.valid[i],sensors.air.temperature_x10/10.0f,sensors.air.humidity_x10/10.0f,sensors.air.lux,now);
            else Model_SetSoil(sensors.valid[i],sensors.soil.temperature_x10/10.0f,sensors.soil.moisture_x10/10.0f,
                sensors.soil.ec_us_cm,sensors.soil.ph_x10/10.0f,sensors.soil.n_mg_kg,sensors.soil.p_mg_kg,sensors.soil.k_mg_kg,now);
            published_frames[i]=sensors.frames[i];published_valid[i]=sensors.valid[i];
        }
    }
    g_node.air_frames=sensors.frames[0];g_node.soil_frames=sensors.frames[1];
    g_node.air_errors=sensors.errors[0];g_node.soil_errors=sensors.errors[1];
    g_node.sensor_frames=sensors.frames[0]+sensors.frames[1];g_node.sensor_errors=sensors.errors[0]+sensors.errors[1];
}
static unsigned configuration_result(unsigned result,bool moved)
{
    switch(result) {
    case IDENTITY_BOUND:return moved?NODECFG_OK:NODECFG_OWNED;
    case IDENTITY_NODE_USED:return NODECFG_OCCUPIED;
    case IDENTITY_DENIED:return NODECFG_DENIED;
    case 5:return NODECFG_AVAILABLE;
    default:return NODECFG_CONFLICT;
    }
}
static bool finish_configuration(uint32_t now)
{
    bool moved=config_stage==CONFIG_MOVE;
    unsigned reply=modem.binding_result;
    if(moved) {
        if(NodeSettings_FinishMove(reply==IDENTITY_BOUND)!=NODECFG_OK) {
            g_node.config_result=NODECFG_RETRY_SAVE;
            return false;
        }
        if(reply==IDENTITY_BOUND) {
            NodeIdentity_SetBinding(IDENTITY_BOUND);
            Model_SetNode(NodeIdentity_Id());
            sample_due=now;
            if(NODE_AUTO_SENSOR)sensor_start(now);
        } else if(NodeIdentity_Saved())NodeIdentity_SetBinding(IDENTITY_PENDING);
    }
    config_stage=CONFIG_IDLE;
    config_offered=false;
    g_node.config_result=configuration_result(reply,moved);
    return true;
}
static void offer_configuration(void)
{
    char request_id[96],json[256];
    if(config_stage==CONFIG_MOVE) {
        const NodeMoveIntent *intent=NodeSettings_PendingMove();
        snprintf(request_id,sizeof(request_id),"move-%08lX%08lX",
                 (unsigned long)intent->nonce[0],(unsigned long)intent->nonce[1]);
        snprintf(json,sizeof(json),
                 "{\"action\":\"move\",\"device_id\":\"node%02u\",\"device_uid\":\"%s\",\"request_id\":\"%s\",\"previous\":%u}",
                 config_target,NodeIdentity_UID(),request_id,(unsigned)intent->previous);
    } else {
        if(!nonce_ready)nonce_ready=random_nonce();
        if(!nonce_ready)return;
        snprintf(request_id,sizeof(request_id),"check-%08lX%08lX%08lX-%08lX",
                 (unsigned long)nonce[0],(unsigned long)nonce[1],(unsigned long)nonce[2],(unsigned long)++sequence);
        snprintf(json,sizeof(json),
                 "{\"action\":\"check\",\"device_id\":\"node%02u\",\"device_uid\":\"%s\",\"request_id\":\"%s\"}",
                 config_target,NodeIdentity_UID(),request_id);
    }
    config_offered=Air780_OfferNodeRequest(&modem,config_target,request_id,json);
    if(config_offered)g_node.config_result=config_stage==CONFIG_MOVE?NODECFG_CONFIRMING:NODECFG_CHECKING;
}
static void storage_poll(uint32_t now)
{
    uint32_t stamp;
    bool capture_allowed=config_stage==CONFIG_IDLE || config_stage==CONFIG_CHECK;
    const AirReading *air=sensors.valid[0]?&sensors.air:0;
    const SoilReading *soil=sensors.valid[1]?&sensors.soil:0;
    bool snapshot_ready;
#if NODE_SENSOR_DUTY_CYCLE
    capture_allowed=capture_allowed || config_stage==CONFIG_DRAIN;
    air=g_sensor_cycle.air_count?&average_air:0;
    soil=g_sensor_cycle.soil_count?&average_soil:0;
    snapshot_ready=g_sensor_cycle.average_pending!=0;
#else
    bool first_round_done=(sensors.frames[0] || sensors.errors[0]) && (sensors.frames[1] || sensors.errors[1]);
    snapshot_ready=first_round_done && (air || soil) && due(now,sample_due);
#endif
    if(capture_allowed && snapshot_ready && !save_pending && NodeClock_Get(&local_clock,&stamp)) {
        if(!nonce_ready)nonce_ready=random_nonce();
        if(!nonce_ready){g_node.queue_errors++;return;}
        if(sequence==0xffffffffu){nonce_ready=false;sequence=0;return;}
        char id[128];
        snprintf(id,sizeof(id),"%s-%08lX%08lX%08lX-%08lX%08lX%08lX-%08lX",
            NodeIdentity_Id(),
            (unsigned long)HAL_GetUIDw0(),(unsigned long)HAL_GetUIDw1(),(unsigned long)HAL_GetUIDw2(),
            (unsigned long)nonce[0],(unsigned long)nonce[1],(unsigned long)nonce[2],(unsigned long)++sequence);
#if NODE_SENSOR_DUTY_CYCLE
        /* If network time arrived late, recover the window-end timestamp from
           its elapsed monotonic time rather than stamping it as a new sample. */
        uint32_t elapsed=(now-cycle_ended_ms)/1000u;
        if(stamp<elapsed)return;
        stamp-=elapsed;
#endif
        save_pending=NodeRecord_BuildSensors(&capture,id,air,soil,stamp);
#if NODE_SENSOR_DUTY_CYCLE
        if(save_pending) {
            unsigned n=(unsigned)strlen(capture.json);
            int added=snprintf(capture.json+n-1,sizeof(capture.json)-n+1,
                ",\"aggregation\":\"mean\",\"window_seconds\":%lu,\"air_sample_count\":%lu,\"soil_sample_count\":%lu}",
                (unsigned long)((cycle_ended_ms-g_sensor_cycle.started_ms)/1000u),
                (unsigned long)g_sensor_cycle.air_count,(unsigned long)g_sensor_cycle.soil_count);
            if(added<0 || (unsigned)added>=sizeof(capture.json)-n+1) {
                save_pending=false;g_node.queue_errors++;return;
            }
            g_sensor_cycle.average_pending=0;
        }
#else
        sample_due=now+NODE_SAMPLE_MS;
#endif
    }
    if(!due(now,queue_due))return;
    queue_due=now+1000; /* back off filesystem work while card/history is busy */
    if(save_pending) {
        if(NodeStore_Save(&capture,nonce[0]^sequence)) {
            save_pending=false;g_node.samples++;
            if(NodeStore_Error())g_node.history_errors++;
        } else g_node.queue_errors++;
    }
    if(Air780_CommitReady(&modem)) {
        if(modem.binding_mode) {
            if(config_offered) {
                if(!finish_configuration(now))return;
            } else if(modem.binding_result<=IDENTITY_DENIED)NodeIdentity_SetBinding(modem.binding_result);
            Air780_Release(&modem);
        }
        else if(NodeStore_Commit(&queued)){
            g_node.stored++;
            if(g_upload_test.state==2 && !strcmp(queued.id,(const char*)g_upload_test.sample_id))g_upload_test.state=3;
            Air780_Release(&modem);
        }
    }
    if((config_stage==CONFIG_CHECK || config_stage==CONFIG_DRAIN) && due(now,config_deadline)) {
        if(config_offered) {
            Air780_Fault(&modem,"Node query timeout",now);
            Air780_Release(&modem);
            config_offered=false;
        }
        config_stage=CONFIG_IDLE;
        g_node.config_result=NODECFG_TIMEOUT;
    }
    if(config_stage==CONFIG_PREPARE && !modem.payload_size) {
        if(!nonce_ready)nonce_ready=random_nonce();
        if(!nonce_ready)return;
        NodeMoveIntent intent={config_target,config_previous,{nonce[0]^nonce[2],nonce[1]^(++sequence)}};
        if(NodeSettings_BeginMove(&intent)!=NODECFG_OK) {
            g_node.config_result=NODECFG_RETRY_SAVE;
            return;
        }
        config_stage=CONFIG_MOVE;
    }
    if(!modem.payload_size && modem.online && (config_stage==CONFIG_CHECK || config_stage==CONFIG_MOVE))
        offer_configuration();
    if(config_stage==CONFIG_MOVE)return;
    if(g_node.modem_enabled && modem.online && !modem.payload_size && NodeIdentity_Saved() && NodeIdentity_Binding()==IDENTITY_PENDING) {
        if(!nonce_ready)nonce_ready=random_nonce();
        if(nonce_ready) {
            char request_id[96],json[256];
            snprintf(request_id,sizeof(request_id),"bind-%s-%u-%08lX-%08lX",NodeIdentity_UID(),NodeIdentity_Number(),(unsigned long)nonce[0],(unsigned long)++sequence);
            snprintf(json,sizeof(json),"{\"device_id\":\"%s\",\"device_uid\":\"%s\",\"request_id\":\"%s\"}",NodeIdentity_Id(),NodeIdentity_UID(),request_id);
            Air780_OfferBinding(&modem,request_id,json);
        }
    }
    if(modem.online && !modem.payload_size) {
        unsigned binding=NodeIdentity_Binding();
        if(binding==IDENTITY_BOUND && NodeStore_Load(&queued))Air780_Offer(&modem,queued.id,queued.json);
        if(config_stage==CONFIG_DRAIN && !modem.payload_size && !save_pending
#if NODE_SENSOR_DUTY_CYCLE
           && !g_sensor_cycle.average_pending
#endif
        ) {
            if(!NodeIdentity_Saved() || (binding==IDENTITY_BOUND && NodeStore_QueueDrained())) {
                config_previous=NodeIdentity_Saved()?NodeIdentity_Number():0;
                config_stage=CONFIG_PREPARE;
            } else if(binding==IDENTITY_NODE_USED) {
                bool has_unsent=NodeStore_Load(&queued);
                if(has_unsent) {
                    config_stage=CONFIG_IDLE;
                    g_node.config_result=NODECFG_CONFLICT;
                } else if(NodeStore_QueueDrained()) {
                    config_previous=0;
                    config_stage=CONFIG_PREPARE;
                }
            } else if(binding==IDENTITY_DEVICE_USED || binding==IDENTITY_DENIED) {
                config_stage=CONFIG_IDLE;
                g_node.config_result=configuration_result(binding,true);
            }
        }
    }
    if(config_stage==CONFIG_DRAIN && modem.online)g_node.config_result=NODECFG_DRAINING;
    g_node.sd_error=NodeStore_Error();
}
static void upload_test_poll(uint32_t now)
{
    if(g_upload_test.command) {
        g_upload_test.command=0;
        if(g_upload_test.state==1 || g_upload_test.state==2){g_upload_test.result=2;return;}
        uint32_t stamp;
        if(g_node.sensor_enabled || !modem.online || NodeIdentity_Binding()!=IDENTITY_BOUND || !NodeClock_Get(&local_clock,&stamp)) {
            g_upload_test.state=4;g_upload_test.result=1;return;
        }
        if(!nonce_ready)nonce_ready=random_nonce();
        if(!nonce_ready || sequence==0xffffffffu){g_upload_test.state=4;g_upload_test.result=3;return;}
        char id[128];
        snprintf(id,sizeof(id),"%s-UPLINK-%s-%08lX%08lX%08lX-%08lX",NodeIdentity_Id(),NodeIdentity_UID(),
            (unsigned long)nonce[0],(unsigned long)nonce[1],(unsigned long)nonce[2],(unsigned long)++sequence);
        AirReading air={256,658,12000};SoilReading soil={232,451,500,65,30,20,50};
        if(!NodeRecord_BuildSensors(&upload_test_record,id,&air,&soil,stamp)) {
            g_upload_test.state=4;g_upload_test.result=3;return;
        }
        unsigned n=(unsigned)strlen(upload_test_record.json);
        const char *mark=",\"_test\":true,\"_display_test\":true,\"test_source\":\"f407_air780e_uplink\"}";
        if(!n || n+strlen(mark)>=sizeof(upload_test_record.json)){g_upload_test.state=4;g_upload_test.result=3;return;}
        strcpy(upload_test_record.json+n-1,mark);
        strcpy((char*)g_upload_test.sample_id,id);g_upload_test.archive_path[0]=0;
        g_upload_test.stamp=stamp;g_upload_test.attempts=0;g_upload_test.sd_error=0;
        g_upload_test.state=1;g_upload_test.result=0;upload_test_due=now;
    }
    if(g_upload_test.state!=1 || !due(now,upload_test_due))return;
    upload_test_due=now+1000;g_upload_test.attempts++;
    if(NodeStore_SaveUploadTest(&upload_test_record,nonce[0]^sequence)) {
        g_upload_test.state=2;g_upload_test.sd_error=NodeStore_Error();
        snprintf((char*)g_upload_test.archive_path,sizeof(g_upload_test.archive_path),"%s",NodeStore_UploadTestArchive());
    } else {
        g_upload_test.sd_error=NodeStore_Error();
        if(g_upload_test.attempts>=30){g_upload_test.state=4;g_upload_test.result=4;}
    }
}
void Node_Poll(uint32_t now)
{
#if NODE_SENSOR_DUTY_CYCLE
    cycle_poll(now);
#endif
    console(now); /* Status is available in preview; an explicit START leaves it. */
    NodeClock_Poll(&local_clock,now);
    if(Model_IsDisplayTest()) {
        uint32_t stamp=0;g_node.clock_valid=NodeClock_Get(&local_clock,&stamp);
        g_node.local_seconds=stamp;g_node.clock_source=local_clock.source;
        g_node.node_number=NodeIdentity_Number();g_node.binding_state=NodeIdentity_Binding();return;
    }
    sensor_poll(now);
    if(g_node.modem_enabled) {
        Air780_Poll(&modem,now);
        if(!modem.online && NodeIdentity_Binding()==IDENTITY_BOUND) {
            NodeIdentity_SetBinding(IDENTITY_PENDING);
            if(modem.payload_size && !modem.binding_mode)Air780_Release(&modem); /* SD original remains queued. */
        }
        if(recover(&modem_serial))Air780_Fault(&modem,"UART receive overrun",now);
        uint8_t bytes[256];unsigned n=0;
        while(n<sizeof(bytes) && pop(&modem_serial,&bytes[n]))n++;
        if(n)Air780_Feed(&modem,bytes,n);
        /* CONNECT OK -> MCONNECT is advanced immediately, before drawing. */
        Air780_Poll(&modem,now);
        g_node.online=modem.online;uint32_t network_time;
        if(Air780_Time(&modem,now,&network_time))NodeClock_Set(&local_clock,network_time,now,NODE_CLOCK_NETWORK);
        memcpy((void*)g_node.modem_last_line,modem.last_line,sizeof(modem.last_line));
        memcpy((void*)g_node.modem_fault,modem.fault,sizeof(modem.fault));
    }
    uint32_t local_time=0;g_node.clock_valid=NodeClock_Get(&local_clock,&local_time);
    g_node.local_seconds=local_time;g_node.clock_source=local_clock.source;
    g_node.node_number=NodeIdentity_Number();g_node.binding_state=NodeIdentity_Binding();
    if(config_stage==CONFIG_IDLE)upload_test_poll(now);
    if(g_node.sensor_enabled || g_node.modem_enabled)storage_poll(now);
}
