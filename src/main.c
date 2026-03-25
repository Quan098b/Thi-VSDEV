#include "tcs34725.h"
#include "i2c.h"
#include "usart.h"
#include "motor_control.h"
#include <stdio.h>
#include <string.h>

/* Define color labels */
typedef enum {
    COLOR_UNKNOWN = 0,
    COLOR_RED = 1,
    COLOR_GREEN = 2,
    COLOR_BLUE = 3,
    COLOR_WHITE = 4
} Color_Type;

typedef enum {
    RECOVER_FORWARD = 0,
    RECOVER_LEFT = 1,
    RECOVER_RIGHT = 2
} Recover_Dir;

typedef enum {
    MODE_TRACK_FORWARD = 0,
    MODE_TRACK_LEFT = 1,
    MODE_TRACK_RIGHT = 2,
    MODE_RECOVER = 3,
    MODE_STOP = 4
} Control_Mode;

static const char* getModeName(Control_Mode mode)
{
    switch(mode) {
        case MODE_TRACK_FORWARD: return "TRACK_FORWARD";
        case MODE_TRACK_LEFT:    return "TRACK_LEFT";
        case MODE_TRACK_RIGHT:   return "TRACK_RIGHT";
        case MODE_RECOVER:       return "RECOVER";
        case MODE_STOP:          return "STOP";
        default:                 return "UNKNOWN_MODE";
    }
}

/* Function to initialize debug LED on PC13 */
void init_LED_PC13(){
    RCC->APB2ENR |= (1 << 4); // IOPCEN
    GPIOC->CRH &= ~(0xF << 20);  // clear CNF13 & MODE13
    GPIOC->CRH |=  (0x2 << 20);  // MODE13 = 10
}

void delay(uint32_t t){
    while(t--);
}

void Blink_LED(){
    GPIOC->ODR &= ~(1 << 13);    // PC13 = 0
    delay(500000);
    GPIOC->ODR |= (1 << 13);     // PC13 = 1
    delay(500000);
}

static void log_line(const char *msg)
{
    USART_Send_bytes(msg, (uint16_t)strlen(msg));
}

/* Color detection algorithm */
Color_Type detectColor(int R, int G, int B, uint16_t clear) {
    /* Reject only too-dark samples */
    if (clear < 150) return COLOR_UNKNOWN;

    int max_val = R;
    if (G > max_val) max_val = G;
    if (B > max_val) max_val = B;

    int min_val = R;
    if (G < min_val) min_val = G;
    if (B < min_val) min_val = B;

    /* Low saturation + very high clear channel is typically white */
    if (clear >= 4500) return COLOR_WHITE;

    /* RED: R is the dominant channel (loosened B margin to handle S2 sensor variance) */
    if (R == max_val && R > G + 10 && R > B) return COLOR_RED;

    /* GREEN: G clearly exceeds R, and extra blue light does not push B more than 20 above G */
    if (G > R + 20 && G > B - 20) return COLOR_GREEN;

    /* BLUE: B clearly dominates both other channels */
    if (B == max_val && B > R + 20 && B > G + 20) return COLOR_BLUE;

    return COLOR_UNKNOWN;
}

/* Helper function to convert Color enum to String */
const char* getColorName(Color_Type color) {
    switch(color) {
        case COLOR_RED:   return "RED";
        case COLOR_GREEN: return "GREEN";
        case COLOR_BLUE:  return "BLUE";
        case COLOR_WHITE: return "WHITE";
        default:          return "UNKNOWN";
    }
}

static int isRunColor(Color_Type color)
{
    return (color == COLOR_RED) || (color == COLOR_GREEN) || (color == COLOR_BLUE);
}

