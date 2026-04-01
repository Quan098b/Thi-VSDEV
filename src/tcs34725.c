#include "tcs34725.h"
#include "usart.h"
#include "i2c.h"
#include <stdio.h>
#include <string.h>

/*
 * tcs34725.c
 * - Driver cam bien mau TCS34725
 * - Co kiem tra loi I2C
 * - Neu loi bus / khong ACK se khong treo vo han
 * - Bao loi qua UART
 */

char *st2;
uint8_t _tcs34725Initialised[2] = {0, 0};
int red, green, blue;
uint16_t timeOut = 100;

/* Helper function to get index based on I2C instance */
uint8_t getI2CIndex(I2C_TypeDef *I2Cx)
{
    return (I2Cx == I2C1) ? 0 : 1;
}

/* Tao khoang thoi gian tre 1 mili giay */
void Delay_ms(uint32_t ms)
{
    uint32_t i, j;
    for (i = 0; i <= ms; i++)
        for (j = 0; j <= 8000; j++);
}

/* Writes a register and an 8 bit value over I2Cx */
int write8(I2C_TypeDef *I2Cx, uint8_t reg, uint32_t value)
{
    uint8_t txBuffer[2];
    txBuffer[0] = (TCS34725_COMMAND_BIT | reg);
    txBuffer[1] = (value & 0xFF);

    return I2C_writeTwoByte(I2Cx, txBuffer, TCS34725_ADDRESS);
}

/* Reads an 8 bit value over I2Cx */
int read8(I2C_TypeDef *I2Cx, uint8_t reg, uint8_t *value)
{
    uint8_t cmd = (TCS34725_COMMAND_BIT | reg);

    if (!I2C_writeByte(I2Cx, cmd, TCS34725_ADDRESS))
        return 0;

    if (!I2C_readByte(I2Cx, TCS34725_ADDRESS, value))
        return 0;

    return 1;
}

/* Reads a 16 bit value over I2Cx */
int read16(I2C_TypeDef *I2Cx, uint8_t reg, uint16_t *ret)
{
    uint8_t cmd = (TCS34725_COMMAND_BIT | reg);
    uint8_t rxBuffer[2];

    if (!I2C_writeByte(I2Cx, cmd, TCS34725_ADDRESS))
        return 0;

    if (!I2C_readTwoByte(I2Cx, TCS34725_ADDRESS, rxBuffer))
        return 0;

    *ret = ((uint16_t)rxBuffer[1] << 8) | (rxBuffer[0] & 0xFF);
    return 1;
}

int enable(I2C_TypeDef *I2Cx)
{
    if (!write8(I2Cx, TCS34725_ENABLE, TCS34725_ENABLE_PON))
        return 0;
    Delay_ms(3);

    if (!write8(I2Cx, TCS34725_ENABLE, TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN))
        return 0;
    Delay_ms(24);

    return 1;
}

int disable(I2C_TypeDef *I2Cx)
{
    uint8_t reg = 0;

    if (!read8(I2Cx, TCS34725_ENABLE, &reg))
        return 0;

    if (!write8(I2Cx, TCS34725_ENABLE, reg & ~(TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN)))
        return 0;

    return 1;
}

int setIntegrationTime(I2C_TypeDef *I2Cx, uint8_t itime)
{
    return write8(I2Cx, TCS34725_ATIME, itime);
}

int setGain(I2C_TypeDef *I2Cx, uint8_t gain)
{
    return write8(I2Cx, TCS34725_CONTROL, gain);
}

