#include "uart.h"
#include "config.h"
#include "inc/stm8s_uart2.h"
#include "inc/stm8s_itc.h"
#include <stdio.h>
#include "adc.h"
#include "load.h"
#include "ui.h"

void uart_init()
{
    uint16_t uart_div =  (F_CPU + BAUDR/2) / BAUDR;
    UART2->BRR2 = (uart_div >> 12) | (uart_div & 0x0f);
    UART2->BRR1 = (uart_div >> 4);
    UART2->CR2 = UART2_CR2_TEN | UART2_CR2_REN | UART2_CR2_RIEN;
}

int putchar(int c)
{
    UART2->DR = (char) c;
    while (!(UART2->SR & (uint8_t)UART2_FLAG_TXE));
    return c;
}

static uint8_t cnt = 0;
void uart_timer()
{
    static uint16_t timer = 0;
    timer++;
    //Output one item each systick, but only start output with F_LOG
    if (timer == F_SYSTICK/F_LOG) {
        timer = 0;
        cnt = 1;
	    
    }
}

typedef enum {
    STATE_IDLE,
    STATE_COMMAND,
    STATE_DIGITS,
    STATE_WAITING_FOR_EXECUTION,
    STATE_UNINITIALIZED,
} uart_state_t;

static uart_state_t state = STATE_UNINITIALIZED;
static uint8_t cmd;
static uint16_t param;
static uint8_t error_code = 0;

static inline void set_error(uint8_t code)
{
    error_code = code;
    error = ERROR_COMMAND;
}

void uart_handler()
{
    if (cnt) {
        if (cnt == 1) {
            char status = 'D';
            if (load_active) {
                status = load_regulated?'A':'U';
            }
            printf("VAL:%c %d ", status, error);
        } else if (cnt == 2) {
            printf("| T %3u ", temperature);
        } else if (cnt == 3) {
            printf("| Vi %5u ", v_12V);
        } else if (cnt == 4) {
            printf("| Vl %5u ", v_load);
        } else if (cnt == 5) {
            printf("| Vs %5u ", v_sense);
        } else if (cnt == 6) {
            printf("| I %5u ", actual_current_setpoint);
        } else if (cnt == 7) {
            printf("| mWs %10lu ", mWatt_seconds);
        } else if (cnt == 8) {
            printf("| mAs %10lu ", mAmpere_seconds);
		} else if (cnt == 9) {
            printf("| RawVs  %8u ", raw_adc_vs);
        } else if (cnt == 10) {
            printf("| RawV %8u ", raw_adc_v);
        } else if (cnt == 11) {
		    printf("| PWM %8u ", ((TIM1->CCR1H*255)+TIM1->CCR1L));
        } else {
            printf("\r\n");
            cnt = 0;
        }
        if (cnt) cnt++;
    } else if (error == ERROR_COMMAND) {
        if (error_code) {
            printf("ERR:%d %d %d\r\n", cmd, param, error_code);
            error_code = 0;
        }
    } else if (state == STATE_WAITING_FOR_EXECUTION) {
        printf("CMD:%c%d\r\n", cmd, param);

        switch (cmd) {
            case 'R': // Run
                if (!load_active) {
                    ui_activate_load(); //This is handled in the UI code because we want to show the run mode on the display as well.
					                    //Commands start with: define CMD_RESET '!' ie start load !R [enter] / stop load !S [enter]
                }
                break;
            case 'S': // Stop
                if (load_active) {
                    ui_disable_load();
                }
                break;
            case 'M': // Mode
                if (param < NUM_MODES) {
                    settings.mode = param;
                } else {
                    set_error(ERR_MODE_INVALID);
                }
                break;
            case 'c': // Setpoint CC
                if (param >= CUR_MIN && param <= CUR_MAX) {
                    settings.setpoints[MODE_CC] = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'w': // Setpoint CW
                if (param >= POW_MIN && param <= POW_MAX) {
                    settings.setpoints[MODE_CW] = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'r': // Setpoint CR
                if (param >= R_MIN && param <= R_MAX) {
                    settings.setpoints[MODE_CR] = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'v': // Setpoint CV
                if (param >= VOLT_MIN && param <= VOLT_MAX) {
                    settings.setpoints[MODE_CV] = param;
                } else {
                    set_error(ERR_OUT_OF_RANGE);
                }
                break;
            case 'E': // Store settings
                settings_update();
                break;
            case 'e': // Load settings
                settings_init();
                break;
            default:
                set_error(ERR_INVALID_COMMAND);
        }
        cmd = 0;
        param = 0;
        state = STATE_IDLE;
    }
}

void uart_rx_irq() __interrupt(ITC_IRQ_UART2_RX)
{
    char c = UART2->DR;
    if (c == CMD_RESET) {
        state = STATE_IDLE;
        cmd = 0;
        param = 0;
        if (error == ERROR_COMMAND) {
            error = 0;
            error_code = ERR_NONE;
        }
    } else if (state == STATE_UNINITIALIZED) {
        // Ignore everything till the interface is initialized
    } else if (c == '\n' || c == '\r') {
        if (state != STATE_IDLE) {
            state = STATE_WAITING_FOR_EXECUTION;
        }
    } else if (state == STATE_IDLE) {
        cmd = c;
        param = 0;
        state = STATE_DIGITS;
    } else if (state == STATE_DIGITS) {
        if (c >= '0' && c <= '9') {
            param *= 10;
            param += c - '0';
        } else {
            set_error(ERR_NOT_A_DIGIT);
            error = ERROR_COMMAND;
            error_code = 1;
        }
    } else {
        set_error(ERR_SHOULD_NOT_HAPPEN);
    }
    //TODO: Calibration mode
}
