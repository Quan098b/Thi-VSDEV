#include "stm32f10x.h"
#include "i2c.h"
#include "tcs34725.h"
#include "usart.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* =========================================================
   PIN MAP
   =========================================================
   LED1  : PA0
   LED2  : PA1

   BTN1  : PA2
   BTN2  : PA3
   BTN3  : PA4
   BTN4  : PA5
   BTN5  : PA6
   BTN6  : PA7

   SENSOR1: I2C1 -> PB6(SCL), PB7(SDA)
   ========================================================= */

typedef struct {
    int r;
    int g;
    int b;
    uint16_t c;
} ColorData;

/* =========================================================
   DELAY
   ========================================================= */
static void delay_simple(volatile uint32_t t)
{
    while (t--);
}

/* =========================================================
   UART DEBUG
   ========================================================= */
static void uart_send(const char *s)
{
    USART_Send_bytes((char *)s, (uint16_t)strlen(s));
}

/* =========================================================
   RESET FLAGS
   ========================================================= */
static void print_reset_flags(void)
{
    uint32_t csr = RCC->CSR;

    if (csr & RCC_CSR_PORRSTF)  uart_send("RST: POR/PDR\r\n");
    if (csr & RCC_CSR_PINRSTF)  uart_send("RST: NRST PIN\r\n");
    if (csr & RCC_CSR_SFTRSTF)  uart_send("RST: SOFTWARE\r\n");
    if (csr & RCC_CSR_IWDGRSTF) uart_send("RST: IWDG\r\n");
    if (csr & RCC_CSR_WWDGRSTF) uart_send("RST: WWDG\r\n");
    if (csr & RCC_CSR_LPWRRSTF) uart_send("RST: LOW POWER\r\n");

    RCC->CSR |= RCC_CSR_RMVF;
}

/* =========================================================
   GPIO INIT
   ========================================================= */
static void GPIO_Init_All(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /* PA0, PA1 = output push-pull, 2MHz */
    GPIOA->CRL &= ~((0xF << 0) | (0xF << 4));
    GPIOA->CRL |=  ((0x2 << 0) | (0x2 << 4));

    /* PA2..PA7 = input pull-up */
    GPIOA->CRL &= ~((0xF << 8)  | (0xF << 12) | (0xF << 16) |
                    (0xF << 20) | (0xF << 24) | (0xF << 28));

    GPIOA->CRL |=  ((0x8 << 8)  | (0x8 << 12) | (0x8 << 16) |
                    (0x8 << 20) | (0x8 << 24) | (0x8 << 28));

    GPIOA->ODR |= (1 << 2) | (1 << 3) | (1 << 4) |
                  (1 << 5) | (1 << 6) | (1 << 7);

    GPIOA->ODR &= ~((1 << 0) | (1 << 1));
}

/* =========================================================
   LED
   ========================================================= */
static void LED1_On(void)  { GPIOA->ODR |=  (1 << 0); }
static void LED1_Off(void) { GPIOA->ODR &= ~(1 << 0); }

static void LED2_On(void)  { GPIOA->ODR |=  (1 << 1); }
static void LED2_Off(void) { GPIOA->ODR &= ~(1 << 1); }

/* =========================================================
   BUTTON RAW
   Nhấn = mức 0
   ========================================================= */
static uint8_t button_raw_read(uint8_t btn)
{
    switch (btn) {
        case 1: return (GPIOA->IDR & (1 << 2)) ? 1 : 0;
        case 2: return (GPIOA->IDR & (1 << 3)) ? 1 : 0;
        case 3: return (GPIOA->IDR & (1 << 4)) ? 1 : 0;
        case 4: return (GPIOA->IDR & (1 << 5)) ? 1 : 0;
        case 5: return (GPIOA->IDR & (1 << 6)) ? 1 : 0;
        case 6: return (GPIOA->IDR & (1 << 7)) ? 1 : 0;
        default: return 1;
    }
}

/* =========================================================
   QUÉT 6 NÚT CHUNG
   - chỉ trả về 1 nút mỗi lần nhấn
   - phải nhả nút rồi mới nhận lần tiếp theo
   ========================================================= */
