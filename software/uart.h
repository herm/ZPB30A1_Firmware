#ifndef _UART_H_
#define _UART_H_
#include <stdint.h>

void uart_init();
void uart_timer();
void uart_handler();

#define CMD_RESET '!'

typedef enum {
    ERR_NONE,
    ERR_MODE_INVALID,
    ERR_OUT_OF_RANGE,
    ERR_NOT_A_DIGIT,
    ERR_SHOULD_NOT_HAPPEN, // = Internal logic error
    ERR_INVALID_COMMAND,
} error_codes_t;

#define UART_BUFFER_SIZE (128) //Must be a power of 2
#define UART_BUFFER_MASK (UART_BUFFER_SIZE - 1ul)

// Buffer read/write macros
#define UART_BUFFER_RESET(uart_buffer)      {uart_buffer.rdIdx = uart_buffer.wrIdx = 0;}
#define UART_BUFFER_WR(uart_buffer, dataIn) {uart_buffer.data[UART_BUFFER_MASK & uart_buffer.wrIdx++] = (dataIn);}
#define UART_BUFFER_RD(uart_buffer, dataOut){uart_buffer.rdIdx++; dataOut = uart_buffer.data[UART_BUFFER_MASK & (uart_buffer.rdIdx-1)];}
#define UART_BUFFER_EMPTY(uart_buffer)      (uart_buffer.rdIdx == uart_buffer.wrIdx)
#define UART_BUFFER_FULL(uart_buffer)       ((UART_BUFFER_MASK & uart_buffer.rdIdx) == (UART_BUFFER_MASK & (uart_buffer.wrIdx+1)))
#define UART_BUFFER_COUNT(uart_buffer)      (UART_BUFFER_MASK & (uart_buffer.wrIdx - uart_buffer.rdIdx))

/* buffer type */
typedef struct {
    uint32_t size;
    uint32_t wrIdx;
    uint32_t rdIdx;
    uint8_t data[UART_BUFFER_SIZE];
} uart_buffer_t;
extern uart_buffer_t guart_buffer;

#endif
