# Air780E 串口诊断

STM32F103C8T6 独立诊断工程。用 Keil µVision 打开 [Air780_F103_Diag.uvprojx](MDK-ARM/Air780_F103_Diag.uvprojx)，选择 Arm Compiler 5 并编译。工程包含需要的 STM32 HAL/CMSIS，来源见 [vendor_sources.json](vendor_sources.json)。

USART1 连接电脑的 USB 转串口，PA9 为 MCU TX，PA10 为 MCU RX。USART2 连接 Air780E 转接板，PA2 为 MCU TX，PA3 为 MCU RX。两路均为 115200、8N1。接线时按信号方向对接，F103 和转接板共地，PA1 保持高阻。模块按转接板规格独立供电。

在电脑串口助手用文本模式发送 `HELP` 查看本地命令，发送 `AT` 查看模块应答，发送 `DIAG` 查询 SIM、信号、注册及分组附着状态。