static uint8_t scan_button_event(void)
{
    static uint8_t raw_last = 0;
    static uint8_t stable_state = 0;
    static uint8_t debounce_cnt = 0;
    uint8_t raw_now = 0;

    if (button_raw_read(1) == 0) raw_now = 1;
    else if (button_raw_read(2) == 0) raw_now = 2;
    else if (button_raw_read(3) == 0) raw_now = 3;
    else if (button_raw_read(4) == 0) raw_now = 4;
    else if (button_raw_read(5) == 0) raw_now = 5;
    else if (button_raw_read(6) == 0) raw_now = 6;
    else raw_now = 0;

    if (raw_now != raw_last) {
        raw_last = raw_now;
        debounce_cnt = 0;
        return 0;
    }

    if (debounce_cnt < 4) {
        debounce_cnt++;
        return 0;
    }

    if (stable_state != raw_now) {
        stable_state = raw_now;
        if (stable_state != 0) {
            return stable_state;
        }
    }

    return 0;
}

/* =========================================================
   SENSOR
   ========================================================= */
static void Sensor_Init_One(void)
{
    I2C_Peripheral_Init(I2C1);
    uart_send("I2C1 INIT DONE\r\n");

    tcs3272_init(I2C1);
    delay_simple(200000);
    uart_send("SENSOR 1 INIT DONE\r\n");
}

static void read_sensor_1(ColorData *d)
{
    getRGB(I2C1, &d->r, &d->g, &d->b, &d->c);
}

/* =========================================================
   COLOR SAVE / DETECT
   ========================================================= */
static ColorData saved_colors[5];
static uint8_t   saved_valid[5] = {0, 0, 0, 0, 0};

static uint8_t capture_active = 0;
static uint8_t capture_slot   = 0xFF;   /* 0..4 */

static uint32_t cap_sum_r = 0;
static uint32_t cap_sum_g = 0;
static uint32_t cap_sum_b = 0;
static uint32_t cap_sum_c = 0;
static uint16_t cap_count = 0;

static uint8_t detect_mode = 0;

static uint8_t has_any_saved_color(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (saved_valid[i]) return 1;
    }
    return 0;
}

static void reset_capture_buffer(void)
{
    cap_sum_r = 0;
    cap_sum_g = 0;
    cap_sum_b = 0;
    cap_sum_c = 0;
    cap_count = 0;
}

static void capture_add_one_sample(void)
{
    ColorData d;
    read_sensor_1(&d);

    cap_sum_r += (uint32_t)d.r;
    cap_sum_g += (uint32_t)d.g;
    cap_sum_b += (uint32_t)d.b;
    cap_sum_c += (uint32_t)d.c;
    cap_count++;
}

static void detect_stop(void)
{
    detect_mode = 0;
    LED2_Off();
}

static void capture_start(uint8_t slot)
{
    char st[64];

    if (detect_mode) {
        detect_stop();
    }

    capture_active = 1;
    capture_slot   = slot;
    reset_capture_buffer();

    capture_add_one_sample();   /* mẫu đầu tiên ngay lúc bắt đầu */

    LED1_On();

    sprintf(st, "BAT DAU LAY MAU %d\r\n", (int)(slot + 1));
    uart_send(st);
}

static void capture_stop_and_save(void)
{
    char st[128];

    capture_add_one_sample();   /* mẫu cuối ngay lúc dừng */

    if (cap_count == 0) {
        uart_send("KHONG CO DU LIEU DE LUU\r\n");
        capture_active = 0;
        capture_slot   = 0xFF;
        LED1_Off();
        return;
    }

    saved_colors[capture_slot].r = (int)(cap_sum_r / cap_count);
    saved_colors[capture_slot].g = (int)(cap_sum_g / cap_count);
    saved_colors[capture_slot].b = (int)(cap_sum_b / cap_count);
    saved_colors[capture_slot].c = (uint16_t)(cap_sum_c / cap_count);

    saved_valid[capture_slot] = 1;

    sprintf(st,
            "LUU MAU %d => R=%d G=%d B=%d C=%u | N=%u\r\n",
            (int)(capture_slot + 1),
            saved_colors[capture_slot].r,
            saved_colors[capture_slot].g,
            saved_colors[capture_slot].b,
            saved_colors[capture_slot].c,
            (unsigned int)cap_count);
    uart_send(st);

    capture_active = 0;
    capture_slot   = 0xFF;
    LED1_Off();
}

/* =========================================================
   Nhận diện theo tỷ lệ RGB chuẩn hóa
   ========================================================= */
