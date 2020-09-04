#ifndef _ADC_H_
#define _ADC_H_
#include <stdint.h>
#include "inc/stm8s_adc1.h"

void adc_init();
void adc_timer();
/* Returns either v_load or v_sense depending on if v_sense is connected. */
uint16_t adc_get_voltage();
extern uint16_t temperature;
extern uint16_t v_12V;
extern uint16_t v_load;
extern uint16_t v_sense;
extern uint16_t raw_adc_vs;
extern uint16_t raw_adc_v;
#endif
