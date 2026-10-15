#include "ssd1306.h"

#include <stddef.h>
#include <string.h>

#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8U)

static I2C_HandleTypeDef *ssd1306_i2c = NULL;
static uint8_t ssd1306_buffer[SSD1306_BUFFER_SIZE];

/*
 * Characters supported by the tiny 5x7 font below.
 * Lower-case letters are converted to upper-case by SSD1306_WriteChar().
 */
static const char font_chars[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-%?";

/* Each row is one character. Each byte is one vertical 5x7 font column. */
static const uint8_t font_data[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x03, 0x04, 0x78, 0x04, 0x03}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x62, 0x64, 0x08, 0x13, 0x23}, /* % */
    {0x02, 0x01, 0x51, 0x09, 0x06}  /* ? */
};

static HAL_StatusTypeDef SSD1306_WriteCommand(uint8_t command)
{
    uint8_t packet[2] = {0x00U, command};

    return HAL_I2C_Master_Transmit(
        ssd1306_i2c,
        SSD1306_I2C_ADDR,
        packet,
        sizeof(packet),
        100U
    );
}

static const uint8_t *SSD1306_GetGlyph(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        c = (char)(c - ('a' - 'A'));
    }

    for (size_t i = 0; i < (sizeof(font_chars) - 1U); i++)
    {
        if (font_chars[i] == c)
        {
            return font_data[i];
        }
    }

    /* Last entry is '?'. */
    return font_data[(sizeof(font_chars) - 2U)];
}

HAL_StatusTypeDef SSD1306_Init(I2C_HandleTypeDef *hi2c)
{
    static const uint8_t init_sequence[] = {
        0xAE,       /* display off */
        0x20, 0x02, /* page addressing mode */
        0xB0,       /* start at page 0 */
        0xC8,       /* COM scan direction remapped */
        0x00,       /* lower column start address */
        0x10,       /* higher column start address */
        0x40,       /* start line = 0 */
        0x81, 0xCF, /* contrast */
        0xA1,       /* segment remap */
        0xA6,       /* normal display */
        0xA8, 0x3F, /* multiplex ratio = 63 */
        0xA4,       /* output follows display RAM */
        0xD3, 0x00, /* display offset = 0 */
        0xD5, 0x80, /* clock divide ratio */
        0xD9, 0xF1, /* pre-charge period */
        0xDA, 0x12, /* COM pins configuration for 128x64 */
        0xDB, 0x40, /* VCOMH deselect level */
        0x8D, 0x14, /* charge pump on */
        0xAF        /* display on */
    };

    if (hi2c == NULL)
    {
        return HAL_ERROR;
    }

    ssd1306_i2c = hi2c;
    HAL_Delay(100U);

    if (HAL_I2C_IsDeviceReady(ssd1306_i2c, SSD1306_I2C_ADDR, 3U, 100U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    for (size_t i = 0; i < sizeof(init_sequence); i++)
    {
        if (SSD1306_WriteCommand(init_sequence[i]) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    SSD1306_Clear();
    return SSD1306_UpdateScreen();
}

void SSD1306_Clear(void)
{
    memset(ssd1306_buffer, 0, sizeof(ssd1306_buffer));
}

HAL_StatusTypeDef SSD1306_UpdateScreen(void)
{
    uint8_t packet[SSD1306_WIDTH + 1U];
    packet[0] = 0x40U; /* following bytes are display data */

    for (uint8_t page = 0; page < 8U; page++)
    {
        if (SSD1306_WriteCommand((uint8_t)(0xB0U + page)) != HAL_OK ||
            SSD1306_WriteCommand(0x00U) != HAL_OK ||
            SSD1306_WriteCommand(0x10U) != HAL_OK)
        {
            return HAL_ERROR;
        }

        memcpy(
            &packet[1],
            &ssd1306_buffer[(uint16_t)page * SSD1306_WIDTH],
            SSD1306_WIDTH
        );

        if (HAL_I2C_Master_Transmit(
                ssd1306_i2c,
                SSD1306_I2C_ADDR,
                packet,
                sizeof(packet),
                250U) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

void SSD1306_DrawPixel(uint8_t x, uint8_t y, uint8_t on)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT)
    {
        return;
    }

    uint16_t index = (uint16_t)x + ((uint16_t)(y / 8U) * SSD1306_WIDTH);
    uint8_t mask = (uint8_t)(1U << (y % 8U));

    if (on)
    {
        ssd1306_buffer[index] |= mask;
    }
    else
    {
        ssd1306_buffer[index] &= (uint8_t)~mask;
    }
}

void SSD1306_WriteChar(uint8_t x, uint8_t y, char c, uint8_t scale)
{
    if (scale == 0U)
    {
        scale = 1U;
    }

    const uint8_t *glyph = SSD1306_GetGlyph(c);

    for (uint8_t column = 0; column < 5U; column++)
    {
        for (uint8_t row = 0; row < 7U; row++)
        {
            if ((glyph[column] & (uint8_t)(1U << row)) != 0U)
            {
                for (uint8_t dx = 0; dx < scale; dx++)
                {
                    for (uint8_t dy = 0; dy < scale; dy++)
                    {
                        SSD1306_DrawPixel(
                            (uint8_t)(x + (column * scale) + dx),
                            (uint8_t)(y + (row * scale) + dy),
                            1U
                        );
                    }
                }
            }
        }
    }
}

void SSD1306_WriteString(uint8_t x, uint8_t y, const char *text, uint8_t scale)
{
    if (text == NULL)
    {
        return;
    }

    if (scale == 0U)
    {
        scale = 1U;
    }

    while (*text != '\0')
    {
        uint16_t char_width = (uint16_t)6U * scale;

        if (((uint16_t)x + char_width) > SSD1306_WIDTH)
        {
            break;
        }

        SSD1306_WriteChar(x, y, *text, scale);
        x = (uint8_t)(x + char_width);
        text++;
    }
}
