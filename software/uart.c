#include "uart.h"
#include "config.h"
#include "inc/stm8s_uart2.h"
#include "inc/stm8s_itc.h"
#include <stdio.h>
#include "adc.h"
#include "load.h"
#include "ui.h"

uart_buffer_t guart_buffer;

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

void uart_timer()
{
	static uint16_t timer = 0;
	static uint8_t cnt = 0;
	timer++;
	//Output one item each systick, but only start output with F_LOG
	if (cnt || (timer == F_SYSTICK/F_LOG)) {
		timer = 0;
		cnt++;
		if (cnt == 1) {
			char status = ' ';
			if (load_active) {
				status = load_regulated?'A':'U';
			}
			printf("%c %d ", status, error);
		} else if (cnt == 2) {
			printf("T %3u ", temperature);
		} else if (cnt == 3) {
			printf("Vi %5u ", v_12V);
		} else if (cnt == 4) {
			printf("Vl %5u ", v_load);
		} else if (cnt == 5) {
			printf("Vs %5u ", v_sense);
		} else if (cnt == 6) {
			printf("I %5u ", actual_current_setpoint);
		} else if (cnt == 7) {
			printf("mWs %10lu ", mWatt_seconds);
		} else if (cnt == 8) {
			printf("mAs %10lu ", mAmpere_seconds);
		} else {
			printf("\r\n");
			cnt = 0;
		}
	}
}

void uart_rx_irq() __interrupt(ITC_IRQ_UART2_RX)
{
	//uint8_t tmp = UART2->DR;
	while (UART2->SR & (uint8_t)UART2_FLAG_RXNE) {
		UART_BUFFER_WR(guart_buffer, UART2->DR);
	}

	//TODO: Calibration mode


}
