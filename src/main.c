#include "stm32f10x.h"
#include "i2c.h"
#include "tcs34725.h"
#include "usart.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* =========================================================
   PIN MAP
   SENSOR_LEFT  : I2C1 -> PB6(SCL),  PB7(SDA)
   SENSOR_RIGHT : I2C2 -> PB10(SCL), PB11(SDA)

   L298N
   ENA = PB0 = TIM3_CH3
   ENB = PB1 = TIM3_CH4
   IN1 = PB12
   IN2 = PB13
   IN3 = PB14
   IN4 = PB15
   ========================================================= */

typedef struct {
    int r;
    int g;
    int b;
    uint16_t c;
} ColorData;

typedef struct {
    ColorData raw;
    int16_t rn;
    int16_t gn;
    int16_t bn;
    uint32_t dist;
    int8_t idx;
    uint8_t match;
    uint8_t valid;
} SensorResult;

typedef enum {
    MOVE_STOP = 0,
    MOVE_FORWARD,
    MOVE_LEFT,
    MOVE_RIGHT
} MoveState;

/* =========================================================
   MAU MUC TIEU
   ========================================================= */
static const int16_t allowed_norm[3][3] = {
    {744, 137, 118},   /* DO */
    {278, 343, 377},   /* XDUONG */
    {292, 446, 261}    /* XLA */
};

#define MATCH_THRESHOLD         15000UL

/* =========================================================
   PWM
   ========================================================= */
#define PWM_PERIOD              999U
#define SPEED_SCALE_PERCENT     25U
#define MIN_EFFECTIVE_PWM       140U

#define PWM_FORWARD             850U
#define PWM_TURN_OUTER          680U /*phi nhanh*/
#define PWM_TURN_INNER          500U

/* =========================================================
   THOI GIAN
   ========================================================= */
#define SENSOR_LOOP_DELAY_MS    25U
#define DEBUG_PRINT_MS          400U

/* =========================================================
   BAT/TAT DEBUG
   0 = tat hoan toan
   1 = in gon
   ========================================================= */
#define DEBUG_UART              1

/* =========================================================
   TIME BASE 1ms
   ========================================================= */
static volatile uint32_t g_ms_ticks = 0;

void SysTick_Handler(void)
{
    g_ms_ticks++;
}

static uint32_t millis(void)
{
    return g_ms_ticks;
}

static void delay_ms_tick(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms);
}

/* =========================================================
   UART
   ========================================================= */
static void uart_send(const char *s)
{
    USART_Send_bytes((char *)s, (uint16_t)strlen(s));
}

static void uart_printf(const char *fmt, ...)
{
#if DEBUG_UART
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uart_send(buf);
#else
    (void)fmt;
#endif
}

/* =========================================================
   GPIO
   ========================================================= */
static void GPIO_Init_All(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

    /* PB0, PB1 = AF push-pull */
    GPIOB->CRL &= ~((0xF << 0) | (0xF << 4));
    GPIOB->CRL |=  ((0xB << 0) | (0xB << 4));

    /* PB12..PB15 = output push-pull */
    GPIOB->CRH &= ~((0xF << 16) | (0xF << 20) | (0xF << 24) | (0xF << 28));
    GPIOB->CRH |=  ((0x2 << 16) | (0x2 << 20) | (0x2 << 24) | (0x2 << 28));

    GPIOB->ODR &= ~((1 << 12) | (1 << 13) | (1 << 14) | (1 << 15));
}

/* =========================================================
   PWM TIM3
   ========================================================= */
static void PWM_TIM3_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    TIM3->PSC = 71;
    TIM3->ARR = PWM_PERIOD;
    TIM3->CCR3 = 0;
    TIM3->CCR4 = 0;

    TIM3->CCMR2 = 0;
    TIM3->CCMR2 |= TIM_CCMR2_OC3PE | (6 << 4);
    TIM3->CCMR2 |= TIM_CCMR2_OC4PE | (6 << 12);

    TIM3->CCER |= TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM3->CR1  |= TIM_CR1_ARPE;
    TIM3->EGR  |= TIM_EGR_UG;
    TIM3->CR1  |= TIM_CR1_CEN;
}

/* =========================================================
   MOTOR
   ========================================================= */
static uint16_t clamp_pwm(uint16_t v)
{
    return (v > PWM_PERIOD) ? PWM_PERIOD : v;
}

static uint16_t apply_speed_scale(uint16_t v)
{
    uint32_t scaled = ((uint32_t)v * SPEED_SCALE_PERCENT) / 100U;

    if ((scaled > 0U) && (scaled < MIN_EFFECTIVE_PWM)) {
        scaled = MIN_EFFECTIVE_PWM;
    }
    if (scaled > PWM_PERIOD) {
        scaled = PWM_PERIOD;
    }
    return (uint16_t)scaled;
}

static void Motor_SetForwardDirection(void)
{
    GPIOB->ODR |=  (1 << 12);
    GPIOB->ODR &= ~(1 << 13);

    GPIOB->ODR |=  (1 << 14);
    GPIOB->ODR &= ~(1 << 15);
}

static void Motor_SetPWM(uint16_t left_pwm, uint16_t right_pwm)
{
    TIM3->CCR3 = apply_speed_scale(clamp_pwm(left_pwm));
    TIM3->CCR4 = apply_speed_scale(clamp_pwm(right_pwm));
}

