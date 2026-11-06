# 基于 RA4M2 的四键调档风扇（新生杯嵌入式赛作品）

广东技术师范大学 · 集成电路与信息工程学院
队员：洪欣怡、叶新怡、陈凤敏

## 项目简介

针对市面手持风扇"单键循环切档、无法急停"的安全隐患，本作品基于瑞萨
RA4M2（R7FA4M2AD3CFL，Cortex-M33）实现一套四键风扇控制系统：
PWM 无级档位调速 + 一键急停 + OLED 实时状态显示。

## 功能

| 按键 | 引脚 | 功能 |
|------|------|------|
| K1 | P100 | 降档（100% → 65% → 35% → 0%） |
| K2 | P101 | 升档（0% → 35% → 65% → 100%） |
| K3 | P102 | 开机 / 关机 |
| K4 | P103 | 一键急停（任意时刻可用） |

- PWM 调速：GPT 定时器 20kHz（高于人耳听觉上限，无啸叫）
- OLED 显示：SSD1306，硬件 I2C（0x3C / 400kHz），实时显示开关状态、档位、占空比
- 驱动电路：P111 → S8050 → MOS 模块（光耦隔离），12V 风扇

## 硬件连接

```
12V 适配器 ── MOS 模块 VIN± ── OUT± ── 风扇
RA4M2 P111(GTIOC3A) ── 1kΩ ── S8050 ── MOS TRIG
RA4M2 P407/P408 (IIC0) ── OLED SDA/SCL
★ MOS 模块 VIN− 必须与主板 GND 共地
```

## 构建 & 烧录

1. e² studio + FSP 打开工程，确认 BSP 芯片型号为 **R7FA4M2AD3CFM**（64 脚，实测所用封装）
2. Generate Project Content → Build 生成 .srec
3. Renesas Flash Programmer 通过 UART Bootloader 烧录

## 踩坑记录

- OLED 黑屏：曾误判为 BSP 型号问题。经 FSP 6.6.0 引脚数据核实，3CFM（64 脚）封装上 P407/P408 本身就有 IIC0 SDA/SCL 复用，BSP 保持 3CFM 即可；黑屏实际原因为 P407/P408 未正确配置成 IIC 外设模式/未重新 Generate
- OLED 乱码：自建字库缺字母 → 补全字库
- Stacks 红叉：GPT 的 Pin Output Support 未开启 → Enabled with Extra Features
- 风扇不转：MOS 模块未与主控共地 + J1 触发电平跳线 → 共地 + 调跳线
