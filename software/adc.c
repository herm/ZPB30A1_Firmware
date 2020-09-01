#include "adc.h"
#include "load.h"
#include "config.h"
#include "inc/stm8s_itc.h"
#include <stdio.h>

static uint16_t adc_sum[ADC_NUM_CHANNELS];
uint16_t adc_values[ADC_NUM_CHANNELS];
static uint8_t adc_count = 0;
uint16_t temperature;
uint16_t v_12V;
uint16_t v_load;
uint16_t v_sense;
uint16_t raw_adc_v;
uint16_t raw_adc_vs;

void adc_init()
{
    /* 1 conversion takes 14 ADC cycles. 4 channels are processed in scan mode.
       => 56 ADC cycles per scan.
    divider    sample rate      cpu cycles between irqs
    3            95 kHz         168
    4            71 kHz         224
    6            48 kHz         336
    8            36 kHz         448
    IRQ overhead is 18 cycles (PM0044, page 14).
    */
    adc_count = 0;
    ADC1->CR1 = ADC1_PRESSEL_FCPU_D8 | ADC1_CONVERSIONMODE_SINGLE;
    ADC1->CR2 = ADC1_ALIGN_RIGHT | ADC1_CR2_SCAN;
    ADC1->TDRH = 0;
    ADC1->TDRL = (1<<ADC_CH_TEMPERATURE) | (1<<ADC_CH_SENSE) | (1<<ADC_CH_SENSE) | (1<<ADC_CH_12V);
    ADC1->CSR = (ADC_NUM_CHANNELS - 1) | ADC1_IT_EOCIE;
    ADC1->CR1 |= ADC1_CR1_ADON; //Wake up
    ADC1->CR1 |= ADC1_CR1_ADON; //Start converting
}

void adc_irq() __interrupt(ITC_IRQ_ADC1)
{
    //TODO: Optimize this function in asm
    // with 32 bit buffers and double buffering this function takes
    //about >200 cycles.

    //This solution takes ~16 cycles/addition
    // uint16_t *r = (uint16_t *)&ADC1->DB0RH; => Internal Error (SDCC)
    uint8_t *p = &ADC1->DB0RH;
    uint16_t *r = p;
    uint16_t *w = &adc_sum[0];
    *w++ += *r++;
    *w++ += *r++;
    *w++ += *r++;
    *w++ += *r++;
    //Clear IRQ flag
    ADC1->CSR = (ADC_NUM_CHANNELS - 1) | ADC1_IT_EOCIE;

    if (++adc_count < ADC_SAMPLES_PER_MEASUREMENT) {
        ADC1->CR1 |= ADC1_CR1_ADON;
    }
}

void adc_update()
{
    temperature = (ADC_CAL_TEMP_T - adc_values[ADC_CH_TEMPERATURE]) / ADC_CAL_TEMP_M;
    v_12V = (uint32_t)adc_values[ADC_CH_12V] * ADC_CAL_12V >> 16;

    int32_t tmps;
    uint32_t tmpu;

    tmps = adc_values[ADC_CH_LOAD];
	raw_adc_v = tmps;
    tmps -= ADC_CAL_LOAD_T;
    tmpu = tmps > 0 ? tmps : 0;
    v_load = tmpu * ADC_CAL_LOAD_M >> 16;

    tmps = adc_values[ADC_CH_SENSE];
	raw_adc_vs = tmps;
    tmps -= ADC_CAL_SENSE_T;
    tmpu = tmps > 0 ? tmps : 0;
    v_sense = tmpu * ADC_CAL_SENSE_M >> 16;

    if (v_12V < ADC_12V_MIN) {
        error = ERROR_POWER_SUPPLY;
    }

    if ((adc_values[ADC_CH_LOAD]  < ADC_LOAD_MIN) ||
        (adc_values[ADC_CH_SENSE] < ADC_SENSE_MIN)) {
        error = ERROR_POLARITY;
    }

    if ((v_load > ADC_INPUT_MAX) || (v_sense > ADC_INPUT_MAX)) {
        error = ERROR_OVERVOLTAGE;
    }
}

void adc_timer()
{
    if (adc_count != ADC_SAMPLES_PER_MEASUREMENT) {
        error = ERROR_INTERNAL;
    }
    for (uint8_t i=0; i<ADC_NUM_CHANNELS; i++) {
        // ADC is 10 bits => multiplying by 64 result in a left aligned 16 bit measurement.
        adc_values[i] = adc_sum[i] * (64 / ADC_SAMPLES_PER_MEASUREMENT);
        adc_sum[i] = 0;
    }
    //Start next measurement
    adc_count = 0;
    ADC1->CR1 |= ADC1_CR1_ADON;
    adc_update();
}

uint16_t adc_get_voltage()
{
  if (v_sense > v_load) return v_sense;
  return v_load;
}