static void Motor_Stop(void)
{
    TIM3->CCR3 = 0;
    TIM3->CCR4 = 0;
    GPIOB->ODR &= ~((1 << 12) | (1 << 13) | (1 << 14) | (1 << 15));
}

static void Motor_Forward(void)
{
    Motor_SetForwardDirection();
    Motor_SetPWM(PWM_FORWARD, PWM_FORWARD);
}

static void Motor_Bias_Left(void)
{
    Motor_SetForwardDirection();
    Motor_SetPWM(PWM_TURN_INNER, PWM_TURN_OUTER);
}

static void Motor_Bias_Right(void)
{
    Motor_SetForwardDirection();
    Motor_SetPWM(PWM_TURN_OUTER, PWM_TURN_INNER);
}

/* =========================================================
   SENSOR
   ========================================================= */
static void Sensor_Init_All(void)
{
    I2C_Peripheral_Init(I2C1);
    tcs3272_init(I2C1);

    I2C_Peripheral_Init(I2C2);
    tcs3272_init(I2C2);
}

static void read_sensor_raw(I2C_TypeDef *I2Cx, ColorData *d)
{
    getRGB(I2Cx, &d->r, &d->g, &d->b, &d->c);
}

/* =========================================================
   COLOR
   ========================================================= */
static void calc_norm_rgb(const ColorData *src, int16_t *rn, int16_t *gn, int16_t *bn)
{
    int32_t sum = src->r + src->g + src->b;

    if (sum <= 0) {
        *rn = 0;
        *gn = 0;
        *bn = 0;
        return;
    }

    *rn = (int16_t)((src->r * 1000L) / sum);
    *gn = (int16_t)((src->g * 1000L) / sum);
    *bn = (int16_t)((src->b * 1000L) / sum);
}

static int8_t classify_from_norm(int16_t rn, int16_t gn, int16_t bn, uint32_t *best_dist)
{
    uint8_t i;
    int8_t best_idx = -1;
    uint32_t min_dist = 0xFFFFFFFFUL;

    for (i = 0; i < 3; i++) {
        int32_t dr = rn - allowed_norm[i][0];
        int32_t dg = gn - allowed_norm[i][1];
        int32_t db = bn - allowed_norm[i][2];
        uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);

        if (d < min_dist) {
            min_dist = d;
            best_idx = (int8_t)i;
        }
    }

    *best_dist = min_dist;
    return best_idx;
}

static void sensor_read_and_classify(I2C_TypeDef *I2Cx, SensorResult *res)
{
    read_sensor_raw(I2Cx, &res->raw);

    if ((res->raw.r == 0) && (res->raw.g == 0) && (res->raw.b == 0) && (res->raw.c == 0)) {
        res->rn = 0;
        res->gn = 0;
        res->bn = 0;
        res->dist = 0xFFFFFFFFUL;
        res->idx = -1;
        res->match = 0;
        res->valid = 0;
        return;
    }

    calc_norm_rgb(&res->raw, &res->rn, &res->gn, &res->bn);
    res->idx = classify_from_norm(res->rn, res->gn, res->bn, &res->dist);
    res->match = ((res->idx >= 0) && (res->dist <= MATCH_THRESHOLD)) ? 1U : 0U;
    res->valid = 1;
}

static MoveState decide_move(const SensorResult *left, const SensorResult *right)
{
    if ((!left->valid) && (!right->valid)) return MOVE_STOP;
    if (left->match && right->match) return MOVE_FORWARD;

    /* Dao huong vi thuc te xe dang cua nguoc */
    if (left->match)  return MOVE_RIGHT;
    if (right->match) return MOVE_LEFT;

    return MOVE_STOP;
}

static void apply_move(MoveState state)
{
    switch (state) {
        case MOVE_FORWARD: Motor_Forward();    break;
        case MOVE_LEFT:    Motor_Bias_Left();  break;
        case MOVE_RIGHT:   Motor_Bias_Right(); break;
        default:           Motor_Stop();       break;
    }
}

static const char* short_name(const SensorResult *s)
{
    if (!s->valid) return "ERR";
    if (!s->match) return "W";

    switch (s->idx) {
        case 0: return "R";
        case 1: return "B";
        case 2: return "G";
        default: return "?";
    }
}

/* =========================================================
   BIEN TINH TOAN CUC
   ========================================================= */
static SensorResult g_left;
static SensorResult g_right;
static uint32_t g_last_print_ms = 0;

/* =========================================================
   MAIN LOOP
   ========================================================= */
static void process_dual_sensor(void)
{
    MoveState state;
    uint32_t now = millis();

    sensor_read_and_classify(I2C1, &g_left);
    sensor_read_and_classify(I2C2, &g_right);

    state = decide_move(&g_left, &g_right);
    apply_move(state);

#if DEBUG_UART
    if ((now - g_last_print_ms) >= DEBUG_PRINT_MS) {
        g_last_print_ms = now;
        uart_printf("L:%s R:%s M:%d\r\n",
                    short_name(&g_left),
                    short_name(&g_right),
                    (int)state);
    }
#endif

    delay_ms_tick(SENSOR_LOOP_DELAY_MS);
}

/* =========================================================
   MAIN
   ========================================================= */
int main(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);

    Usart_Int(115200);
    uart_send("RUN\r\n");

    GPIO_Init_All();
    PWM_TIM3_Init();
    Sensor_Init_All();

    Motor_Stop();

    while (1)
    {
        process_dual_sensor();
    }
}