/*
 * RA4M2_MINI 可调档位风扇 - hal_entry.c
 * 功能：4按键调档 + PWM 20kHz 调速 + OLED 显示
 *
 * 硬件对应：
 *   P111 (GPT3 GTIOC3A)  -> MOS模块 TRIG/PWM
 *   P100 -> 按键K1 (档位-1)
 *   P101 -> 按键K2 (档位+1)
 *   P102 -> 按键K3 (开/关)
 *   P103 -> 按键K4 (一键停止)
 *   P407 -> OLED SDA (I2C0 SDA，板上已连)
 *   P408 -> OLED SCL (I2C0 SCL，板上已连)
 *
 * FSP 要求（必须在 FSP Configurator 里已配置）：
 *   1. g_timer0: GPT PWM, Channel = 3, 20kHz, GTIOCA = P111
 *   2. P100/P101/P102/P103: Input mode + Input pull-up
 *   3. 已点 Generate Project Content
 */

#include "hal_data.h"
#include "oled.h"
#include <string.h>

/* ---------- 档位定义 ---------- */
/* 占空比: 0% / 35% / 65% / 100% */
static const uint8_t gear_duty[4] = {0, 35, 65, 100};
static const char *gear_name[4] = {"STOP", "LOW ", "MID ", "HIGH"};

static int  gear = 0;      /* 当前档位 0~3 */
static int  power_on = 0;  /* 开关状态 */

/* ---------- PWM 输出 ---------- */
static void fan_apply(void)
{
    timer_info_t info;
    uint32_t duty;

    if (!power_on)
    {
        R_GPT_DutyCycleSet(&g_timer0_ctrl, 0, GPT_IO_PIN_GTIOCA);
        return;
    }

    R_GPT_InfoGet(&g_timer0_ctrl, &info);
    duty = (uint32_t)((uint64_t)info.period_counts * gear_duty[gear] / 100);
    R_GPT_DutyCycleSet(&g_timer0_ctrl, duty, GPT_IO_PIN_GTIOCA);
}

/* ---------- 按键扫描（低电平=按下，带消抖+等待释放） ---------- */
static bsp_io_port_pin_t key_pins[4] = {
    BSP_IO_PORT_01_PIN_00,  /* P100 K1 */
    BSP_IO_PORT_01_PIN_01,  /* P101 K2 */
    BSP_IO_PORT_01_PIN_02,  /* P102 K3 */
    BSP_IO_PORT_01_PIN_03,  /* P103 K4 */
};

/* 返回 1~4 表示按键号, 0=无按键 */
/* 按键引脚初始化：输入 + 内部上拉。
 * 注意：FSP 里只配了 P100，P101/P102/P103 是悬空的，会导致"没人按键却自己跳档"，
 * 所以这里在运行时把 4 个脚统一配成上拉输入。 */
static void keys_init(void)
{
    for (int i = 0; i < 4; i++)
    {
        R_IOPORT_PinCfg(&g_ioport_ctrl, key_pins[i],
                        (uint32_t) (IOPORT_CFG_PORT_DIRECTION_INPUT | IOPORT_CFG_PULLUP_ENABLE));
    }
}

static int key_scan(void)
{
    for (int i = 0; i < 4; i++)
    {
        bsp_io_level_t lv;
        R_IOPORT_PinRead(&g_ioport_ctrl, key_pins[i], &lv);
        if (lv == BSP_IO_LEVEL_LOW)
        {
            R_BSP_SoftwareDelay(20, BSP_DELAY_UNITS_MILLISECONDS); /* 消抖 */
            R_IOPORT_PinRead(&g_ioport_ctrl, key_pins[i], &lv);
            if (lv == BSP_IO_LEVEL_LOW)
            {
                while (1) /* 等待松手 */
                {
                    R_IOPORT_PinRead(&g_ioport_ctrl, key_pins[i], &lv);
                    if (lv == BSP_IO_LEVEL_HIGH) break;
                    R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
                }
                return i + 1;
            }
        }
    }
    return 0;
}

/* ---------- OLED 界面 ---------- */
/* 128x32 屏共 4 页（0~3），4 行内容正好占满，从第 0 页开始 */
#define UI_PAGE_BASE   0

static void ui_show(void)
{
    char buf[24];

    oled_clear();

    strcpy(buf, "FAN CONTROLLER");
    oled_draw_string(UI_PAGE_BASE + 0, 0, buf);

    if (power_on)
    {
        strcpy(buf, "STATE: ON");
        oled_draw_string(UI_PAGE_BASE + 1, 0, buf);

        strcpy(buf, "GEAR: ");
        buf[6] = (char)('0' + gear);
        buf[7] = ' ';
        buf[8] = 0;
        strcat(buf, gear_name[gear]);
        oled_draw_string(UI_PAGE_BASE + 2, 0, buf);

        strcpy(buf, "PWM: ");
        if (gear_duty[gear] == 100)
        {
            strcat(buf, "100%");
        }
        else
        {
            uint8_t d = gear_duty[gear];
            uint8_t len = strlen(buf);
            buf[len]     = (char)('0' + d / 10);
            buf[len + 1] = (char)('0' + d % 10);
            buf[len + 2] = '%';
            buf[len + 3] = 0;
        }
        oled_draw_string(UI_PAGE_BASE + 3, 0, buf);
    }
    else
    {
        strcpy(buf, "STATE: OFF");
        oled_draw_string(UI_PAGE_BASE + 1, 0, buf);
        strcpy(buf, "PRESS K3 TO RUN");
        oled_draw_string(UI_PAGE_BASE + 2, 0, buf);
    }
}

/* ---------- 主入口 ---------- */
void hal_entry(void)
{
    /* 0. 按键引脚初始化为上拉输入（必须最先执行） */
    keys_init();

    /* 1. 启动 PWM (GPT3, 20kHz) */
    R_GPT_Open(&g_timer0_ctrl, &g_timer0_cfg);
    R_GPT_Start(&g_timer0_ctrl);
    fan_apply();

    /* 2. 初始化 OLED (硬件 I2C: P407=SDA, P408=SCL, 地址 0x3C) */
    oled_init();
    ui_show();

    /* 3. 主循环 */
    while (1)
    {
        int key = key_scan();

        if (key == 1)            /* K1: 档位-1 */
        {
            if (gear > 0) gear--;
            power_on = 1;
            fan_apply();
            ui_show();
        }
        else if (key == 2)       /* K2: 档位+1 */
        {
            if (gear < 3) gear++;
            power_on = 1;
            fan_apply();
            ui_show();
        }
        else if (key == 3)       /* K3: 开/关 */
        {
            power_on = !power_on;
            fan_apply();
            ui_show();
        }
        else if (key == 4)       /* K4: 一键急停 */
        {
            power_on = 0;
            gear = 0;
            fan_apply();
            ui_show();
        }

        R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
    }
}
