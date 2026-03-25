#include "i2c.h"

/* Initialize I2C based on the selected peripheral (I2C1 or I2C2) */
void I2C_Peripheral_Init(I2C_TypeDef* I2Cx)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef  I2C_InitStructure;

    // Enable clock for GPIOB
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* Configure Pins based on I2C instance */
    if (I2Cx == I2C1) {
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
        // Configure PB6 (SCL) and PB7 (SDA) in Alternate function open-drain mode
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    } else if (I2Cx == I2C2) {
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
        // Configure PB10 (SCL) and PB11 (SDA) in Alternate function open-drain mode
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    }

    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // Reset I2Cx to ensure initial state
    I2C_DeInit(I2Cx);

    // Configure I2Cx parameters
    I2C_InitStructure.I2C_ClockSpeed = 100000; // 100kHz
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00; // Master address, not used
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;

    I2C_Init(I2Cx, &I2C_InitStructure);

    // Enable I2Cx
    I2C_Cmd(I2Cx, ENABLE);
}



void I2C_Transmit(I2C_TypeDef* I2Cx, uint8_t slaveAddr, uint8_t *pData, uint16_t size)
{
    uint16_t i;
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY)); // Wait until I2C is ready

    // Generate START signal
    I2C_GenerateSTART(I2Cx, ENABLE);
    // Wait until START is sent
    while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_MODE_SELECT));

    // Send slave address (write direction)
    I2C_Send7bitAddress(I2Cx, slaveAddr, I2C_Direction_Transmitter);
    // Wait for ACK from slave
    while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));

    // Send data
    for(i = 0; i < size; i++)
    {
        I2C_SendData(I2Cx, pData[i]);
        // Wait for byte to be transmitted
        while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_TRANSMITTED));
    }

    // Generate STOP signal
    I2C_GenerateSTOP(I2Cx, ENABLE);
}

void I2C_Receive(I2C_TypeDef* I2Cx, uint8_t slaveAddr, uint8_t *pData, uint16_t size)
{
    uint16_t i;
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY)); // Wait until I2C is ready

    // Generate START signal
    I2C_GenerateSTART(I2Cx, ENABLE);
    while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_MODE_SELECT));

    // Send slave address with read direction
    I2C_Send7bitAddress(I2Cx, slaveAddr, I2C_Direction_Receiver);
    while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED));

    for(i = 0; i < size; i++)
    {
        // If it is the last byte, disable ACK to end reading
        if(i == (size - 1))
        {
            I2C_AcknowledgeConfig(I2Cx, DISABLE);
        }

        // Wait for received data
        while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_RECEIVED));
        pData[i] = I2C_ReceiveData(I2Cx);
    }

    // Generate STOP signal
    I2C_GenerateSTOP(I2Cx, ENABLE);

    // Re-enable ACK for next read
    I2C_AcknowledgeConfig(I2Cx, ENABLE);
}

/* Wrapper functions for TCS34725 compatibility */
void I2C_writeByte(I2C_TypeDef* I2Cx, uint8_t data, uint8_t address) {
    I2C_Transmit(I2Cx, address, &data, 1);
}

void I2C_writeTwoByte(I2C_TypeDef* I2Cx, uint8_t* data, uint8_t address) {
    I2C_Transmit(I2Cx, address, data, 2);
}

uint8_t I2C_readByte(I2C_TypeDef* I2Cx, uint8_t address) {
    uint8_t data;
    I2C_Receive(I2Cx, address, &data, 1);
    return data;
}

void I2C_readTwoByte(I2C_TypeDef* I2Cx, uint8_t address, uint8_t* data) {
    I2C_Receive(I2Cx, address, data, 2);
}
