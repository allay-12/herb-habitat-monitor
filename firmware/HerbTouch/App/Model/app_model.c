#include "app_model.h"
#include "node_identity.h"
#include <string.h>
#include <stdio.h>

static HerbDataModel_t s_model;
static uint32_t revision;
static bool display_test;
static HerbDataModel_t before_test;
static bool air_seen,soil_seen;

void Model_Init(void)
{
    revision=0;
    display_test=false;
    air_seen=soil_seen=false;
    memset(&s_model, 0, sizeof(s_model));

    /* In Phase 1, sensors are not connected: valid = false */
    s_model.air_temp_valid   = false;
    s_model.air_humi_valid   = false;
    s_model.light_valid      = false;
    s_model.battery_valid    = false;
    s_model.battery_pct      = -1; /* Unknown */

    s_model.soil_temp_valid  = false;
    s_model.soil_moist_valid = false;
    s_model.soil_ec_valid    = false;
    s_model.soil_ph_valid    = false;
    s_model.soil_npk_valid   = false;

    strncpy(s_model.node_id, NodeIdentity_Id(), sizeof(s_model.node_id) - 1);
    strncpy(s_model.update_time, "--", sizeof(s_model.update_time) - 1);
    strncpy(s_model.sensor_status, "未接入", sizeof(s_model.sensor_status) - 1);
    strcpy(s_model.air_status,"未接入");strcpy(s_model.soil_status,"未接入");
}

const HerbDataModel_t* Model_GetSnapshot(void)
{
    return &s_model;
}
void Model_SetNode(const char *id){snprintf(s_model.node_id,sizeof(s_model.node_id),"%s",id);revision++;}
void Model_SetAir(bool valid,float temperature,float humidity,uint32_t lux,uint32_t now)
{
    s_model.air_temp_valid=valid;s_model.air_humi_valid=valid;s_model.light_valid=valid;
    if(valid){air_seen=true;s_model.air_temperature_c=temperature;s_model.air_humidity_pct=humidity;s_model.illuminance_lux=lux;}
    if(valid)snprintf(s_model.update_time,sizeof(s_model.update_time),"%lus",(unsigned long)(now/1000));
    snprintf(s_model.air_status,sizeof(s_model.air_status),"%s",valid?"已接入":"未接入");revision++;
}
void Model_SetSoil(bool valid,float temperature,float moisture,uint16_t ec,float ph,uint16_t n,uint16_t p,uint16_t k,uint32_t now)
{
    s_model.soil_temp_valid=s_model.soil_moist_valid=s_model.soil_ec_valid=s_model.soil_ph_valid=s_model.soil_npk_valid=valid;
    if(valid) {
        soil_seen=true;
        s_model.soil_temperature_c=temperature;s_model.soil_moisture_pct=moisture;
        s_model.soil_ec_us_cm=ec;s_model.soil_ph=ph;s_model.soil_n_mg_kg=n;s_model.soil_p_mg_kg=p;s_model.soil_k_mg_kg=k;
    }
    if(valid)snprintf(s_model.update_time,sizeof(s_model.update_time),"%lus",(unsigned long)(now/1000));
    snprintf(s_model.soil_status,sizeof(s_model.soil_status),"%s",valid?"已接入":"未接入");revision++;
}
uint32_t Model_Revision(void){return revision;}
void Model_SetSensorCollecting(bool collecting)
{
    if(!collecting) {
        s_model.air_temp_valid=s_model.air_humi_valid=s_model.light_valid=air_seen;
        s_model.soil_temp_valid=s_model.soil_moist_valid=s_model.soil_ec_valid=soil_seen;
        s_model.soil_ph_valid=s_model.soil_npk_valid=soil_seen;
    }
    const char *state=collecting?"采集中":"休息中";
    snprintf(s_model.sensor_status,sizeof(s_model.sensor_status),"%s",state);
    snprintf(s_model.air_status,sizeof(s_model.air_status),"%s",state);
    snprintf(s_model.soil_status,sizeof(s_model.soil_status),"%s",state);
    revision++;
}
bool Model_IsDisplayTest(void){return display_test;}
bool Model_SetDisplayTest(const float v[10],const char *clock)
{
    if(!v || !clock || v[2]<0 || v[2]>200000 || v[5]<0 || v[5]>65535 ||
       v[7]<0 || v[7]>65535 || v[8]<0 || v[8]>65535 || v[9]<0 || v[9]>65535)return false;
    if(!display_test)before_test=s_model;
    display_test=true;
    s_model.air_temp_valid=s_model.air_humi_valid=s_model.light_valid=true;
    s_model.air_temperature_c=v[0];s_model.air_humidity_pct=v[1];s_model.illuminance_lux=(uint32_t)v[2];
    s_model.soil_temp_valid=s_model.soil_moist_valid=s_model.soil_ec_valid=s_model.soil_ph_valid=s_model.soil_npk_valid=true;
    s_model.soil_temperature_c=v[3];s_model.soil_moisture_pct=v[4];s_model.soil_ec_us_cm=(uint16_t)v[5];s_model.soil_ph=v[6];
    s_model.soil_n_mg_kg=(uint16_t)v[7];s_model.soil_p_mg_kg=(uint16_t)v[8];s_model.soil_k_mg_kg=(uint16_t)v[9];
    s_model.battery_valid=false;s_model.battery_pct=-1;
    snprintf(s_model.update_time,sizeof(s_model.update_time),"%s",clock);
    snprintf(s_model.sensor_status,sizeof(s_model.sensor_status),"测试数据");
    strcpy(s_model.air_status,"测试数据");strcpy(s_model.soil_status,"测试数据");revision++;return true;
}
void Model_ClearDisplayTest(void)
{if(display_test){s_model=before_test;display_test=false;revision++;}}
