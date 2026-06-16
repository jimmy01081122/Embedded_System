#include "epaper_1in02d.h"
#include <stdint.h>
#include <string.h>

extern SPI_HandleTypeDef hspi1;

/*
 * 你目前已修正完成的方向是 EPD_ROTATE_MODE 1。
 * 若顯示方向再次錯誤，只改這一行。
 */
#define EPD_ROTATE_MODE 1

static uint8_t epd_buffer[EPD_BUF_SIZE];

static void EPD_SendCommand(uint8_t command)
{
    HAL_GPIO_WritePin(EPD_DC_GPIO_Port, EPD_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &command, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_SET);
}

static void EPD_SendData(uint8_t data)
{
    HAL_GPIO_WritePin(EPD_DC_GPIO_Port, EPD_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_SET);
}

static void EPD_SendBuffer(const uint8_t *data, uint32_t len)
{
    HAL_GPIO_WritePin(EPD_DC_GPIO_Port, EPD_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, (uint8_t *)data, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_SET);
}

static void EPD_Reset(void)
{
    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(20);

    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);

    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(200);
}

/*
 * BUSY = Low 表示 busy
 * BUSY = High 表示 idle
 */
static void EPD_WaitUntilIdle(void)
{
    while (HAL_GPIO_ReadPin(EPD_BUSY_GPIO_Port, EPD_BUSY_Pin) == GPIO_PIN_RESET)
    {
        HAL_Delay(10);
    }
}

static void EPD_TurnOnDisplay(void)
{
    /*
     * AUTO 0x17 + 0xA5 = PON + DRF + POF
     */
    EPD_SendCommand(0x17);
    EPD_SendData(0xA5);
    EPD_WaitUntilIdle();
}

void EPD_Init(void)
{
    EPD_Reset();
    EPD_WaitUntilIdle();

    EPD_SendCommand(0x00);
    EPD_SendData(0x4F);

    EPD_SendCommand(0x01);
    EPD_SendData(0x03);
    EPD_SendData(0x00);
    EPD_SendData(0x26);
    EPD_SendData(0x26);

    EPD_SendCommand(0x06);
    EPD_SendData(0x3F);

    EPD_SendCommand(0x30);
    EPD_SendData(0x13);

    EPD_SendCommand(0x50);
    EPD_SendData(0xD7);

    EPD_SendCommand(0x60);
    EPD_SendData(0x22);

    /*
     * TRES:
     * HRES = 80  = 0x50
     * VRES = 128 = 0x80
     */
    EPD_SendCommand(0x61);
    EPD_SendData(0x50);
    EPD_SendData(0x80);

    EPD_WaitUntilIdle();
}

static void EPD_DisplayFrame(const uint8_t *image)
{
    uint16_t i;

    EPD_SendCommand(0x10);
    for (i = 0; i < EPD_BUF_SIZE; i++)
    {
        EPD_SendData(0xFF);
    }

    EPD_SendCommand(0x13);
    EPD_SendBuffer(image, EPD_BUF_SIZE);

    EPD_TurnOnDisplay();
}

void EPD_ClearWhite(void)
{
    memset(epd_buffer, 0xFF, sizeof(epd_buffer));
    EPD_DisplayFrame(epd_buffer);
}

static void EPD_DrawPixel(uint16_t x, uint16_t y, uint8_t color)
{
    uint16_t ram_x;
    uint16_t ram_y;
    uint32_t index;
    uint8_t mask;

    if ((x >= EPD_LOG_WIDTH) || (y >= EPD_LOG_HEIGHT))
    {
        return;
    }

#if (EPD_ROTATE_MODE == 0)
    ram_x = y;
    ram_y = x;
#elif (EPD_ROTATE_MODE == 1)
    ram_x = EPD_RAM_WIDTH - 1 - y;
    ram_y = x;
#elif (EPD_ROTATE_MODE == 2)
    ram_x = y;
    ram_y = EPD_RAM_HEIGHT - 1 - x;
#elif (EPD_ROTATE_MODE == 3)
    ram_x = EPD_RAM_WIDTH - 1 - y;
    ram_y = EPD_RAM_HEIGHT - 1 - x;
#else
    ram_x = y;
    ram_y = x;
#endif

    if ((ram_x >= EPD_RAM_WIDTH) || (ram_y >= EPD_RAM_HEIGHT))
    {
        return;
    }

    index = (ram_x + ram_y * EPD_RAM_WIDTH) / 8;
    mask = 0x80 >> (ram_x % 8);

    if (color)
    {
        epd_buffer[index] |= mask;
    }
    else
    {
        epd_buffer[index] &= (uint8_t)(~mask);
    }
}

