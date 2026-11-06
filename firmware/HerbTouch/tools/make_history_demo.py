"""Synthetic records for testing only; firmware never writes/injects these."""
from pathlib import Path
from datetime import datetime,timedelta
import math
import sys
sys.path.insert(0,str(Path(__file__).parent/'tests'))
from build_history_fixtures import HEADER,fat_image

root=Path(__file__).resolve().parents[1]
out=root/'design/sd-test-card'
data=out/'data';data.mkdir(parents=True,exist_ok=True)
files={}
for day in range(31):
    date=datetime(2026,10,1)+timedelta(days=day)
    rows=[HEADER]
    for quarter in range(96):
        t=date+timedelta(minutes=quarter*15)
        phase=(quarter-24)*2*math.pi/96
        temp=23+5*math.sin(phase)+0.04*day
        humid=65-12*math.sin(phase)
        lux=max(0,43000*math.sin((quarter-24)*math.pi/48)) if 24<=quarter<=72 else 0
        vals=[temp,humid,lux,21+2*math.sin(phase-0.7),48-0.1*day+2*math.cos(phase),730+day*2,6.5+0.1*math.sin(phase),30+day*0.1,15+day*0.05,60+day*0.2]
        rows.append(t.strftime('%Y-%m-%d %H:%M:%S')+','+','.join(f'{v:.2f}' for v in vals))
    text='\r\n'.join(rows)+'\r\n';name=date.strftime('%Y%m%d')+'.csv'
    (data/name).write_text(text,encoding='utf-8',newline='')
    files[name]=text
(out/'README_仅模拟数据.txt').write_text('此目录是合成的测试记录，不是真实采集数据。\n将 data 目录复制到FAT32测试卡根目录，即可在屏幕历史页验证折线图。\n真实使用时换成传感器采集记录；固件不会自动生成模拟数据。\n时间为北京时间 UTC+08:00，结束时间不包含在查询范围中。\n',encoding='utf-8')
fat_image(root/'evidence/history-fixtures/history-demo.img',files)
print('Created 31 daily synthetic CSV files and test-only FAT16 image.')
