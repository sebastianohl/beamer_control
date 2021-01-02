#ifndef BEAMER_H_
#define BEAMER_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "homie.h"
#include "uart.h"

struct beamer_state_s
{
    bool state;
    TickType_t last_change;
    SemaphoreHandle_t mutex;
};
typedef struct beamer_state_s beamer_state_t;


extern beamer_state_t beamer_state;
extern uart_handle_t uart;

void update_power(struct homie_handle_s *handle, int node, int property);
void write_power(struct homie_handle_s *handle, int node, int property,
                 const char *data, int data_len);
void update_source(struct homie_handle_s *handle, int node, int property);
void write_source(struct homie_handle_s *handle, int node, int property,
                  const char *data, int data_len);

#endif