void tcs3272_init(I2C_TypeDef *I2Cx)
{
    uint8_t readValue = 0;
    char st3[32];

    st2 = "Khoi tao TCS34725!\r\n";
    USART_Send_bytes(st2, strlen(st2));

    if (!read8(I2Cx, TCS34725_ID, &readValue))
    {
        st2 = "LOI: Khong doc duoc ID qua I2C!\r\n";
        USART_Send_bytes(st2, strlen(st2));
        _tcs34725Initialised[getI2CIndex(I2Cx)] = 0;
        return;
    }

    sprintf(st3, "ID: %x\r\n", readValue);
    USART_Send_bytes(st3, strlen(st3));

    if ((readValue != 0x44) && (readValue != 0x4d))
    {
        st2 = "Khong khop dia chi I2C!\r\n";
        USART_Send_bytes(st2, strlen(st2));
        _tcs34725Initialised[getI2CIndex(I2Cx)] = 0;
        return;
    }

    st2 = "Khop dia chi THANH CONG!\r\n";
    USART_Send_bytes(st2, strlen(st2));

    st2 = "Thiet lap TIME INTERGRATION!*******\r\n";
    USART_Send_bytes(st2, strlen(st2));
    if (!setIntegrationTime(I2Cx, TCS34725_INTEGRATIONTIME_101MS))
    {
        st2 = "LOI: Ghi ATIME that bai!\r\n";
        USART_Send_bytes(st2, strlen(st2));
        _tcs34725Initialised[getI2CIndex(I2Cx)] = 0;
        return;
    }
    Delay_ms(3);

    st2 = "*****Thiet lap GAIN!******\r\n";
    USART_Send_bytes(st2, strlen(st2));
    if (!setGain(I2Cx, TCS34725_GAIN_4X))
    {
        st2 = "LOI: Ghi GAIN that bai!\r\n";
        USART_Send_bytes(st2, strlen(st2));
        _tcs34725Initialised[getI2CIndex(I2Cx)] = 0;
        return;
    }

    st2 = "*****Cho phep TCS34725******\r\n";
    USART_Send_bytes(st2, strlen(st2));
    if (!enable(I2Cx))
    {
        st2 = "LOI: Enable TCS34725 that bai!\r\n";
        USART_Send_bytes(st2, strlen(st2));
        _tcs34725Initialised[getI2CIndex(I2Cx)] = 0;
        return;
    }

    _tcs34725Initialised[getI2CIndex(I2Cx)] = 1;

    st2 = "Cho phep THANH CONG!!!!!!!!!\r\n";
    USART_Send_bytes(st2, strlen(st2));
}

/* Get raw data */
void getRawData(I2C_TypeDef *I2Cx, uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c)
{
    uint8_t idx = getI2CIndex(I2Cx);

    if (_tcs34725Initialised[idx] != 1)
    {
        *c = 0;
        *r = 0;
        *g = 0;
        *b = 0;
        return;
    }

    if (!read16(I2Cx, TCS34725_CDATAL, c))
    {
        *c = 0; *r = 0; *g = 0; *b = 0;
        return;
    }

    if (!read16(I2Cx, TCS34725_RDATAL, r))
    {
        *c = 0; *r = 0; *g = 0; *b = 0;
        return;
    }

    if (!read16(I2Cx, TCS34725_GDATAL, g))
    {
        *c = 0; *r = 0; *g = 0; *b = 0;
        return;
    }

    if (!read16(I2Cx, TCS34725_BDATAL, b))
    {
        *c = 0; *r = 0; *g = 0; *b = 0;
        return;
    }
}

/* Read color from registers: Red, Green, Blue and Clear from Raw Data */
void getRGB(I2C_TypeDef *I2Cx, int *R, int *G, int *B, uint16_t *c)
{
    uint16_t rawRed, rawGreen, rawBlue, rawClear;
    getRawData(I2Cx, &rawRed, &rawGreen, &rawBlue, &rawClear);

    *c = rawClear;

    if (rawClear == 0)
    {
        *R = 0;
        *G = 0;
        *B = 0;
    }
    else
    {
        *R = (int)rawRed   * 255 / rawClear;
        *G = (int)rawGreen * 255 / rawClear;
        *B = (int)rawBlue  * 255 / rawClear;
    }
}