static const uint8_t *GetGlyph5x7(char c)
{
    static const uint8_t space[5] = {0x00,0x00,0x00,0x00,0x00};

    static const uint8_t zero[5]  = {0x3E,0x51,0x49,0x45,0x3E};
    static const uint8_t one[5]   = {0x00,0x42,0x7F,0x40,0x00};
    static const uint8_t two[5]   = {0x42,0x61,0x51,0x49,0x46};
    static const uint8_t three[5] = {0x21,0x41,0x45,0x4B,0x31};
    static const uint8_t four[5]  = {0x18,0x14,0x12,0x7F,0x10};
    static const uint8_t five[5]  = {0x27,0x45,0x45,0x45,0x39};
    static const uint8_t six[5]   = {0x3C,0x4A,0x49,0x49,0x30};
    static const uint8_t seven[5] = {0x01,0x71,0x09,0x05,0x03};
    static const uint8_t eight[5] = {0x36,0x49,0x49,0x49,0x36};
    static const uint8_t nine[5]  = {0x06,0x49,0x49,0x29,0x1E};

    static const uint8_t C[5] = {0x3E,0x41,0x41,0x41,0x22};
    static const uint8_t M[5] = {0x7F,0x02,0x0C,0x02,0x7F};
    static const uint8_t T[5] = {0x01,0x01,0x7F,0x01,0x01};
    static const uint8_t U[5] = {0x3F,0x40,0x40,0x40,0x3F};

    static const uint8_t e[5] = {0x38,0x54,0x54,0x54,0x18};
    static const uint8_t m[5] = {0x7C,0x04,0x18,0x04,0x78};
    static const uint8_t p[5] = {0x7C,0x14,0x14,0x14,0x08};

    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t dot[5]   = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t minus[5] = {0x08,0x08,0x08,0x08,0x08};

    switch (c)
    {
        case '0': return zero;
        case '1': return one;
        case '2': return two;
        case '3': return three;
        case '4': return four;
        case '5': return five;
        case '6': return six;
        case '7': return seven;
        case '8': return eight;
        case '9': return nine;

        case 'C': return C;
        case 'M': return M;
        case 'T': return T;
        case 'U': return U;

        case 'e': return e;
        case 'm': return m;
        case 'p': return p;

        case ':': return colon;
        case '.': return dot;
        case '-': return minus;
        case ' ': return space;

        default: return space;
    }
}

static void EPD_DrawChar5x7(uint16_t x, uint16_t y, char c)
{
    const uint8_t *glyph;
    uint8_t col;
    uint8_t row;
    uint8_t line;

    glyph = GetGlyph5x7(c);

    for (col = 0; col < 5; col++)
    {
        line = glyph[col];

        for (row = 0; row < 7; row++)
        {
            if (line & (1 << row))
            {
                EPD_DrawPixel(x + col, y + row, 0);
            }
            else
            {
                EPD_DrawPixel(x + col, y + row, 1);
            }
        }
    }
}

static void EPD_DrawString5x7(uint16_t x, uint16_t y, const char *str)
{
    while (*str)
    {
        EPD_DrawChar5x7(x, y, *str);
        x += 6;
        str++;
    }
}

void EPD_ShowString(const char *text)
{
    memset(epd_buffer, 0xFF, sizeof(epd_buffer));

    EPD_DrawString5x7(8, 36, text);

    EPD_DisplayFrame(epd_buffer);
}

void EPD_ShowTestPattern(void)
{
    uint16_t x;
    uint16_t y;

    memset(epd_buffer, 0xFF, sizeof(epd_buffer));

    for (x = 0; x < EPD_LOG_WIDTH; x++)
    {
        EPD_DrawPixel(x, 0, 0);
        EPD_DrawPixel(x, EPD_LOG_HEIGHT - 1, 0);
    }

    for (y = 0; y < EPD_LOG_HEIGHT; y++)
    {
        EPD_DrawPixel(0, y, 0);
        EPD_DrawPixel(EPD_LOG_WIDTH - 1, y, 0);
    }

    EPD_DrawString5x7(10, 10, "MCU");
    EPD_DrawString5x7(10, 30, "Temp");
    EPD_DrawString5x7(10, 50, "123");

    EPD_DisplayFrame(epd_buffer);
}

void EPD_Sleep(void)
{
    EPD_SendCommand(0x07);
    EPD_SendData(0xA5);
    HAL_Delay(100);
}

