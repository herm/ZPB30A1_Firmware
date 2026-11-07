#ifndef _CONFIG_H
#define _CONFIG_H_

//F_xxx is in Hz
#define F_CPU 16000000L
#define F_SYSTICK 100

#define BAUDR 115200L

#define F_DISPLAY_BLINK 15
#define F_UI_SWITCH_DISPLAY 0.2
#define F_UI_UPDATE_DISPLAY 2

#define BRIGHTNESS_BRIGHT 4
#define BRIGHTNESS_DIM 2

/* F_POWER_CALC must be an integer 1 <= f <= F_SYSTICK and an
   integer divider of F_SYSTICK */
#define F_POWER_CALC 100
#define F_FAN 5
#define F_BEEP_ERROR 2
#define F_BEEP_CUTOFF 5
#define F_BEEP_KHZ 1 // 1, 2 or 4
#define F_LOG 5

#define VOLT_MIN 500 //mV
#define VOLT_MAX 30000 //mV
#define VOLT_DOT_OFFSET 3

#define CUR_MIN 200 //mA
#define CUR_MAX 10000 //mA
#define CUR_DOT_OFFSET 3

#define POW_MIN 1 //mW
#define POW_MAX 60000 //mW: maximum settable power
#define POW_DOT_OFFSET 3
#define POW_ABS_MAX 65000 //mW: Current at which the load current is reduced

/* Usable range:
  Rmin = 1V / 10A = 0.1 Ohm
  Rmax = 30V / 0.2A = 150 Ohm
*/
#define R_MIN 10 //10*mOhm
#define R_MAX 15000 //10*mOhm
#define R_DOT_OFFSET 2

#define AS_DOT_OFFSET 3
#define WS_DOT_OFFSET 3

/* Definition of slope and offset for the current path:
   PWM = (current * slope - offset) / 2^16
   `slope` is the Q16 slope in PWM LSB per mA; `offset` is the Q16 term that
   is subtracted from it, i.e. the negated Q16 y-intercept.

   The values below were measured on the author's unit. On a different unit
   the load current can be far off the setpoint. Use the on-device wizard
   (... -> CAL -> CURR) to store a per-unit calibration in the EEPROM; the
   complete procedure is in calibration.md. These constants are the
   compile-time defaults and the fallback whenever the EEPROM block is
   missing or invalid.
*/
#define LOAD_CAL_OFFSET 8821987L
#define LOAD_CAL_SLOPE 350445L

/* Runtime current calibration (calibration.c, calibration wizard in ui.c):
   Default measurement points (mA) and the wizard's timings. F_SYSTICK is
   100 Hz, so the load settles for 500 ms before a measurement may be taken
   and the wizard aborts after 60 s without user input. */
#define CAL_CURR_LO_DEFAULT 500
#define CAL_CURR_HI_DEFAULT 3000
#define CAL_SETTLE_TICKS 50
#define CAL_TIMEOUT_TICKS 6000

/* Maximum: 64 */
#define ADC_SAMPLES_PER_MEASUREMENT 64
/* Consecutive systicks without a complete ADC set before an internal
   error is raised (3 ticks = 30 ms at F_SYSTICK 100 Hz). */
#define ADC_INCOMPLETE_MAX 3
#define ADC_NUM_CHANNELS 4
#define ADC_CH_TEMPERATURE 0
#define ADC_CH_LOAD 1
#define ADC_CH_SENSE 2
#define ADC_CH_12V 3


/* Calibration data: hardware/temperature.ods
   These measurement channels are compile-time only in v1; see calibration.md
   ("Voltage and temperature (not user-calibratable in v1)"). */
#define ADC_CAL_TEMP_M 42
#define ADC_CAL_TEMP_T 64014
/* 12V measurement Voltage divider: 1/3
    V12V in mV = ADC * 5Vref * 3 / 2^16 * 1000mV/V = ADC * 15000 / 2^16
*/
#define ADC_CAL_12V 15080

/* Definition of t and m:
   result = (ADC - t) * m / 2^16
*/
#define ADC_CAL_LOAD_T 1246
#define ADC_CAL_LOAD_M 41430
#define ADC_CAL_SENSE_T 1326
#define ADC_CAL_SENSE_M 36546

/* ADC value at which reverse voltage protection is triggered. */
#define ADC_LOAD_MIN  100 // ADC counts
#define ADC_SENSE_MIN 100 // ADC counts
#define ADC_12V_MIN 10000 // mV
#define ADC_INPUT_MAX 35000 // mV

#define FAN_TEMPERATURE_OTP_LIMIT 850 // * 0.1°C
#define FAN_TEMPERATURE_FULL 750 // * 0.1°C
#define FAN_TEMPERATURE_LOW  400 // * 0.1°C
#define FAN_ON_OFF_HYSTERESIS 50 // * 0.1°C
#define FAN_ALWAYS_ON 0
#define FAN_PWM_MAX 0xfff
#define FAN_SPEED_LOW ((uint16_t)(FAN_PWM_MAX/20)) // PWM value
#define FAN_SPEED_FULL FAN_PWM_MAX // PWM value. max: 0xffff

// port B
#define PINB_ENC_A (1u<<5)
#define PINB_ENC_B (1u<<4)

//port C
#define PINC_I_SET (1u<<1)
#define PINC_OL_DETECT (1u<<2)
#define PINC_ENC_P (1u<<3)
#define PINC_RUN_P (1u<<4)
#define PINC_SCL (1u<<5)
#define PINC_SDA1 (1u<<6)
#define PINC_SDA2 (1u<<7)

// port D
#define PIND_FAN (1u<<0)
#define PIND_SWIM (1u<<1)
#define PIND_BUS_F (1u<<2)
#define PIND_V_OK (1u<<3)
#define PIND_BEEPER (1u<<4)
#define PIND_TX (1u<<5)
#define PIND_RX (1u<<6)
#define PIND_TLI (1u<<7)

// port E
#define PINE_ENABLE (1u<<5)


#define GPIO_DISPLAY GPIOC
#define DP_TOP_PIN PINC_SDA2
#define DP_BOT_PIN PINC_SDA1
#define LED_V       0x01
#define LED_AH      0x02
#define LED_WH      0x04
#define LED_A       0x08
#define LED_RUN     0x10
#define LED_DIGIT1  0x20
#define LED_DIGIT2  0x40
#endif
