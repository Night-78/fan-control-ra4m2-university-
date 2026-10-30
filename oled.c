/*
 * oled.c - SSD1306 OLED 驱动（硬件 I2C 版）最终版
 * 依赖 FSP: g_i2c_master0 (r_iic_master)
 *   Channel=0, Slave Address=0x3C (7-bit), 400kHz
 *   Callback 必须在 FSP 里填: i2c_master0_callback
 * 引脚: P408=I2C0_SCL, P407=I2C0_SDA（在 FSP Pins 里配置成外设模式）
 *
 * 本版参数（实测通过）：
 *   分辨率 128x64 → multiplex 0x3F, COM pins 0x12, 清屏 page 0~7
 *   显示方向     → segment remap 0xA1 + COM scan 0xC0（正立）
 *   电荷泵       → 0x8D 0x14（标准 SSD1306 使能）
 */
#include "oled.h"
#include "hal_data.h"

/* 字符集必须与下面 font 表的顺序一一对应 */
static const char oled_font_chars[] = " ABCDEFHILMNOPRSTUW0123456789%:";

static const uint8_t font[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x00,0x7F,0x02,0x04,0x3E}, /* 'L' */
    {0x7F,0x02,0x04,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x7F,0x20,0x18,0x20,0x7F}, /* 'W' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x30,0x48,0x30,0x08,0x06}, /* '%' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
};

#define FONT_CHAR_NUM  (sizeof(font) / sizeof(font[0]))

/* 屏幕分辨率：128x64 用 64；128x32 用 32 */
#define OLED_HEIGHT   64
#define OLED_PAGES    (OLED_HEIGHT / 8)

static volatile bool g_i2c_done = false;

void i2c_master0_callback(i2c_master_callback_args_t *p_args)
{
    if (NULL != p_args)
    {
        g_i2c_done = (I2C_MASTER_EVENT_TX_COMPLETE == p_args->event);
    }
}

static void oled_send(uint8_t ctrl, const uint8_t *data, uint32_t len)
{
    uint8_t buf[17];
    uint32_t retry, wait;

    if (len > 16) len = 16;

    buf[0] = ctrl;
    for (uint32_t i = 0; i < len; i++)
    {
        buf[1 + i] = data[i];
    }

    for (retry = 0; retry < 3; retry++)
    {
        g_i2c_done = false;
        fsp_err_t err = R_IIC_MASTER_Write(&g_i2c_master0_ctrl, buf, len + 1, false);
        if (FSP_SUCCESS != err)
        {
            R_BSP_SoftwareDelay(1000, BSP_DELAY_UNITS_MICROSECONDS);
            continue;
        }

        for (wait = 0; wait < 5000; wait++)
        {
            if (g_i2c_done) return;
            R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MICROSECONDS);
        }
        return;   /* 超时直接返回，绝不卡死 */
    }
}

static void oled_write_cmd(uint8_t cmd)
{
    oled_send(0x00, &cmd, 1);
}

static void oled_write_data(uint8_t dat)
{
    oled_send(0x40, &dat, 1);
}

static void oled_set_pos(uint8_t page, uint8_t col)
{
    oled_write_cmd(0xB0 | (page & 0x07));
    oled_write_cmd(0x00 | (col & 0x0F));
    oled_write_cmd(0x10 | ((col >> 4) & 0x0F));
}

void oled_init(void)
{
    fsp_err_t err = R_IIC_MASTER_Open(&g_i2c_master0_ctrl, &g_i2c_master0_cfg);
    if ((FSP_SUCCESS != err) && (FSP_ERR_ALREADY_OPEN != err))
    {
        return;
    }

    R_IIC_MASTER_SlaveAddressSet(&g_i2c_master0_ctrl, 0x3C, I2C_MASTER_ADDR_MODE_7BIT);
    R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MILLISECONDS);

    oled_write_cmd(0xAE);                        /* display off */
    oled_write_cmd(0xD5); oled_write_cmd(0x80);  /* clock */
#if OLED_HEIGHT == 32
    oled_write_cmd(0xA8); oled_write_cmd(0x1F);  /* multiplex 1/32 */
#else
    oled_write_cmd(0xA8); oled_write_cmd(0x3F);  /* multiplex 1/64 */
#endif
    oled_write_cmd(0xD3); oled_write_cmd(0x00);  /* display offset */
    oled_write_cmd(0x40);                        /* start line 0 */
    oled_write_cmd(0x8D); oled_write_cmd(0x14);  /* charge pump on（标准）*/
    oled_write_cmd(0x20); oled_write_cmd(0x02);  /* page addressing */
    oled_write_cmd(0xA1);                        /* segment remap：配 C0 得正立 */
    oled_write_cmd(0xC0);                        /* COM scan：配 A1 得正立 */
#if OLED_HEIGHT == 32
    oled_write_cmd(0xDA); oled_write_cmd(0x02);  /* COM pins 32 行 */
#else
    oled_write_cmd(0xDA); oled_write_cmd(0x12);  /* COM pins 64 行 */
#endif
    oled_write_cmd(0x81); oled_write_cmd(0xCF);  /* contrast */
    oled_write_cmd(0xD9); oled_write_cmd(0xF1);  /* precharge */
    oled_write_cmd(0xDB); oled_write_cmd(0x40);  /* VCOMH */

    oled_clear();
    oled_write_cmd(0xA4);                        /* display from RAM */
    oled_write_cmd(0xA6);                        /* normal */
    oled_write_cmd(0xAF);                        /* display on */
}

void oled_clear(void)
{
    uint8_t page, col;
    uint8_t zeros[16] = {0};

    for (page = 0; page < OLED_PAGES; page++)
    {
        oled_set_pos(page, 0);
        for (col = 0; col < 128; col += 16)
        {
            oled_send(0x40, zeros, 16);
        }
    }
}

void oled_draw_string(uint8_t page, uint8_t col, const char *str)
{
    uint8_t i, j, idx;

    oled_set_pos(page, col);
    for (i = 0; str[i] != '\0'; i++)
    {
        for (idx = 0; idx < FONT_CHAR_NUM; idx++)
        {
            if (oled_font_chars[idx] == str[i]) break;
        }
        if (idx >= FONT_CHAR_NUM) idx = 0;

        for (j = 0; j < 5; j++)
        {
            oled_write_data(font[idx][j]);
        }
        oled_write_data(0x00);
        col += 6;
        if (col > 122) break;
    }
}
