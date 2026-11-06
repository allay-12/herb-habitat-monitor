"""Create a valid FAT16 card image and explicit synthetic CSV fixtures."""
from pathlib import Path
import struct

HEADER='timestamp,air_temperature_c,air_humidity_pct,illuminance_lux,soil_temperature_c,soil_moisture_pct,soil_ec_us_cm,soil_ph,soil_n_mg_kg,soil_p_mg_kg,soil_k_mg_kg'
def fat_image(path,files,fat32=False):
    sectors=73728 if fat32 else 32768
    reserved,fat_sectors=(32,576) if fat32 else (1,128)
    root_sector=reserved+2*fat_sectors;data_sector=root_sector+(0 if fat32 else 32)
    eof=0x0fffffff if fat32 else 0xffff
    image=bytearray(sectors*512);fat=[0]*(fat_sectors*512//(4 if fat32 else 2))
    fat[0]=0x0ffffff8 if fat32 else 0xfff8;fat[1]=eof;next_cluster=2
    def alloc(data):
        nonlocal next_cluster
        clusters=list(range(next_cluster,next_cluster+max(1,(len(data)+511)//512)));next_cluster+=len(clusters)
        for i,c in enumerate(clusters):
            fat[c]=clusters[i+1] if i+1<len(clusters) else eof
            pos=(data_sector+c-2)*512;image[pos:pos+len(data[i*512:(i+1)*512])]=data[i*512:(i+1)*512]
        return clusters[0]
    def entry(name,attr,cluster,size):
        stem,_,ext=name.partition('.')
        b=bytearray(32);b[:11]=(stem.upper().ljust(8)+ext.upper().ljust(3)).encode('ascii');b[11]=attr
        if name in ('.','..'):b[:11]=name.ljust(11).encode('ascii')
        struct.pack_into('<H',b,26,cluster);struct.pack_into('<I',b,28,size);return b
    root_cluster=alloc(bytes(512)) if fat32 else 0
    def make_directory(contents,parent):
        cluster=alloc(bytes(max(1,(len(contents)+2+15)//16)*512))
        directory=bytearray(entry('.',0x10,cluster,0)+entry('..',0x10,parent,0))
        for name,data in contents.items():
            if isinstance(data,dict):
                child=make_directory(data,cluster);directory+=entry(name,0x10,child,0)
            else:
                if isinstance(data,str):data=data.encode('utf-8')
                child=alloc(data);directory+=entry(name,0x20,child,len(data))
        c=cluster;off=0
        while c!=eof:
            pos=(data_sector+c-2)*512;chunk=directory[off:off+512];image[pos:pos+len(chunk)]=chunk;off+=512;c=fat[c]
        return cluster
    directory_cluster=make_directory(files,0)
    root_pos=(data_sector+root_cluster-2)*512 if fat32 else root_sector*512
    root=entry('DATA',0x10,directory_cluster,0);image[root_pos:root_pos+32]=root
    boot=bytearray(512);boot[:3]=b'\xeb\x3c\x90';boot[3:11]=b'HERBTEST'
    struct.pack_into('<HBHBHHBHHHII',boot,11,512,1,reserved,2,0 if fat32 else 512,0 if fat32 else sectors,0xf8,0 if fat32 else fat_sectors,32,64,0,sectors if fat32 else 0)
    if fat32:
        struct.pack_into('<IHHIHH',boot,36,fat_sectors,0,0,root_cluster,1,6)
        boot[64]=0x80;boot[66]=0x29;struct.pack_into('<I',boot,67,12345)
        boot[71:82]=b'HERB TEST  ';boot[82:90]=b'FAT32   '
        info=bytearray(512);struct.pack_into('<I',info,0,0x41615252);struct.pack_into('<III',info,484,0x61417272,0xffffffff,0xffffffff);struct.pack_into('<I',info,508,0xaa550000)
        image[512:1024]=info;image[7*512:8*512]=info
    else:
        boot[36]=0x80;boot[38]=0x29;struct.pack_into('<I',boot,39,12345)
        boot[43:54]=b'HERB TEST  ';boot[54:62]=b'FAT16   '
    boot[510:]=b'\x55\xaa';image[:512]=boot
    if fat32:image[6*512:7*512]=boot
    packed=struct.pack('<'+('I' if fat32 else 'H')*len(fat),*fat)
    image[reserved*512:reserved*512+len(packed)]=packed;image[(reserved+fat_sectors)*512:(reserved+fat_sectors)*512+len(packed)]=packed
    path.write_bytes(image)

def build(out):
    out.mkdir(parents=True,exist_ok=True)
    def row(t,air='20',soil='11'):return f'{t},{air},50,1000,{soil},40,100,6.5,2,3,4\r\n'
    rows=HEADER+'\r\n'+row('2026-10-31 02:00:00','10')+row('2026-10-31 00:00:00','20')+row('2026-10-31 00:30:00','24')+row('2026-10-31 01:00:00','',soil='12')+row('2026-10-31 03:00:00','30')
    rows+=row('2026-10-32 02:00:00','50')+'not,a,valid,row\n'+row('2026-10-31 02:01:00','nan')
    rows+=row('2026-10-30 02:00:00','99')+'X'*1500+'\n'+row('2026-10-31 02:30:00','14')
    rows+=row('2026-10-31 02:59:00','999').rstrip('\r\n')
    files={'README.TXT':'Synthetic test card. Never deploy as real readings.\n','20261031.CSV':rows,
           '20261030.CSV':'\ufeff'+HEADER+'\r\n'+row('2026-10-30 23:30:00','18'),
           '20260930.CSV':HEADER+'\n'+row('2026-09-30 12:00:00','1'),
           '20240229.CSV':HEADER+'\n'+row('2024-02-29 12:00:00','2')}
    fat_image(out/'history-valid.img',files)
    fat_image(out/'history-fat32.img',files,True)
    fat_image(out/'history-invalid.img',{'20261031.CSV':'wrong,header\n1,2,3\n'})
    fat_image(out/'history-empty.img',{'README.TXT':'no samples'})
    grouped={
        'air':{'20261031.CSV':HEADER+'\n'+row('2026-10-31 00:00:00','25')},
        'light':{'20261031.CSV':HEADER+'\n'+row('2026-10-31 00:00:00','20').replace(',1000,',',12345,')},
        'soil':{'20261031.CSV':HEADER+'\n'+row('2026-10-31 00:00:00','20',soil='19')},
        '20261031.CSV':HEADER+'\n'+row('2026-10-31 00:00:00','99')}
    fat_image(out/'history-groups.img',grouped,True)
    print('Created real FAT16/FAT32 fixtures (valid, bad header, empty).')
if __name__=='__main__':build(Path(__file__).resolve().parents[2]/'evidence/history-fixtures')
