# HerbTouch

STM32F407VET6 主固件，包含 ST7796S 触摸界面、板载 SD 卡历史记录、RS485 传感器采集和 Air780E MQTT/TLS 上传。PB0 每 15 分钟开启传感器电源 5 分钟，每轮将有效样本的平均值保存到 SD 卡，联网后可补传待上传记录。

- CubeMX 配置见 [HerbTouch.ioc](HerbTouch.ioc)，生成环境为 CubeMX 6.16.1、STM32Cube FW_F4 V1.28.3。
- 用 Keil µVision 打开 [HerbTouch.uvprojx](MDK-ARM/HerbTouch.uvprojx)。工程使用 Arm Compiler 5 和 STM32F4 Device Family Pack。
- 引脚和接线备注见 [JSON](docs/pin_manifest.json) 或 [CSV](docs/pin_manifest.csv)。百叶箱和土壤探头共享 RS485，地址分别为 2 和 1，波特率 4800。
- 安装 Python 3 和主机 GCC 后，在仓库根目录运行 `python firmware/HerbTouch/tools/run_firmware_checks.py`。回归测试使用模拟串口和合成 SD 数据。

## 已脱敏的配置

公开版可以直接编译。配置中的 `*****` 标记表示发布时已替换的真实值。使用公开默认值时，设备身份校验会阻止发起 MQTT 连接。

| 文件 | 已脱敏的内容 |
| --- | --- |
| [server_config.h](PublicConfig/server_config.h) | MQTT 服务器地址、部署端口和 TLS CA 证书。端口是整数，公开版使用 `0`，并在行末标注 `*****`。 |
| [device_secrets.h](PublicConfig/device_secrets.h) | 已登记的 MCU UID 和设备 MQTT 密码。用户名和客户端 ID 由 UID 在运行时生成。 |

需要联网时，在本工程新建 `PrivateConfig/`，复制 `PublicConfig/` 中的两个同名头文件到该目录，再填入实际连接配置。CA 写成 PEM 内容（C 字符串，每行以 `\n` 结束），`NODE_CA_SIZE` 为 PEM 字节数，不含 C 字符串的终止字节。Keil 优先加载本地配置，Git 会忽略 `PrivateConfig/`。
