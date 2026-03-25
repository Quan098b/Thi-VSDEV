#include "stm32f10x.h"
#include "tcs34725.h"
#include "i2c.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    int r;
    int g;
    int b;
    uint16_t c;
    uint8_t valid;
} ColorSample;

/* ==========================
   LED PC13
   ========================= */
void init_LED_PC13(void)
{
    RCC->APB2ENR |= (1 << 4);      // IOPCEN
    GPIOC->CRH &= ~(0xF << 20);
    GPIOC->CRH |=  (0x2 << 20);    // output 2MHz
}

void delay_simple(uint32_t t)
{
    while (t--);
}

void Blink_LED(void)
{
    GPIOC->ODR &= ~(1 << 13);
    delay_simple(300000);
    GPIOC->ODR |= (1 << 13);
    delay_simple(300000);
}

/* =========================
   UART helper
   ========================= */
static void uart_send_text(const char *s)
{
    USART_Send_bytes((char *)s, (uint16_t)strlen(s));
}

static void uart_send_line(const char *s)
{
    uart_send_text(s);
    uart_send_text("\r\n");
}

static int uart_read_char_nonblock(char *ch)
{
    if (USART1->SR & USART_SR_RXNE) {
        *ch = (char)(USART1->DR & 0xFF);
        return 1;
    }
    return 0;
}

/* =========================
   Color distance
   ========================= */
static uint32_t abs_i32(int x)
{
    return (x < 0) ? (uint32_t)(-x) : (uint32_t)x;
}

static uint32_t color_distance_rgb(int r, int g, int b, const ColorSample *s)
{
    uint32_t dr = abs_i32(r - s->r);
    uint32_t dg = abs_i32(g - s->g);
    uint32_t db = abs_i32(b - s->b);
    return dr + dg + db;
}

static int classify_to_index(int r, int g, int b, ColorSample samples[5])
{
    uint32_t bestDist = 0xFFFFFFFF;
    int bestIndex = 0;
    int i;

    for (i = 0; i < 5; i++) {
        if (samples[i].valid) {
            uint32_t d = color_distance_rgb(r, g, b, &samples[i]);
            if (d < bestDist) {
                bestDist = d;
                bestIndex = i + 1;   // trả về 1..5
            }
        }
    }

    return bestIndex; // 0 nếu chưa có mẫu nào
}

/* =========================
   MAIN
   ========================= */
int main(void)
{
    int normR, normG, normB;
    uint16_t rawC;
    char rx;
    char st[64];

    ColorSample samples[5] = {
        {0,0,0,0,0},
        {0,0,0,0,0},
        {0,0,0,0,0},
        {0,0,0,0,0},
        {0,0,0,0,0}
    };

    int waitingSample = 0;          // 0 = không chờ, 1..5 = đang chờ lưu mẫu số đó
    int detectEnable = 0;           // 0 = không nhận diện, 1 = nhận diện
    int currentClass = 0;
    int lastPrintedClass = -1;

    init_LED_PC13();
    Blink_LED();

    Usart_Int(115200);
    uart_send_line("");
    uart_send_line("SYSTEM READY");
    uart_send_line("Sensor: TCS34725 on I2C1 PB6/PB7");
    uart_send_line("1..5: press once to wait, press same key again to save");
    uart_send_line("m: toggle detect mode");
    uart_send_line("");

    I2C_Peripheral_Init(I2C1);
    tcs3272_init(I2C1);

    while (1)
    {
        getRGB(I2C1, &normR, &normG, &normB, &rawC);

        if (uart_read_char_nonblock(&rx)) {

            if (rx >= '1' && rx <= '5') {
                int key = rx - '0';

                if (waitingSample == 0) {
                    waitingSample = key;
                    uart_send_text("WAIT ");
                    USART_Send_bytes(&rx, 1);
                    uart_send_text("\r\n");
                    Blink_LED();
                }
                else if (waitingSample == key) {
                    samples[key - 1].r = normR;
                    samples[key - 1].g = normG;
                    samples[key - 1].b = normB;
                    samples[key - 1].c = rawC;
                    samples[key - 1].valid = 1;

                    uart_send_text("SAVE ");
                    USART_Send_bytes(&rx, 1);
                    uart_send_text("\r\n");

                    waitingSample = 0;
                    Blink_LED();
                    Blink_LED();
                }
                else {
                    waitingSample = key;
                    uart_send_text("WAIT ");
                    USART_Send_bytes(&rx, 1);
                    uart_send_text("\r\n");
                    Blink_LED();
                }
            }
            else if (rx == 'm' || rx == 'M') {
                detectEnable = !detectEnable;
                lastPrintedClass = -1;

                if (detectEnable) {
                    uart_send_line("DETECT ON");
                } else {
                    uart_send_line("DETECT OFF");
                }
                Blink_LED();
            }
        }

        /* Đang chờ lưu mẫu thì không nhận diện */
        if (waitingSample != 0) {
            delay_simple(200000);
            continue;
        }

        /* Chưa bật nhận diện thì không in */
        if (!detectEnable) {
            delay_simple(200000);
            continue;
        }

        currentClass = classify_to_index(normR, normG, normB, samples);

        if (currentClass != 0 && currentClass != lastPrintedClass) {
            sprintf(st, "%d\r\n", currentClass);
            uart_send_text(st);
            lastPrintedClass = currentClass;
        }

        delay_simple(200000);
    }
}