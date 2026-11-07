#include "calibration.h"
#include "config.h"
#include "settings.h"
#include "systick.h"
#include "load.h"

static cal_block_t cal_block;

/* Validation ranges of the two-point fit (calibration notes in the
   measurement procedure). Slopes outside these limits mean one of the points
   was not measured correctly or does not describe the unit. The slope and
   offset limits are also re-checked by calibration_commit_current() before a
   fit is installed; they stay internal to this file. */
#define CAL_GAIN_Q12_MIN   2048L      /* gain 0.5 in Q12 */
#define CAL_GAIN_Q12_MAX   8192L      /* gain 2.0 in Q12 */
#define CAL_INTERCEPT_MAX_MA 2000     /* mA, plausible intercept */
#define CAL_OFFSET_MAX_Q16 500000000L /* |offset constant| limit */
#define CAL_SLOPE_MIN      150000L    /* sane slope range */
#define CAL_SLOPE_MAX      700000L    /* sane slope range */
#define CAL_POINT_GAP     200      /* mA, minimum setpoint separation */
#define CAL_MEAS_GAP      100      /* mA, minimum measured separation */

const cal_channel_t cal_channel_current = {
    .flag = CAL_FLAG_CURRENT,
    .caption = "CURR",
    .kind = CAL_KIND_CURRENT,
    .default_lo = CAL_CURR_LO_DEFAULT,
    .default_hi = CAL_CURR_HI_DEFAULT,
    .min = CUR_MIN,
    .max = CUR_MAX,
    .dot_offset = CUR_DOT_OFFSET,
    .led = LED_A,
    .drives_load = true,
};

/* CRC-8 over the block: polynomial 0x07, MSB first, init 0. Covers bytes
   1..39 but never the magic (written last) nor the crc byte itself. */
