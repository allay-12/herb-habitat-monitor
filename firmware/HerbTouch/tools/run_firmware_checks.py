"""Compile and run host regressions against actual application C sources."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import zlib

root=Path(__file__).resolve().parents[1]
out=root/'evidence/host-tests'
out.mkdir(parents=True,exist_ok=True)
gcc=shutil.which('gcc')
if not gcc:raise SystemExit('A host GCC compiler is required.')
inc=[root/'tools/tests/host',root/'App/UI',root/'App/Model',root/'BSP/LCD',root/'BSP/Touch']
inc += [root/'App/History',root/'BSP/Storage',root/'ThirdParty/FatFs']
inc += [root/'App/Diagnostics']
inc += [root/'App/Node',root/'PublicConfig']
history_sources=['App/History/history.c','App/History/history_time.c','BSP/Storage/history_fs_fatfs.c','ThirdParty/FatFs/ff.c','tools/tests/host/disk_image.c']
subprocess.run([sys.executable,str(root/'tools/tests/build_history_fixtures.py')],check=True)
subprocess.run([sys.executable,str(root/'tools/make_history_demo.py')],check=True)
fixtures=root/'evidence/history-fixtures'
shutil.copy2(fixtures/'history-fat32.img',fixtures/'history-writable.img')
shutil.copy2(fixtures/'history-fat32.img',fixtures/'node-writable.img')
shutil.copy2(fixtures/'history-fat32.img',fixtures/'display-writable.img')
shutil.copy2(fixtures/'history-fat32.img',fixtures/'node-app-writable.img')
shutil.copy2(fixtures/'history-fat32.img',fixtures/'node-auto-writable.img')
shutil.copy2(fixtures/'history-fat32.img',fixtures/'sensor-cycle-writable.img')
shutil.copy2(fixtures/'history-fat32.img',fixtures/'live-history-writable.img')
shutil.copy2(fixtures/'history-fat32.img',fixtures/'settings-writable.img')
shutil.copy2(fixtures/'history-fat32.img',fixtures/'ui-config-writable.img')
nonce=0x12345678
crc=zlib.crc32(bytes(((n*37)^(nonce>>((n&3)*8))^0xa5)&255 for n in range(4096)))
failure=False
for name,sources in [
    ('ui',['tools/tests/host/test_ui.c','App/Model/app_model.c','BSP/LCD/bsp_lcd_font.c','App/UI/ui_history.c',*history_sources]),
    ('ui_controls',['tools/tests/host/test_ui_controls.c','tools/tests/host/lcd_capture.c','App/Model/app_model.c','BSP/LCD/bsp_lcd_font.c','App/UI/ui_history.c',*history_sources]),
    ('touch',['tools/tests/host/test_touch.c']),
    ('history',['tools/tests/host/test_history.c',*history_sources]),
    ('history_adaptive',['tools/tests/host/test_history_adaptive.c','App/History/history.c','App/History/history_time.c']),
    ('history_ui',['tools/tests/host/test_history_ui.c','tools/tests/host/lcd_capture.c','BSP/LCD/bsp_lcd_font.c',*history_sources]),
    ('live_history',['tools/tests/host/test_live_history.c','tools/tests/host/lcd_capture.c','BSP/LCD/bsp_lcd_font.c','App/Node/node_store.c','App/Node/air_sensor.c','App/Node/soil_sensor.c',*history_sources]),
    ('sd_diag',['tools/tests/host/test_sd_diag.c','App/Diagnostics/sd_diag.c',*history_sources]),
    ('node',['tools/tests/host/test_node.c','App/Node/air_sensor.c','App/Node/soil_sensor.c','App/Node/at_stream.c','App/Node/air780.c','App/Node/node_store.c',*history_sources]),
    ('sensors',['tools/tests/host/test_sensors.c','App/Node/air_sensor.c','App/Node/soil_sensor.c','App/Node/sensor_bus.c','App/Node/node_clock.c','App/History/history_time.c']),
    ('node_app',['tools/tests/host/test_node_app.c','App/Model/app_model.c','App/Node/air_sensor.c','App/Node/soil_sensor.c','App/Node/sensor_bus.c','App/Node/node_clock.c','App/Node/air780.c','App/Node/at_stream.c','App/Node/node_store.c',*history_sources]),
    ('node_autostart',['tools/tests/host/test_node_autostart.c','App/Model/app_model.c','App/Node/air_sensor.c','App/Node/soil_sensor.c','App/Node/sensor_bus.c','App/Node/node_clock.c','App/Node/air780.c','App/Node/at_stream.c','App/Node/node_store.c',*history_sources]),
    ('sensor_cycle',['tools/tests/host/test_sensor_cycle.c','App/Model/app_model.c','App/Node/air_sensor.c','App/Node/soil_sensor.c','App/Node/sensor_bus.c','App/Node/node_clock.c','App/Node/air780.c','App/Node/at_stream.c','App/Node/node_store.c',*history_sources]),
    ('node_settings',['tools/tests/host/test_node_settings.c','App/Node/node_settings.c',*history_sources]),
    ('display',['tools/tests/host/test_display.c','App/Diagnostics/display_test.c','App/Model/app_model.c',*history_sources]),
    ('history_flash',['tools/tests/host/test_history_flash.c','tools/tests/host/lcd_capture.c','BSP/LCD/bsp_lcd_font.c',*history_sources])]:
    exe=out/f'test_{name}.exe'
    selected_inc=([root/'tools/tests/host/node_hal'] if name in ['node_app','node_autostart','sensor_cycle'] else [])+inc
    if name in ['ui','ui_controls','node_app','node_autostart','sensor_cycle']:sources += ['App/Node/node_settings.c']
    sources += ['App/Node/node_identity.c']
    profile=['-DNODE_AUTO_SENSOR=0','-DNODE_AUTO_MODEM=0'] if name=='node_app' else []
    if name=='sensor_cycle':profile=['-DNODE_AUTO_SENSOR=0','-DNODE_AUTO_MODEM=0']
    else:profile+=['-DNODE_SENSOR_DUTY_CYCLE=0']
    cmd=[gcc,'-std=c11','-Wall','-Wextra',*profile,*[f'-I{p}' for p in selected_inc],*[str(root/p) for p in sources],'-o',str(exe),'-lm']
    subprocess.run(cmd,check=True)
    args=[str(fixtures/f'history-{kind}.img') for kind in ['valid','invalid','empty','fat32','groups']] if name=='history' else []
    if name=='history_ui':args=[str(fixtures/'history-demo.img'),str(out)]
    if name=='live_history':args=[str(fixtures/'live-history-writable.img'),str(out)]
    if name=='ui_controls':args=[str(fixtures/'history-demo.img'),str(out),str(fixtures/'ui-config-writable.img')]
    if name=='sd_diag':args=[str(fixtures/'history-writable.img'),f'{crc:08x}']
    if name=='node':args=[str(fixtures/'node-writable.img')]
    if name=='node_app':args=[str(fixtures/'node-app-writable.img')]
    if name=='node_autostart':args=[str(fixtures/'node-auto-writable.img')]
    if name=='sensor_cycle':args=[str(fixtures/'sensor-cycle-writable.img')]
    if name=='node_settings':args=[str(fixtures/'settings-writable.img')]
    if name=='display':args=[str(fixtures/'display-writable.img')]
    if name=='history_flash':args=[str(fixtures/'history-groups.img'),str(fixtures/'history-demo.img')]
    result=subprocess.run([str(exe),*args],capture_output=True,text=True)
    print(result.stdout,end='');print(result.stderr,end='')
    (out/f'{name}-result.txt').write_text(result.stdout+result.stderr,encoding='utf-8')
    failure |= result.returncode!=0
raise SystemExit(1 if failure else 0)
