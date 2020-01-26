/*
 * uart.c
 *
 *  Created on: Jan 24, 2020
 *      Author: ohli
 */

#include "uart.h"
#include <strings.h>
#include <string.h>

void uart_cycle(uart_handle_t *handle)
{
    uart_event_t event;

	// Waiting for UART event.
	if (xQueueReceive(handle->queue, (void *)&event, handle->wait_ticks)) {
		switch (event.type) {
			// Event of UART receving data
			// We'd better handler data event fast, there would be much more data events than
			// other types of events. If we take too much time on data event, the queue might be full.
			case UART_DATA:
				if (handle->buffer_fill + event.size > UART_BUF_SIZE)
				{
					bzero(handle->buffer, UART_RD_BUF_SIZE);
					handle->buffer_fill = 0;
				}
				uart_read_bytes(EX_UART_NUM, handle->buffer+handle->buffer_fill, event.size, handle->wait_ticks);
				handle->buffer_fill += event.size;
				break;

			// Event of HW FIFO overflow detected
			case UART_FIFO_OVF:
				// If fifo overflow happened, you should consider adding flow control for your application.
				// The ISR has already reset the rx FIFO,
				// As an example, we directly flush the rx buffer here in order to read more data.
				uart_flush_input(EX_UART_NUM);
				xQueueReset(handle->queue);
				break;

			// Event of UART ring buffer full
			case UART_BUFFER_FULL:
				// If buffer full happened, you should consider encreasing your buffer size
				// As an example, we directly flush the rx buffer here in order to read more data.
				uart_flush_input(EX_UART_NUM);
				xQueueReset(handle->queue);
				break;

			case UART_PARITY_ERR:
				break;

			// Event of UART frame error
			case UART_FRAME_ERR:
				break;

			// Others
			default:
				break;
		}
	}
}

void uart_write(uart_handle_t *handle, const char *buffer, size_t length)
{
	uart_write_bytes(EX_UART_NUM, buffer, length);
}

void uart_get_buffer(uart_handle_t *handle, char **buffer, size_t *length)
{
	memcpy(*buffer, handle->buffer, (*length > handle->buffer_fill)?handle->buffer_fill:*length);
	*length = (*length > handle->buffer_fill)?handle->buffer_fill:*length;
	handle->buffer_fill = 0;
}

void uart_init(uart_handle_t *handle)
{
    // Configure parameters of an UART driver,
    // communication pins and install the driver
    uart_param_config(EX_UART_NUM, &handle->config);

    // Install UART driver, and get the queue.
    uart_driver_install(EX_UART_NUM, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 100, &handle->queue, 0);
}
