本项目使用了F407VET6作为主控，使用vemsee的土壤监测和百叶箱模块来实时监测药物生长环境

通过合宙 Air780E 4G 模块与服务器进行通讯，上传传感器的数据（10分钟休息后五分钟采集环境，每15分钟上传一次平均数据），服务器发送数据给手机应用端来进行查看

本地SD卡存储数据与4G模块相同，当网络不通畅时，会暂存数据在SD卡中，然后网络通畅时发送数据给服务器

## 固件工程

- [HerbTouch](firmware/HerbTouch/README.md) 为 STM32F407VET6 主固件。包含 CubeMX 和 Keil 工程、传感器采集、触摸屏、SD 卡及 Air780E 上传逻辑。
- [Air780_F103_Diag](firmware/Air780_F103_Diag/README.md) 为 STM32F103C8T6 串口诊断固件。
- 服务器地址、证书及设备凭据的脱敏位置和本地配置方法见 [已脱敏的配置](firmware/HerbTouch/README.md#已脱敏的配置)。
