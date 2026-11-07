#ifndef _CALIBRATION_H_
#define _CALIBRATION_H_
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h> /* offsetof */

/* The calibration block is stored in the data EEPROM, well behind the
   settings. Keeping it separate from settings_t means the settings can grow
   or be reset without touching the calibration. Layout of the conservative
   128 byte pool on this chip (settings_t: 16 bytes + 1 checksum byte in this
   build, verified against the map):
     0x00 .. 0x10  settings (17 bytes used)
     0x40 .. 0x67  calibration block (40 bytes)
     0x68 .. 0x7f  free
*/
#define CAL_EEPROM_OFFSET   0x40
#define CAL_EEPROM_SIZE     40

/* The magic byte is written last: a write that is interrupted (e.g. by a
   power loss) leaves a block that fails a check and is replaced by the
   compile-time defaults at the next boot. */
#define CAL_MAGIC           0x5A
#define CAL_VERSION         1

/* Validity flag, one bit per channel. A channel's data is only used if its
   bit is set. Unknown bits are ignored, so a block written by newer firmware
   can still be read here. */
#define CAL_FLAG_CURRENT    0x01
#define CAL_FLAG_V_LOAD     0x02   /* reserved, always 0 in CAL_VERSION 1 */
#define CAL_FLAG_V_SENSE    0x04   /* reserved, always 0 in CAL_VERSION 1 */

/* Calibration data as stored in the EEPROM. The layout is fixed. Adding a
   voltage channel later only fills the reserved fields and sets their flag;
   that does not change CAL_VERSION. CAL_VERSION changes only when the layout
   itself changes.
   Note: curr_offset can legitimately be negative, it must stay signed.
*/
typedef struct {
    uint8_t  magic;          /* 0x00: CAL_MAGIC, written last */
    uint8_t  version;        /* 0x01: CAL_VERSION */
    uint8_t  flags;          /* 0x02: CAL_FLAG_* per channel */
    uint8_t  crc;            /* 0x03: CRC-8 over 0x01..0x27 except 0x03 */
    uint16_t curr_lo;        /* 0x04: lower calibration point, mA */
    uint16_t curr_hi;        /* 0x06: upper calibration point, mA */
    uint32_t curr_slope;     /* 0x08: 2^16 PWM LSB per mA, see config.h */
    int32_t  curr_offset;    /* 0x0c: 2^16 PWM LSB offset, signed */
    uint16_t vload_lo;       /* 0x10: reserved (mV) */
    uint16_t vload_hi;       /* 0x12: reserved (mV) */
    uint32_t vload_slope;    /* 0x14: reserved (ADC domain) */
    int32_t  vload_offset;   /* 0x18: reserved */
    uint16_t vsense_lo;      /* 0x1c: reserved (mV) */
    uint16_t vsense_hi;      /* 0x1e: reserved (mV) */
    uint32_t vsense_slope;   /* 0x20: reserved (ADC domain) */
    int32_t  vsense_offset;  /* 0x24: reserved */
} cal_block_t;               /* exactly 40 bytes, checked below */

/* The layout above is part of the EEPROM format, make sure it does not
   drift. Requires --std-sdcc11. */
_Static_assert(sizeof(cal_block_t) == 40, "cal_block_t must be 40 bytes");
_Static_assert(offsetof(cal_block_t, curr_lo) == 4,  "cal_block_t layout");
_Static_assert(offsetof(cal_block_t, curr_slope) == 8,  "cal_block_t layout");
_Static_assert(offsetof(cal_block_t, curr_offset) == 12, "cal_block_t layout");
_Static_assert(offsetof(cal_block_t, vsense_offset) == 36, "cal_block_t layout");

