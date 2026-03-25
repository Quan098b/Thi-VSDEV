# STM32F103 - Đọc cảm biến màu TCS34725 và điều khiển động cơ

## Mô tả
Project dùng STM32F103C8 với framework CMSIS/StdPeriph để:
- Giao tiếp 2 cảm biến màu TCS34725 qua I2C1 và I2C2
- Gửi log debug qua UART1
- Điều khiển động cơ theo màu đọc được

## Cấu hình chính
- Board: `genericSTM32F103C8`
- Framework: `cmsis`
- Upload: `stlink`
- UART debug: `USART1`
  - TX = `PA9`
  - RX = `PA10`
  - Baudrate = `115200`

## Sơ đồ kết nối UART debug
- CH340 TX -> STM32 PA10
- CH340 RX -> STM32 PA9
- GND CH340 -> GND STM32

## Cảm biến màu
- I2C1: PB6 (SCL), PB7 (SDA)
- I2C2: PB10 (SCL), PB11 (SDA)

## Chức năng mã nguồn
- `src/main.c`: khởi tạo hệ thống, đọc 2 cảm biến, phân loại màu, điều khiển xe
- `src/usart.c`: cấu hình UART1 và hàm gửi dữ liệu serial
- `src/i2c.c`: cấu hình I2C1/I2C2 và hàm đọc/ghi I2C
- `src/tcs34725.c`: driver cảm biến màu TCS34725
- `src/motor_control.c`: điều khiển hướng chạy và xung tốc độ động cơ

## Ghi chú
Nếu chương trình bị treo khi khởi tạo cảm biến, cần kiểm tra:
- Dây SDA/SCL
- Điện trở pull-up I2C
- Nguồn cấp cảm biến
- Địa chỉ / phản hồi của cảm biến
