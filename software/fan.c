#include "fan.h"
#include "load.h"
#include "adc.h"
#include "config.h"
#include <stdio.h>
#include "inc/stm8s_tim3.h"

static bool fan_on = 0;

void fan_init()
{
    TIM3->ARRH = FAN_PWM_FREQ >> 8;
    TIM3->ARRL = FAN_PWM_FREQ & 0xff;
    TIM3->CCMR2  = TIM3_OCMODE_PWM1 | TIM3_CCMR_OCxPE;
    TIM3->CCER1  = TIM3_CCER1_CC2E;
    TIM3->PSCR   = TIM3_PRESCALER_1; // Prescaler of 1 gives 16 MHz / 2^16 = 244 Hz
    TIM3->CR1   |= TIM3_CR1_CEN | TIM3_CR1_ARPE;
    TIM3->CCR2H  = 0;
    TIM3->CCR2L  = 0;
}

static void fan_set_pwm()
{
    if (temperature > FAN_TEMPERATURE_FULL) {
        fan_on = 1;
        // Over temperature protection
        if (temperature > FAN_TEMPERATURE_OTP_LIMIT) {
            error = ERROR_OVERTEMPERATURE;
        }
        TIM3->CCR2H  = FAN_SPEED_FULL >> 8;
        TIM3->CCR2L  = FAN_SPEED_FULL & 0xff;
    } else if (temperature > FAN_TEMPERATURE_LOW) {
        fan_on = 1;
        #define INT_ROUND_DIV(x,y) (((x)+((y)/2))/(y))
        uint16_t fan_speed = FAN_SPEED_LOW +
                             (temperature - FAN_TEMPERATURE_LOW) *
                             INT_ROUND_DIV(FAN_SPEED_FULL - FAN_SPEED_LOW,
                                 FAN_TEMPERATURE_FULL - FAN_TEMPERATURE_LOW);
        TIM3->CCR2H = fan_speed >> 8;
        TIM3->CCR2L = fan_speed & 0xff;
    } else if (fan_on && load_active && (temperature > FAN_TEMPERATURE_LOW - FAN_ON_OFF_HYSTERESIS)) {
        TIM3->CCR2H = FAN_SPEED_LOW >> 8;
        TIM3->CCR2L = FAN_SPEED_LOW & 0xff;
    } else {
        fan_on = 0;
        #if FAN_ALWAYS_ON
        if (load_active) {
            fan_on = 1;
            TIM3->CCR2H  = FAN_SPEED_LOW >> 8;
            TIM3->CCR2L  = FAN_SPEED_LOW & 0xff;
        } else {
        #else
        {
        #endif
            TIM3->CCR2H  = 0;
            TIM3->CCR2L  = 0;
        }
    }
}

void fan_timer()
{
    static uint16_t timer = 0;
    timer++;
    if (timer == F_SYSTICK/F_FAN) {
        timer = 0;
        fan_set_pwm();
    }
}