/* Result of the two-point fit. Shown as error reason in the wizard. */
typedef enum {
    CAL_ERR_NONE = 0,   /* points valid, fit succeeded */
    CAL_ERR_RANGE,      /* setpoint or measured value out of range */
    CAL_ERR_GAIN,       /* measured gain outside 0.5 .. 2.0 */
    CAL_ERR_SLOPE,      /* resulting slope outside the sanity range */
    CAL_ERR_OFFSET,     /* resulting offset outside the sanity range */
    CAL_ERR_PWM,        /* corrected constants cannot realize the points within the PWM range */
} cal_result_t;

/* Channel descriptors. The wizard (menu) selects a channel by its
   descriptor, so adding a channel later only needs a descriptor plus the
   storage managed by the block. */
typedef enum {
    CAL_KIND_CURRENT,        /* actuation: fit requested->actual, drives PWM */
    CAL_KIND_VOLTAGE_LOAD,   /* reserved for v2: measurement only */
    CAL_KIND_VOLTAGE_SENSE,  /* reserved for v2: measurement only */
} cal_kind_t;

typedef struct {
    uint8_t       flag;         /* CAL_FLAG_* bit of this channel */
    const uint8_t caption[5];   /* menu caption, see MenuItem */
    cal_kind_t    kind;
    uint16_t      default_lo;   /* mA (current) or mV (voltage) */
    uint16_t      default_hi;
    uint16_t      min, max;     /* numeric editor limits */
    uint8_t       dot_offset;
    uint8_t       led;          /* LED_* shown while editing */
    bool          drives_load;  /* true for current only */
} cal_channel_t;

extern const cal_channel_t cal_channel_current;

/* Read the block, validate it and install the stored current calibration in
   cal_slope/cal_offset (load.c). Falls back to the compile-time defaults
   whenever the block or the current channel is invalid. Never writes the
   EEPROM. */
void calibration_init(void);

/* Q4 conversion of a setpoint to the PWM compare value, clamped to
   [0, 0xFFFF]; uses cal_slope/cal_offset. Total for any uint16_t input:
   arguments above CUR_MAX are clamped first, which keeps the Q4 multiply
   inside int32 for every allowed cal_slope/cal_offset. This is the single
   conversion used by the runtime path (load.c) and by the solver validation,
   so the two cannot drift apart. */
uint16_t calibration_current_to_pwm(uint16_t current);

/* Two-point fit for the current channel:
   The setpoints set_lo/set_hi (mA) had been applied and produced the currents
   measured_lo/measured_hi (mA) on the external meter. On success
   (CAL_ERR_NONE) the corrected slope/offset values are stored in slope_out
   and offset_out; they are not applied or stored until the caller passes them
   to calibration_commit_current().
   Uses the calibration currently in RAM (cal_slope/cal_offset) as reference,
   because the calibration in RAM was active while the measurements were
   taken. */
cal_result_t calibration_solve(uint16_t set_lo, uint16_t set_hi,
                               uint16_t measured_lo, uint16_t measured_hi,
                               uint32_t *slope_out, int32_t *offset_out);

/* The measurement points of the current channel as the wizard should
   prefill them: the values stored in the RAM mirror when it is calibrated,
   otherwise the descriptor's defaults. */
void calibration_get_points(uint16_t *set_lo, uint16_t *set_hi);

/* Install a successful two-point fit and persist it. Writes the candidate
   block to the EEPROM first (payload, CRC, magic last, read-back verify);
   only on success are the RAM mirror and cal_slope/cal_offset updated. On
   failure the old calibration stays active and unchanged. The load must be
   disabled. set_lo/set_hi must have been validated by calibration_solve()
   before. */
bool calibration_commit_current(uint16_t set_lo, uint16_t set_hi,
                                uint32_t slope, int32_t offset);

/* Persist whatever is in the RAM mirror (payload, CRC, magic last, read-back
   verify). Used by calibration_reset(); the wizard must not use it to store
   a new fit, it stages one through calibration_commit_current() instead.
   The load must be disabled while this runs. */
bool calibration_save(void);

/* Replace the calibration with the compile-time defaults and store them as a
   valid block with all channel flags cleared. The load must be disabled. */
bool calibration_reset(void);

#endif
