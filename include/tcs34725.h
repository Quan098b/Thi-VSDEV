#ifndef __TCS34725__H__
#define __TCS34725__H__
/* USER CODE BEGIN 0 */

#include "stm32f10x.h"
#include "i2c.h" // Add i2c include for I2C_TypeDef

#define TCS34725_ADDRESS          (0x29 << 1) /* I2C address */
/* Datasheet is at https://cdn-shop.adafruit.com/datasheets/TCS34725.pdf */
#define TCS34725_COMMAND_BIT      (0x80)      /* Command bit */
#define TCS34725_ENABLE           (0x00)      /* Enable register */
#define TCS34725_ENABLE_AEN       (0x02)      /* RGBC Enable */
#define TCS34725_ENABLE_PON       (0x01)      /* Power on */
#define TCS34725_ATIME            (0x01)      /* Integration time */
#define TCS34725_CONTROL          (0x0F)      /* Set the gain level */
#define TCS34725_ID               (0x12)
/* 0x44 = TCS34721/TCS34725, 0x4D = TCS34723/TCS34727 */
#define TCS34725_CDATAL           (0x14)      /* Clear channel data */
#define TCS34725_CDATAH           (0x15)
#define TCS34725_RDATAL           (0x16)      /* Red channel data */
#define TCS34725_RDATAH           (0x17)
#define TCS34725_GDATAL           (0x18)      /* Green channel data */
#define TCS34725_GDATAH           (0x19)
#define TCS34725_BDATAL           (0x1A)      /* Blue channel data */
#define TCS34725_BDATAH           (0x1B)

//#define TCS34725_INTEGRATIONTIME_50MS   0xEB  /* 50ms  - 20 cycles */
#define TCS34725_INTEGRATIONTIME_24MS   0xF6  /* 24ms  - 10 cycles */
#define TCS34725_INTEGRATIONTIME_101MS   0xD5  /* 101ms  - 42 cycles */
#define TCS34725_GAIN_4X                0x01  /* 4x gain  */

 void Delay_ms(uint32_t ms);

 /* Added I2Cx parameter to allow multiple sensors */
 void write8 (I2C_TypeDef *I2Cx, uint8_t reg, uint32_t value);
 uint8_t read8(I2C_TypeDef *I2Cx, uint8_t reg);
 uint16_t read16(I2C_TypeDef *I2Cx, uint8_t reg);
 void enable(I2C_TypeDef *I2Cx);
 void disable(I2C_TypeDef *I2Cx);
 void setIntegrationTime(I2C_TypeDef *I2Cx, uint8_t it);
 void setGain(I2C_TypeDef *I2Cx, uint8_t gain);
 void tcs3272_init(I2C_TypeDef *I2Cx);
 void getRawData (I2C_TypeDef *I2Cx, uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c);
 void getRGB(I2C_TypeDef *I2Cx, int *R, int *G, int *B, uint16_t *c);
#endif //__TCS34725__H__
