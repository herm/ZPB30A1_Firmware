#include "tm1650.h"
#include "config.h"
#include "inc/stm8s_gpio.h"

#define PIN_I2C_CLK PINC_SCL
#define DIGIT_REG 0x68
#define BRIGHTNESS_REG 0x48

uint8_t digits[] = {
    0x68,
    0x6A,
    0x6C,
    0x6E
};

#define CHAR_OFFSET 48

uint8_t chars[] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F, // 9
    0x00, // :
    0x00, // ;
    0x00, // <
    0x40, // =
    0x00, // >
    0x00, // ?
    0x00, // @ as SPACE
    0x77, // A
    0x7C, // b
    0x39, // C
    0x5E, // d
    0x79, // E
    0x71, // F
    0x3D, // G
    0x74, // h
    0x30, // I
    0x1E, // J
    0x70, // K
    0x38, // L
    0x37, // m
    0x54, // n
    0x5C, // o
    0x73, // P
    0x67, // q
    0x50, // r
    0x2D, // S
    0x78, // t
    0x3E, // U
    0x1C, // v
    0x14, // w
    0x76, // x
    0x6E, // y
    0x1B, // Z
    0x40, // -
};

static void i2c_write(uint8_t data, uint8_t pin)
{
    uint8_t i;
    GPIO_DISPLAY->DDR |= pin;
    for (i = 7; i < 255; i--) {
        if (data & (1 << i)) {
            GPIO_DISPLAY->ODR |= pin;
        } else {
            GPIO_DISPLAY->ODR &= ~pin;
        }
        // Clock H/L
        GPIO_DISPLAY->ODR |= PIN_I2C_CLK;
        GPIO_DISPLAY->ODR &= ~PIN_I2C_CLK;
    }
    // Avoid driving a 1 while receiving a 0.
    GPIO_DISPLAY->ODR &= ~pin;
    // We don't need the ACK, so just do a single clock H/L without reading
    GPIO_DISPLAY->ODR |= PIN_I2C_CLK;
    GPIO_DISPLAY->ODR &= ~PIN_I2C_CLK;
}

void disp_write(uint8_t addr, uint8_t data, uint8_t pin)
{
    pin = (pin == DP_TOP)?DP_TOP_PIN:DP_BOT_PIN;
    // Start sequence
    GPIO_DISPLAY->ODR |= pin;         // SDA HIGH
    GPIO_DISPLAY->ODR |= PIN_I2C_CLK;  // SCL HIGH
    GPIO_DISPLAY->ODR &= ~pin;        // SDA LOW
    GPIO_DISPLAY->ODR &= ~PIN_I2C_CLK; // SCL LOW

    i2c_write(addr, pin);
    i2c_write(data, pin);

    // Stop sequence
    GPIO_DISPLAY->ODR &= ~pin;        // SDA LOW
    GPIO_DISPLAY->ODR |= PIN_I2C_CLK;  // SCL HIGH
    GPIO_DISPLAY->ODR |= pin;         // SDA HIGH
}

void disp_char(uint8_t position, uint8_t c, uint8_t dot, uint8_t pin)
{
    if (c == '.') {
        c = ' ';
        dot = 1;
    }
    if (c == ' ') {
        c = '@';
    }
    disp_write(DIGIT_REG + 2*position, chars[c - CHAR_OFFSET] | (dot?0x80:0), pin);
}

void disp_brightness(uint8_t brightness, uint8_t pin)
{
    if (!brightness) disp_write(BRIGHTNESS_REG, 0x00, pin); // OFF
    else disp_write(BRIGHTNESS_REG, ((brightness & 7) << 4) | 0x01, pin); // 8 brightness levels
}

void disp_leds(uint8_t leds)
{
    disp_write(DIGIT_REG + 6, leds, DP_BOT);
}
