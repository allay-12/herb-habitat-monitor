#include "display_test.h"
#include "app_model.h"
#include "app_ui.h"
#include "history.h"
#include "history_fs.h"
#include "disk_image.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static unsigned failures,page;
#define CHECK(c,s) do{if(!(c)){printf("FAIL: %s\n",s);failures++;}}while(0)
uint32_t HAL_GetTick(void){return 0;}
void UI_ShowDataPage(UIPage_t p){page=p;}
static uint32_t crc(const char *p,unsigned n)
{
    uint32_t c=~0u;while(n--){c^=(unsigned char)*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u & (0u-(c&1)));}return ~c;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    TestDisk_SetImage(argv[1]);TestDisk_AllowWrites(1);History_Init();Model_Init();
    const char *row="2026-11-02 10:20:30,25.6,65.8,12000,23.2,45.1,500,6.5,30,20,50\r\n";
    const char *json="{\"device_id\":\"node01\",\"sample_id\":\"DISPLAY-TEST\",\"_test\":true,\"_display_test\":true}";
    HistoryDate date={2026,11,2,10,20,30};uint32_t stamp;HistoryDate_ToSeconds(&date,&stamp);
    g_display_test.stamp=stamp;g_display_test.date=20261102;strcpy((char*)g_display_test.sample_id,"DISPLAY-TEST");
    strcpy((char*)g_display_test.json,json);strcpy((char*)g_display_test.csv,row);
    g_display_test.json_size=strlen(json);g_display_test.csv_size=strlen(row);
    g_display_test.json_crc=crc(json,strlen(json));g_display_test.csv_crc=crc(row,strlen(row));
    g_display_test.command=1;DisplayTest_Poll();
    CHECK(g_display_test.state==2 && g_display_test.stage==4 && g_display_test.valid_mask==0x3ff,"write/read actual FatFs and apply complete test model");
    CHECK(Model_IsDisplayTest() && page==UI_PAGE_OVERVIEW,"explicit preview marked on overview");
    const HerbDataModel_t *m=Model_GetSnapshot();
    CHECK(fabs(m->air_temperature_c-25.6)<.001 && fabs(m->air_humidity_pct-65.8)<.001 && m->illuminance_lux==12000,"air model matches card row");
    CHECK(m->soil_ec_us_cm==500 && fabs(m->soil_ph-6.5)<.001 && m->soil_n_mg_kg==30 && m->soil_k_mg_kg==50,"soil model matches card row");
    CHECK(!m->battery_valid && m->battery_pct==-1,"unknown battery remains unknown");
    HistoryFS_SelectMetric(3);CHECK(HistoryFS_OpenDate(stamp)==HFS_OK,"test soil path selected");HistoryFS_Close();
    g_display_test.command=3;DisplayTest_Poll();CHECK(page==UI_PAGE_SOIL,"switch real soil view");
    g_display_test.command=5;DisplayTest_Poll();CHECK(!Model_IsDisplayTest() && !Model_GetSnapshot()->air_temp_valid,"exit restores original invalid model");
    HistoryFS_SelectMetric(3);CHECK(HistoryFS_OpenDate(stamp)==HFS_MISSING,"test record never became a normal data row");
    unsigned before=test_disk_writes;
    g_display_test.command=6;DisplayTest_Poll();
    CHECK(g_display_test.state==2 && Model_IsDisplayTest() && test_disk_writes==before,"restore preview from existing card files with zero writes");
    g_display_test.command=5;DisplayTest_Poll();
    before=test_disk_writes;g_display_test.command=7;DisplayTest_Poll();
    CHECK(page==UI_PAGE_HISTORY&&!Model_IsDisplayTest()&&test_disk_writes==before,"SWD live history navigation never injects test values or writes files");
    g_display_test.csv_crc^=1;g_display_test.command=1;DisplayTest_Poll();
    CHECK(g_display_test.state==3 && !Model_IsDisplayTest(),"corrupt controller payload rejected before display");
    printf("SD/server display fixture regression failures: %u\n",failures);return failures?1:0;
}