static uint32_t color_distance_norm(const ColorData *a, const ColorData *b)
{
    int32_t sumA, sumB;
    int32_t ar, ag, ab;
    int32_t br, bg, bb;
    int32_t dr, dg, db;

    sumA = a->r + a->g + a->b;
    sumB = b->r + b->g + b->b;

    if (sumA <= 0 || sumB <= 0) {
        return 0xFFFFFFFFUL;
    }

    ar = (int32_t)((a->r * 1000L) / sumA);
    ag = (int32_t)((a->g * 1000L) / sumA);
    ab = (int32_t)((a->b * 1000L) / sumA);

    br = (int32_t)((b->r * 1000L) / sumB);
    bg = (int32_t)((b->g * 1000L) / sumB);
    bb = (int32_t)((b->b * 1000L) / sumB);

    dr = ar - br;
    dg = ag - bg;
    db = ab - bb;

    return (uint32_t)(dr * dr + dg * dg + db * db);
}

static int8_t classify_current_color(ColorData *current, uint32_t *best_dist)
{
    uint8_t i;
    int8_t best_idx = -1;
    uint32_t min_dist = 0xFFFFFFFFUL;

    for (i = 0; i < 5; i++) {
        uint32_t d;

        if (!saved_valid[i]) continue;

        d = color_distance_norm(current, &saved_colors[i]);
        if (d < min_dist) {
            min_dist = d;
            best_idx = (int8_t)i;
        }
    }

    if (best_dist != 0) {
        *best_dist = min_dist;
    }

    return best_idx;
}

static void detect_start(void)
{
    if (!has_any_saved_color()) {
        uart_send("0\r\n");
        return;
    }

    detect_mode = 1;
    LED2_On();
}

static void process_detect_step(void)
{
    static int8_t last_idx = -2;
    ColorData cur;
    uint32_t dist;
    int8_t idx;

    read_sensor_1(&cur);
    idx = classify_current_color(&cur, &dist);

    if (idx != last_idx) {
        last_idx = idx;

        if (idx >= 0) {
            switch (idx + 1) {
                case 1: uart_send("1\r\n"); break;
                case 2: uart_send("2\r\n"); break;
                case 3: uart_send("3\r\n"); break;
                case 4: uart_send("4\r\n"); break;
                case 5: uart_send("5\r\n"); break;
                default: uart_send("0\r\n"); break;
            }
        } else {
            uart_send("0\r\n");
        }
    }
}

/* =========================================================
   NHỊP LẤY MẪU / NHẬN DIỆN
   ========================================================= */
static void capture_process_step(void)
{
    static uint8_t div = 0;

    if (!capture_active) return;
    if (cap_count >= 40) return;

    div++;
    if (div < 8) return;
    div = 0;

    capture_add_one_sample();
}

static void detect_process_step(void)
{
    static uint8_t div = 0;

    if (!detect_mode || capture_active) return;

    div++;
    if (div < 2) return;
    div = 0;

    process_detect_step();
}

/* =========================================================
   MAIN
   ========================================================= */
int main(void)
{
    uint8_t key;

    Usart_Int(115200);
    uart_send("\r\nCOLOR SAVE + DETECT FAST FIX START\r\n");
    print_reset_flags();

    GPIO_Init_All();
    uart_send("GPIO INIT DONE\r\n");

    Sensor_Init_One();
    uart_send("READY\r\n");

    while (1)
    {
        key = scan_button_event();

        if (key != 0) {
            if (capture_active) {
                /* đang lấy mẫu thì chỉ nhận đúng nút hiện tại để lưu */
                if ((key >= 1) && (key <= 5) && ((key - 1) == capture_slot)) {
                    capture_stop_and_save();
                }
                else if (key == 6) {
                    uart_send("DANG LAY MAU\r\n");
                }
                /* nút khác bỏ qua */
            } else {
                switch (key) {
                    case 1: capture_start(0); break;
                    case 2: capture_start(1); break;
                    case 3: capture_start(2); break;
                    case 4: capture_start(3); break;
                    case 5: capture_start(4); break;
                    case 6:
                        if (!detect_mode) detect_start();
                        else detect_stop();
                        break;
                    default:
                        break;
                }
            }
        }

        capture_process_step();
        detect_process_step();

        delay_simple(300);
    }
}