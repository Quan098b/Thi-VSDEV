#include "tcs34725.h"
#include "usart.h"
#include "i2c.h"
#include <stdio.h>
#include <string.h>

/*
 * File: tcs34725.c
 * Chức năng:
 * - Driver cảm biến màu TCS34725.
 * - Khởi tạo cảm biến, đọc ID, cấu hình gain/thời gian tích phân.
 * - Đọc dữ liệu màu thô (R/G/B/C) và chuẩn hóa về thang dễ xử lý.
 */

char *st2;
// Array to keep track of initialization status: index 0 for I2C1, index 1 for I2C2
uint8_t _tcs34725Initialised[2] = {0, 0};
int red, green, blue;
uint16_t timeOut = 100;

/* Helper function to get index based on I2C instance */
uint8_t getI2CIndex(I2C_TypeDef *I2Cx) {
    return (I2Cx == I2C1) ? 0 : 1;
}

 // Tao khoang thoi gian tre 1 mili giay!
 void Delay_ms(uint32_t ms)
 {
    uint32_t i, j;
    for (i=0; i<=ms; i++)
    	for (j=0; j<=8000; j++);
 }


/* Writes a register and an 8 bit value over I2Cx */
void write8(I2C_TypeDef *I2Cx, uint8_t reg, uint32_t value)
{
    uint8_t txBuffer[2];
    txBuffer[0] = (TCS34725_COMMAND_BIT | reg);
    txBuffer[1] = (value & 0xFF);

    I2C_writeTwoByte(I2Cx, txBuffer, TCS34725_ADDRESS);
}

/* Reads an 8 bit value over I2Cx */
uint8_t read8(I2C_TypeDef *I2Cx, uint8_t reg)
{
	uint8_t buffer[1];
	buffer[0] = (TCS34725_COMMAND_BIT | reg);

	I2C_writeByte(I2Cx, buffer[0], TCS34725_ADDRESS);

	buffer[0] = I2C_readByte(I2Cx, TCS34725_ADDRESS);
    return buffer[0];
}

/* Reads a 16 bit values over I2Cx */
uint16_t read16(I2C_TypeDef *I2Cx, uint8_t reg)
{
    uint16_t ret;
    uint8_t txBuffer[1], rxBuffer[2];
    txBuffer[0] = (TCS34725_COMMAND_BIT | reg);

    I2C_writeByte(I2Cx, txBuffer[0], TCS34725_ADDRESS);
    I2C_readTwoByte(I2Cx, TCS34725_ADDRESS, rxBuffer);
    ret = rxBuffer[1];
    ret <<= 8;
    ret |= rxBuffer[0] & 0xFF;
    return ret;
}

void enable(I2C_TypeDef *I2Cx)
{
	    // Step 1: Power ON
	    write8(I2Cx, TCS34725_ENABLE, TCS34725_ENABLE_PON);
	    Delay_ms(3);

	    // Step 2: Enable ADC
	    write8(I2Cx, TCS34725_ENABLE, TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN);
	    Delay_ms(24);
}

void disable(I2C_TypeDef *I2Cx)
{
  /* Turn the device off to save power */
  uint8_t reg = 0;
  reg = read8(I2Cx, TCS34725_ENABLE);
  write8(I2Cx, TCS34725_ENABLE, reg & ~(TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN));
}

void setIntegrationTime(I2C_TypeDef *I2Cx, uint8_t itime)
{
    write8(I2Cx, TCS34725_ATIME, itime);
}

void setGain(I2C_TypeDef *I2Cx, uint8_t gain)
{
    write8(I2Cx, TCS34725_CONTROL, gain);
}

void tcs3272_init(I2C_TypeDef *I2Cx)
{
	st2="Khoi tao TCS34725!\r\n";
    USART_Send_bytes(st2,strlen(st2));
    uint8_t readValue = read8(I2Cx, TCS34725_ID);

    char st3[15];
    sprintf(st3, "ID: %x\r\n", readValue);
    USART_Send_bytes(st3,strlen(st3));

    if ((readValue != 0x44) && (readValue !=0x4d))    // 0x44 || 0x4D
    {
	  st2="Khong khop dia chi I2C!\r\n";
	  USART_Send_bytes(st2,strlen(st2));
      return;
    }
    st2="Khop dia chi THANH CONG!\r\n";
    USART_Send_bytes(st2,strlen(st2));

    // Mark this specific I2C instance as initialized
    _tcs34725Initialised[getI2CIndex(I2Cx)] = 1;

    st2="Thiet lap TIME INTERGRATION!*******\r\n";
    USART_Send_bytes(st2,strlen(st2));
    setIntegrationTime(I2Cx, TCS34725_INTEGRATIONTIME_101MS);
    Delay_ms(3);

    st2="*****Thiet lap GAIN!******\r\n";
    USART_Send_bytes(st2,strlen(st2));
    setGain(I2Cx, TCS34725_GAIN_4X);

    st2="*****Cho phep TCS34725******\r\n";
    USART_Send_bytes(st2,strlen(st2));
    enable(I2Cx);

    st2="Cho phep THANH CONG!!!!!!!!!\r\n";
    USART_Send_bytes(st2,strlen(st2));
}

/* Get raw data */
void getRawData (I2C_TypeDef *I2Cx, uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c)
{
  if (_tcs34725Initialised[getI2CIndex(I2Cx)] == 1) {
	  *c = read16(I2Cx, TCS34725_CDATAL);
	  *r = read16(I2Cx, TCS34725_RDATAL);
	  *g = read16(I2Cx, TCS34725_GDATAL);
	  *b = read16(I2Cx, TCS34725_BDATAL);
	  /* Delay time is from page no 16/26 from the datasheet  (256 - ATIME)* 2.4ms */
//	  Delay_ms(101); // Set delay for (256 - 0xD5)* 2.4ms = 101ms
  }
}

/* Doc mau sac tu cac thanh ghi: Red, Green and Blue color from Raw Data */
/* Read color from registers: Red, Green, Blue and Clear from Raw Data */
void getRGB(I2C_TypeDef *I2Cx, int *R, int *G, int *B, uint16_t *c)
{
    uint16_t rawRed, rawGreen, rawBlue, rawClear;
    getRawData(I2Cx, &rawRed, &rawGreen, &rawBlue, &rawClear);

    // Pass the raw clear value back to the caller
    *c = rawClear;

    if(rawClear == 0)
    {
      *R = 0;
      *G = 0;
      *B = 0;
    }
    else  // Normalize values to 0-255 range for easier processing
    {
      *R = (int)rawRed * 255 / rawClear;
      *G = (int)rawGreen * 255 / rawClear;
      *B = (int)rawBlue * 255 / rawClear;
    }
}
