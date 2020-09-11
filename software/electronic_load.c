#include "config.h"
#include "uart.h"
#include "utils.h"
#include "ui.h"
#include "stdio.h"
#include "tm1650.h"
#include "eeprom.h"
#include "timer.h"
#include "settings.h"
#include "load.h"
#include "fan.h"
#include "adc.h"
#include "beeper.h"
#include "remote.h"
#include "inc/stm8s_clk.h"
#include "inc/stm8s_exti.h"
#include "inc/stm8s_itc.h"

void clock_init()
{
    CLK->CKDIVR = CLK_PRESCALER_HSIDIV1;
    CLK->ICKR |= CLK_ICKR_LSIEN; // Low speed RC for beeper
    CLK->ECKR |= CLK_ECKR_HSEEN; // Crystal oscillator
    while ((CLK->ECKR & CLK_FLAG_HSERDY) == 0); // wait for crystal startup
    CLK->SWR = CLK_SOURCE_HSE;
    //CLK->SWCR = CLK_SWCR_SWEN;*/
    CLK->CKDIVR = 0;
}

void gpio_init()
{
    GPIOB->CR1 = PINB_ENC_A | PINB_ENC_B; // Pullup
    GPIOB->CR2 = PINB_ENC_A | PINB_ENC_B; // Irq

    GPIOC->DDR = PINC_I_SET | PINC_SCL | PINC_SDA1 | PINC_SDA2;
    GPIOC->CR1 = PINC_I_SET | PINC_SCL | // push pull
                 PINC_OL_DETECT | PINC_ENC_P | PINC_RUN_P; // pullup
    GPIOC->CR2 = PINC_OL_DETECT | PINC_ENC_P | PINC_RUN_P |  // irq
                 PINC_SCL | PINC_SDA1 | PINC_SDA2; // 10 MHz


    GPIOD->DDR = PIND_FAN | PIND_BUS_F | PIND_BEEPER | PIND_TX;
    GPIOD->CR1 = PIND_FAN | PIND_BUS_F | PIND_BEEPER | PIND_TX | // push pull
                 PIND_V_OK | PIND_TLI | PIND_RX; // pullup
    GPIOD->CR2 = PIND_V_OK | PIND_TLI; // irq

    GPIOE->ODR = PINE_ENABLE; // load off
    GPIOE->DDR = PINE_ENABLE; // pullup

    EXTI->CR2    = EXTI_SENSITIVITY_FALL_ONLY; // TLI
    EXTI->CR1   |= EXTI_SENSITIVITY_RISE_FALL << 2; // GPIOB
    EXTI->CR1   |= EXTI_SENSITIVITY_RISE_FALL << 4; // GPIOC
    EXTI->CR1   |= EXTI_SENSITIVITY_RISE_FALL << 6; // GPIOD
}



void main(void) {

    clock_init();
    gpio_init();
    adc_init();
    uart_init();
  	remote_init();
    systick_init();
    load_init();
    beeper_init();
    fan_init();
    settings_init();

    __asm__ ("rim");

    // Power on beep
    beeper_on();
    delay10ms(10);
    beeper_off();

    // Init UI after power on delay to avoid spurious button events.
    ui_init();

    systick_flag = 0; // Clear any overflows up to this point
    while (1) {
        if (systick_flag & SYSTICK_OVERFLOW)
        {
            error = ERROR_TIMER_OVERFLOW;
            systick_flag &= ~SYSTICK_OVERFLOW;
        }
        if (systick_flag & SYSTICK_COUNT) {
            adc_timer();
            fan_timer();
            ui_timer();
            load_timer();
            uart_timer();
            remote_timer();
            systick_flag &= ~SYSTICK_COUNT;
        }
    }

}

//Voltage OK interrupt
void GPIOD_Handler() __interrupt(ITC_IRQ_PORTD) {
}

/* If you have multiple source files in your project, interrupt service routines
 can be present in any of them, but a prototype of the isr MUST be present or
 included in the file that contains the function main. */
void ui_encoder_irq() __interrupt(ITC_IRQ_PORTB);
void ui_button_irq() __interrupt(ITC_IRQ_PORTC);
void uart_rx_irq() __interrupt(ITC_IRQ_UART2_RX);
void systick_irq() __interrupt(ITC_IRQ_TIM2_OVF);
void adc_irq() __interrupt(ITC_IRQ_ADC1);