static uint8_t cal_crc8(const cal_block_t *block)
{
    const uint8_t *data = (const uint8_t*)block;
    uint8_t crc = 0;
    for (uint8_t i = 1; i < CAL_EEPROM_SIZE; i++) {
        if (i == 3) continue; // Skip the crc byte itself
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* Check magic, version and CRC of a (copied) block. */
static bool cal_block_valid(const cal_block_t *block)
{
    if (block->magic != CAL_MAGIC) return false;
    if (block->version != CAL_VERSION) return false;
    if (block->crc != cal_crc8(block)) return false;
    return true;
}

/* The stored current calibration is only applied if the channel flag is set
   and the values are plausible. Unknown flag bits are ignored. */
static bool cal_current_valid(const cal_block_t *block)
{
    if (!(block->flags & CAL_FLAG_CURRENT)) return false;
    if (block->curr_slope < CAL_SLOPE_MIN ||
        block->curr_slope > CAL_SLOPE_MAX) return false;
    if (block->curr_offset > CAL_OFFSET_MAX_Q16 ||
        block->curr_offset < -CAL_OFFSET_MAX_Q16) return false;
    return true;
}

/* Compile-time defaults, all channels uncalibrated. Never used for hardware
   output unless calibration_reset() or calibration_save() made it valid. */
static void cal_block_default(cal_block_t *block)
{
    block->magic = 0;
    block->version = CAL_VERSION;
    block->flags = 0;
    block->crc = 0;
    block->curr_lo = CAL_CURR_LO_DEFAULT;
    block->curr_hi = CAL_CURR_HI_DEFAULT;
    block->curr_slope = LOAD_CAL_SLOPE;
    block->curr_offset = LOAD_CAL_OFFSET;
    block->vload_lo = 0;
    block->vload_hi = 0;
    block->vload_slope = 0;
    block->vload_offset = 0;
    block->vsense_lo = 0;
    block->vsense_hi = 0;
    block->vsense_slope = 0;
    block->vsense_offset = 0;
}

/* Unclamped PWM for a candidate (slope, offset) pair, in the same Q4 form as
   the runtime path. Shared so solver validation cannot drift from load.c.
   current * slope can exceed uint32 for the largest allowed slope
   (700000 * 10000 mA = 7e9 > 2^32), so both constants are used in Q4:
   current * slope_q4 stays below 2^31 and the shift by 12 removes the Q4
   factor to divide by 2^16: PWM = (current * slope - offset) / 2^16. */
static int32_t cal_pwm_raw(uint32_t slope, int32_t offset, uint16_t current)
{
    uint16_t slope_q4 = (uint16_t)((slope + 8) >> 4);
    int32_t  offset_q4 = offset >> 4;
    return (((int32_t)current * (int32_t)slope_q4) - offset_q4) >> 12;
}

void calibration_init(void)
{
    uint8_t *data = (uint8_t*)(&cal_block);
    for (uint8_t i = 0; i < CAL_EEPROM_SIZE; i++) {
        data[i] = eeprom_read(CAL_EEPROM_OFFSET + i);
    }
    if (cal_block_valid(&cal_block) && cal_current_valid(&cal_block)) {
        /* Valid current calibration: use it for the PWM conversion. */
        cal_slope = cal_block.curr_slope;
        cal_offset = cal_block.curr_offset;
    } else {
        /* Blank, corrupted or unsuitable EEPROM: install the compile-time
           defaults. Do not write the EEPROM at boot. */
        cal_slope = LOAD_CAL_SLOPE;
        cal_offset = LOAD_CAL_OFFSET;
        cal_block_default(&cal_block);
    }
}

uint16_t calibration_current_to_pwm(uint16_t current)
{
    /* Total for any uint16_t argument: clamping at CUR_MAX keeps
       current * slope_q4 inside int32 for every allowed cal_slope, so a
       large nonsense input cannot overflow into a wrong small PWM value. */
    if (current > CUR_MAX) current = CUR_MAX;
    int32_t pwm = cal_pwm_raw(cal_slope, cal_offset, current);
    if (pwm < 0)    pwm = 0;
    if (pwm > 0xFFFF) pwm = 0xFFFF;
    return (uint16_t)pwm;
}

void calibration_get_points(uint16_t *set_lo, uint16_t *set_hi)
{
    if (cal_block.flags & CAL_FLAG_CURRENT) {
        *set_lo = cal_block.curr_lo;
        *set_hi = cal_block.curr_hi;
    } else {
        *set_lo = cal_channel_current.default_lo;
        *set_hi = cal_channel_current.default_hi;
    }
}

cal_result_t calibration_solve(uint16_t set_lo, uint16_t set_hi,
                               uint16_t measured_lo, uint16_t measured_hi,
                               uint32_t *slope_out, int32_t *offset_out)
{
    /* Setpoints and measured currents must be in range and sufficiently far
       apart, otherwise the fit is meaningless. */
    if (set_lo < CUR_MIN || set_lo > CUR_MAX) return CAL_ERR_RANGE;
    if (set_hi < CUR_MIN || set_hi > CUR_MAX) return CAL_ERR_RANGE;
    if (set_hi < set_lo + CAL_POINT_GAP) return CAL_ERR_RANGE;
    if (measured_lo < CUR_MIN || measured_lo > CUR_MAX) return CAL_ERR_RANGE;
    if (measured_hi < CUR_MIN || measured_hi > CUR_MAX) return CAL_ERR_RANGE;
    if (measured_hi < measured_lo + CAL_MEAS_GAP) return CAL_ERR_RANGE;

    /* Measured gain (actual/setpoint) in Q12, ideally near 4096:
       gain_q12 = (measured_hi - measured_lo) * 4096 / (set_hi - set_lo)
       All intermediates fit in 32 bit: the dividend is at most 9800*4096
       and gain_q12 * set_lo is at most 8192*10000 = 8.2e7. */
    int32_t gain_q12 = ((int32_t)(measured_hi - measured_lo) * 4096) /
                       (int32_t)(set_hi - set_lo);
    if (gain_q12 < CAL_GAIN_Q12_MIN || gain_q12 > CAL_GAIN_Q12_MAX) {
        return CAL_ERR_GAIN;
    }

    /* Intercept of the measured current against the setpoint:
       measured = (gain_q12 / 4096) * set + intercept_ma. Signed; negative
       values are normal for a gain error. */
    int32_t intercept_ma = measured_lo -
                           ((gain_q12 * (int32_t)set_lo) / 4096);
    if (intercept_ma > CAL_INTERCEPT_MAX_MA ||
        intercept_ma < -CAL_INTERCEPT_MAX_MA) {
        return CAL_ERR_OFFSET;
    }

    /* The measurement was taken with the calibration currently in RAM. */
    uint32_t slope_ref = cal_slope;
    int32_t offset_ref = cal_offset;
    if (slope_ref < CAL_SLOPE_MIN || slope_ref > CAL_SLOPE_MAX) {
        return CAL_ERR_SLOPE;
    }

    /* Divide the slope by the measured gain, rounded to nearest:
       slope_new = slope_ref * 4096 / gain_q12. slope_ref * 4096 <=
       700000*4096 = 2.87e9 fits in uint32, the result is halved at most by
       the gain limits above. */
    uint32_t slope_new = (slope_ref * 4096UL + (uint32_t)(gain_q12 / 2)) /
                         (uint32_t)gain_q12;
    if (slope_new < slope_ref / 2 || slope_new > slope_ref * 2) {
        return CAL_ERR_SLOPE;
    }
    if (slope_new < CAL_SLOPE_MIN || slope_new > CAL_SLOPE_MAX) {
        return CAL_ERR_SLOPE;
    }

    /* Correct the offset by the intercept times the new slope:
       offset_new = offset_ref + intercept_ma * slope_new. |intercept_ma| <=
       2000 and slope_new <= 700000 keep each term below 1.4e9, so the sum
       fits in int32. */
    int32_t offset_new = offset_ref + intercept_ma * (int32_t)slope_new;
    if (offset_new > CAL_OFFSET_MAX_Q16 ||
        offset_new < -CAL_OFFSET_MAX_Q16) {
        return CAL_ERR_OFFSET;
    }

    /* Realizability: the fit must be rebuildable as PWM codes. If the LO
       point needs a negative PWM the model claims current flows at zero
       drive (impossible); if the HI point exceeds 0xFFFF the plant cannot
       reach it. Reject rather than saving constants that get clamped. */
    int32_t p_lo = cal_pwm_raw(slope_new, offset_new, set_lo);
    int32_t p_hi = cal_pwm_raw(slope_new, offset_new, set_hi);
    if (p_lo <= 0 || p_hi > 0xFFFF) return CAL_ERR_PWM;

    *slope_out = slope_new;
    *offset_out = offset_new;
    return CAL_ERR_NONE;
}

/* Write the block to the EEPROM: payload first, then CRC, then the magic
   byte which marks the block as valid. Interrupted writes always leave one
   of the checks failing. */
static bool cal_block_write(const cal_block_t *block)
{
    const uint8_t *data = (const uint8_t*)block;
    for (uint8_t i = 1; i < CAL_EEPROM_SIZE; i++) {
        if (i == 3) continue; // CRC byte is written after the payload
        eeprom_write(CAL_EEPROM_OFFSET + i, data[i]);
    }
    eeprom_write(CAL_EEPROM_OFFSET + 3, cal_crc8(block));
    eeprom_write(CAL_EEPROM_OFFSET + 0, CAL_MAGIC);

    /* Verify by reading the block back. */
    for (uint8_t i = 0; i < CAL_EEPROM_SIZE; i++) {
        if (eeprom_read(CAL_EEPROM_OFFSET + i) != data[i]) return false;
    }
    return true;
}

bool calibration_commit_current(uint16_t set_lo, uint16_t set_hi,
                                uint32_t slope, int32_t offset)
{
    /* Validate again, independent of the solver: these constants are about
       to become the active calibration. Same limits as cal_current_valid();
       realizability was proven by calibration_solve() for exactly this
       (slope, offset) pair and is not re-checked here. */
    if (slope < CAL_SLOPE_MIN || slope > CAL_SLOPE_MAX) return false;
    if (offset > CAL_OFFSET_MAX_Q16 ||
        offset < -CAL_OFFSET_MAX_Q16) return false;

    /* Stage the candidate: the mirror and the runtime constants are only
       updated once the EEPROM write has been verified. */
    cal_block_t cand;
    cand = cal_block;
    cand.curr_lo = set_lo;
    cand.curr_hi = set_hi;
    cand.curr_slope = slope;
    cand.curr_offset = offset;
    cand.flags |= CAL_FLAG_CURRENT;
    cand.crc = cal_crc8(&cand);
    cand.magic = CAL_MAGIC;
    bool ok = cal_block_write(&cand);
    /* Writing the EEPROM can take several 10s of milliseconds and swallow
       timer ticks, same as in settings_update(). */
    systick_flag &= ~(SYSTICK_OVERFLOW|SYSTICK_COUNT);
    if (!ok) return false;
    cal_block = cand;
    cal_slope = slope;
    cal_offset = offset;
    return true;
}

bool calibration_save(void)
{
    cal_block.crc = cal_crc8(&cal_block);
    /* The magic is written last. Stamping it in the RAM copy as well keeps
       the mirror identical to the verified EEPROM contents. */
    cal_block.magic = CAL_MAGIC;
    bool ok = cal_block_write(&cal_block);
    /* Writing the EEPROM can take several 10s of milliseconds and swallow
       timer ticks, same as in settings_update(). */
    systick_flag &= ~(SYSTICK_OVERFLOW|SYSTICK_COUNT);
    return ok;
}

bool calibration_reset(void)
{
    cal_block_default(&cal_block);
    /* Also fall back to the compile-time calibration right away. */
    cal_slope = LOAD_CAL_SLOPE;
    cal_offset = LOAD_CAL_OFFSET;
    return calibration_save();
}
