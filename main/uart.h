/*
 * uart.h
 *
 *  Created on: Jan 24, 2020
 *      Author: ohli
 */

#ifndef MAIN_UART_H_
#define MAIN_UART_H_

#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

#define RX_UART_NUM UART_NUM_0
#define TX_UART_NUM UART_NUM_1

#define UART_BUF_SIZE (1024)
#define UART_RD_BUF_SIZE (UART_BUF_SIZE)

struct uart_handle_s
{
    uart_config_t configRx;
    uart_config_t configTx;
    QueueHandle_t queue;
    TickType_t wait_ticks;
    uint8_t buffer[UART_BUF_SIZE];
    size_t buffer_fill;
};
typedef struct uart_handle_s uart_handle_t;

void uart_write(uart_handle_t *handle, const char *buffer, size_t length);
void uart_get_buffer(uart_handle_t *handle, char *buffer, size_t *length);

void uart_init(uart_handle_t *handle);
void uart_cycle(uart_handle_t *handle);


#endif /* MAIN_UART_H_ */