int main(void)
{
    char st[100];

    // Variables for Sensor 1 and 2
    int normR1, normG1, normB1;
    uint16_t rawC1;
    Color_Type color1;

    int normR2, normG2, normB2;
    uint16_t rawC2;
    Color_Type color2;

    uint8_t whiteMissCount = 0;
    Recover_Dir lastRecoverDir = RECOVER_FORWARD;
    Control_Mode prevMode = MODE_STOP;
    Control_Mode currentMode = MODE_STOP;

    const uint16_t speedStraightBoost = 1600;
    const uint16_t speedStraightTransitionBoost = 1600;
    const uint16_t speedStraightCruise = 610;
    const uint16_t speedTurn = 300;
    const uint16_t speedTurnTransitionBoost = 500;
    const uint16_t speedRecover = 610;
    const uint16_t speedRecoverTransitionBoost = 760;
    const uint8_t whiteStopFrames = 12;
    const uint8_t forwardBoostWindow = 3;
    const uint8_t forwardTransitionBoostWindow = 2;
    const uint16_t forwardRampStepUp = 45;
    const uint16_t forwardRampStepDown = 25;

    uint8_t forwardBoostTicks = 0;
    uint8_t forwardTransitionBoostTicks = 0;
    uint16_t currentForwardPulse = 0;
    unsigned int loopCounter = 0;
    const uint8_t logDivider = 6;

    init_LED_PC13();
    Blink_LED();

    Usart_Int(115200);
    log_line("[BOOT] USART DONE\r\n");
    log_line("[BOOT] START I2C1 I2C2 INIT...\r\n");

    I2C_Peripheral_Init(I2C1);
    I2C_Peripheral_Init(I2C2);
    log_line("[BOOT] I2C1 and I2C2 SETUP DONE\r\n");
    Blink_LED();
    log_line("[BOOT] START TCS34725 INIT...\r\n");
    tcs3272_init(I2C1);
    log_line("[BOOT] I2C1 INIT DONE!\r\n");
    tcs3272_init(I2C2);
    log_line("[BOOT] I2C2 INIT DONE!\r\n");
    log_line("[BOOT] TCS34725 INIT DONE\r\n");

    log_line("[BOOT] START MOTOR INIT...\r\n");
    MotorControl_Init();
    Motor_Stop();
    log_line("[BOOT] MOTOR INIT DONE\r\n");
    log_line("[BOOT] SYSTEM READY\r\n");


    while(1)
    {
        loopCounter++;
        getRGB(I2C1, &normR1, &normG1, &normB1, &rawC1);
        color1 = detectColor(normR1, normG1, normB1, rawC1);
        if ((loopCounter % logDivider) == 0) {
            sprintf(st, "S1(I2C1) - R:%3d G:%3d B:%3d C:%5d => %s\r\n",
                normR1, normG1, normB1, rawC1, getColorName(color1));
            USART_Send_bytes(st, strlen(st));
        }

        getRGB(I2C2, &normR2, &normG2, &normB2, &rawC2);
        color2 = detectColor(normR2, normG2, normB2, rawC2);
        if ((loopCounter % logDivider) == 0) {
            sprintf(st, "S2(I2C2) - R:%3d G:%3d B:%3d C:%5d => %s\r\n",
                    normR2, normG2, normB2, rawC2, getColorName(color2));
            USART_Send_bytes(st, strlen(st));
        }

        uint8_t leftOnLine = isRunColor(color1);
        uint8_t rightOnLine = isRunColor(color2);

        if (leftOnLine && rightOnLine) {
            uint16_t targetForwardPulse;

            if (prevMode != MODE_TRACK_FORWARD) {
                forwardBoostTicks = forwardBoostWindow;
                forwardTransitionBoostTicks = forwardTransitionBoostWindow;
            }

            Motor_MoveForward();
            if (forwardTransitionBoostTicks > 0) {
                targetForwardPulse = speedStraightTransitionBoost;
                forwardTransitionBoostTicks--;
            } else if (forwardBoostTicks > 0) {
                targetForwardPulse = speedStraightBoost;
                forwardBoostTicks--;
            } else {
                targetForwardPulse = speedStraightCruise;
            }

            if (currentForwardPulse < targetForwardPulse) {
                uint16_t up = currentForwardPulse + forwardRampStepUp;
                if (up > targetForwardPulse) {
                    currentForwardPulse = targetForwardPulse;
                } else {
                    currentForwardPulse = up;
                }
            } else if (currentForwardPulse > targetForwardPulse) {
                if (currentForwardPulse > (targetForwardPulse + forwardRampStepDown)) {
                    currentForwardPulse -= forwardRampStepDown;
                } else {
                    currentForwardPulse = targetForwardPulse;
                }
            }

            MotorControl_SetSpeedPulse(currentForwardPulse);
            whiteMissCount = 0;
            lastRecoverDir = RECOVER_FORWARD;
            currentMode = MODE_TRACK_FORWARD;
        } else if (leftOnLine && !rightOnLine) {
            forwardBoostTicks = 0;
            forwardTransitionBoostTicks = 0;
            currentForwardPulse = 0;
            Motor_TurnLeft();
            if (prevMode != MODE_TRACK_LEFT) {
                MotorControl_SetSpeedPulse(speedTurnTransitionBoost);
            } else {
                MotorControl_SetSpeedPulse(speedTurn);
            }
            whiteMissCount = 0;
            lastRecoverDir = RECOVER_LEFT;
            currentMode = MODE_TRACK_LEFT;
        } else if (!leftOnLine && rightOnLine) {
            forwardBoostTicks = 0;
            forwardTransitionBoostTicks = 0;
            currentForwardPulse = 0;
            Motor_TurnRight();
            if (prevMode != MODE_TRACK_RIGHT) {
                MotorControl_SetSpeedPulse(speedTurnTransitionBoost);
            } else {
                MotorControl_SetSpeedPulse(speedTurn);
            }
            whiteMissCount = 0;
            lastRecoverDir = RECOVER_RIGHT;
            currentMode = MODE_TRACK_RIGHT;
        } else {
            forwardBoostTicks = 0;
            forwardTransitionBoostTicks = 0;
            currentForwardPulse = 0;
            whiteMissCount++;

            if (whiteMissCount <= whiteStopFrames) {
                if (lastRecoverDir == RECOVER_LEFT) {
                    Motor_TurnRight();
                } else if (lastRecoverDir == RECOVER_RIGHT) {
                    Motor_TurnLeft();
                } else {
                    Motor_MoveBackward();
                }
                if (prevMode != MODE_RECOVER) {
                    MotorControl_SetSpeedPulse(speedRecoverTransitionBoost);
                } else {
                    MotorControl_SetSpeedPulse(speedRecover);
                }
                currentMode = MODE_RECOVER;
            } else {
                Motor_Stop();
                currentMode = MODE_STOP;
            }
        }

        if (currentMode != prevMode) {
            sprintf(st, "[CTRL] MODE=%s white=%u\r\n", getModeName(currentMode), whiteMissCount);
            USART_Send_bytes(st, strlen(st));
            prevMode = currentMode;
        }

        if ((loopCounter % logDivider) == 0) {
            USART_Send_bytes("-----------------------------------\r\n", 37);
        }
    }
}
