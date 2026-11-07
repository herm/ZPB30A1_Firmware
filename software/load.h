#ifndef _LOAD_H_
#define _LOAD_H_
#include <stdbool.h>
#include <stdint.h>
#include "settings.h"

//NOTE: Keep this enum in sync with the messages in ui_error_handler()!
typedef enum {
    ERROR_NONE,
    ERROR_POLARITY, // input polarity reversed
    ERROR_OVERVOLTAGE, // input voltage to high
    ERROR_OVERLOAD, // input can't supply the required current
    ERROR_MAX_POWER, // power > POW_ABS_MAX and max_power_action == MAX_P_OFF
    ERROR_OVERTEMPERATURE, // heat sink temperature to high
    ERROR_POWER_SUPPLY, // supply voltage to low
    ERROR_TIMER_OVERFLOW, // timer tick lost
    ERROR_INTERNAL, // other internal error
    ERROR_COMMAND, // invalid command received
} error_t;
extern error_t error;

typedef enum {
    DISABLE_USER,
    DISABLE_ERROR,
    DISABLE_CUTOFF,
} disable_reason_t;

extern bool load_active;
extern bool load_regulated;
extern uint8_t load_disable_reason;

/* Current setpoint after all constraints are taken into account. */
extern uint16_t current_setpoint;
/* Transient calibration override, set by the calibration wizard (ui.c):
   while cal_active is set, load_update() regulates cal_setpoint instead of
   the user's setpoint and enforces the current limit as in CC mode. The
   override is never written to settings_t. */
extern bool cal_active;

/* Control law actually in effect: a calibration run always uses CC,
   regardless of the user's stored mode. settings.mode itself is never
   modified, so nothing is persisted when the wizard pops. */
static inline sink_mode_t load_control_mode(void)
{
    return cal_active ? MODE_CC : settings.mode;
}
extern uint16_t cal_setpoint;
/* Runtime current calibration, set by calibration_init(). Definition:
   PWM = (current * cal_slope - cal_offset) / 2^16. */
extern uint32_t cal_slope;
extern int32_t cal_offset;
extern uint32_t mAmpere_seconds;
extern uint32_t mWatt_seconds;


void load_init(void);
void load_timer(void);
void load_enable(void);
void load_disable(uint8_t reason);

#endif
