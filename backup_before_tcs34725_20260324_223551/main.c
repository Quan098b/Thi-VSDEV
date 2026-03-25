#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

UART_HandleTypeDef huart1;

/* =========================
   CẤU HÌNH GỬI
   ========================= */
#define TOTAL_CP           5
#define MIN_INTERVAL_MS    5000U
#define MAX_INTERVAL_MS   10000U

static const char *TEAM_A = "ESDEV01";
static const char *TEAM_B = "ESDEV02";

/* =========================
   BIẾN ĐIỀU KHIỂN
   ========================= */
static uint32_t nextSendTick = 0;
static uint32_t randState    = 0x12345678;

static uint8_t currentTeam = 0;   // 0=A, 1=B
static uint8_t currentCpA  = 1;
static uint8_t currentCpB  = 1;

static uint8_t sendArrivedA = 0;  // 0: CPx, 1: CP0x
static uint8_t sendArrivedB = 0;

/* =========================
   KHAI BÁO HÀM
   ========================= */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);

static void UART1_SendString(const char *s);
static void UART1_SendLine(const char *s);
static uint32_t FakeRand(void);
static uint32_t RandomIntervalMs(void);
static void ScheduleNextSend(void);
static void SendPacket(const char *teamName, uint8_t cp, uint8_t arrived);
static void ProcessFakeTeamA(void);
static void ProcessFakeTeamB(void);

/* =========================
   HÀM GỬI UART1 -> Zigbee
   ========================= */
static void UART1_SendString(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 1000);
}

static void UART1_SendLine(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 1000);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\n", 1, 1000);
}

/* =========================
   RANDOM ĐƠN GIẢN
   ========================= */
static uint32_t FakeRand(void)
{
    randState = randState * 1664525UL + 1013904223UL + HAL_GetTick();
    return randState;
}

static uint32_t RandomIntervalMs(void)
{
    uint32_t range = MAX_INTERVAL_MS - MIN_INTERVAL_MS + 1U;
    return MIN_INTERVAL_MS + (FakeRand() % range);
}

static void ScheduleNextSend(void)
{
    nextSendTick = HAL_GetTick() + RandomIntervalMs();
}

/* =========================
   GÓI TIN GỬI QUA ZIGBEE
   Ví dụ:
   ESDEV01_CP1
   ESDEV01_CP01
   ========================= */
static void SendPacket(const char *teamName, uint8_t cp, uint8_t arrived)
{
    char packet[32];

    if (arrived)
        snprintf(packet, sizeof(packet), "%s_CP0%d", teamName, cp);
    else
        snprintf(packet, sizeof(packet), "%s_CP%d", teamName, cp);

    UART1_SendLine(packet);
}

/* =========================
   LUỒNG GIẢ LẬP ĐỘI A
   ========================= */
static void ProcessFakeTeamA(void)
{
    SendPacket(TEAM_A, currentCpA, sendArrivedA);

    if (sendArrivedA == 0)
    {
        sendArrivedA = 1;
    }
    else
    {
        sendArrivedA = 0;
        currentCpA++;
        if (currentCpA > TOTAL_CP)
            currentCpA = 1;
    }
}

/* =========================
   LUỒNG GIẢ LẬP ĐỘI B
   ========================= */
static void ProcessFakeTeamB(void)
{
    SendPacket(TEAM_B, currentCpB, sendArrivedB);

    if (sendArrivedB == 0)
    {
        sendArrivedB = 1;
    }
    else
    {
        sendArrivedB = 0;
        currentCpB++;
        if (currentCpB > TOTAL_CP)
            currentCpB = 1;
    }
}

/* =========================
   MAIN
   ========================= */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART1_UART_Init();

    randState ^= HAL_GetTick();

    HAL_Delay(1000);

    ScheduleNextSend();

    while (1)
    {
        if ((int32_t)(HAL_GetTick() - nextSendTick) >= 0)
        {
            if (currentTeam == 0)
            {
                ProcessFakeTeamA();
                currentTeam = 1;
            }
            else
            {
                ProcessFakeTeamB();
                currentTeam = 0;
            }

            ScheduleNextSend();
        }
    }
}

/* =========================
   USART1 INIT
   PA9  = TX
   PA10 = RX
   Baud = 115200
   ========================= */
static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
}

/* =========================
   CLOCK CONFIG
   Nếu project của bạn đã có sẵn
   thì giữ nguyên file CubeMX sinh ra
   ========================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1  |
                                  RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}