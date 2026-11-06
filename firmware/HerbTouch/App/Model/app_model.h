#ifndef __APP_MODEL_H
#define __APP_MODEL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool     air_temp_valid;
    float    air_temperature_c;

    bool     air_humi_valid;
    float    air_humidity_pct;

    bool     light_valid;
    uint32_t illuminance_lux;

    bool     battery_valid;
    float    battery_voltage_v;
    int8_t   battery_pct; /* -1: unknown */

    bool     soil_temp_valid;
    float    soil_temperature_c;

    bool     soil_moist_valid;
    float    soil_moisture_pct;

    bool     soil_ec_valid;
    uint16_t soil_ec_us_cm;

    bool     soil_ph_valid;
    float    soil_ph;

    bool     soil_npk_valid;
    uint16_t soil_n_mg_kg;
    uint16_t soil_p_mg_kg;
    uint16_t soil_k_mg_kg;

    char     node_id[16];
    char     update_time[16];
    char     sensor_status[16];
    char     air_status[16],soil_status[16];
} HerbDataModel_t;

void                   Model_Init(void);
const HerbDataModel_t* Model_GetSnapshot(void);
void Model_SetNode(const char *node_id);
/* Main-loop only. Invalid readings remain '--'; no stale success values. */
void Model_SetAir(bool valid,float temperature,float humidity,uint32_t lux,uint32_t now);
void Model_SetSoil(bool valid,float temperature,float moisture,uint16_t ec,float ph,uint16_t n,uint16_t p,uint16_t k,uint32_t now);
uint32_t Model_Revision(void);
void Model_SetSensorCollecting(bool collecting);
bool Model_IsDisplayTest(void);
bool Model_SetDisplayTest(const float values[10],const char *clock);
void Model_ClearDisplayTest(void);

#endif /* __APP_MODEL_H */
