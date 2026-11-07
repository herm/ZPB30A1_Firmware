/* Host tests for the calibration wizard (ui_cal_wizard, ui_cal_reset) and
 * the CAL menu hub.
 *
 * This file is NOT part of the firmware build (see software/test/Makefile).
 * It compiles the real ui.c and menu_items.c on the host:
 *
 *   - gen/ui_host.c is a copy of software/ui.c (the copy exists only so the
 *     pin-mapping includes "inc/stm8s_*.h" resolve to the mocks in mock/;
 *     the harness compiles with -D'__interrupt(x)=' to drop the ISRs).
 *   - load.c, settings.c, tm1650.c, beeper.c and adc.c are not linked;
 *     the harness provides their extern objects and stubs, and a display
 *     model that records the drawn characters and decimal points.
 *
 * Events are injected the same way the hardware delivers them: the encoder
 * and button ISRs are fed with GPIO levels, and every ui_timer() call is
 * one 10 ms tick. The load side (load_update) is simulated by writing
 * load_regulated / current_setpoint before the ticks that the regulation
 * gate reads.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "calibration.h"
#include "config.h"
#include "settings.h"
#include "load.h"
#include "ui.h"
#include "menu_items.h"
#include "systick.h"
#include "tm1650.h"
#include "inc/stm8s_gpio.h" /* mock, resolved via -Imock */

void ui_encoder_irq();
void ui_button_irq();

/* ---- RAM-simulated EEPROM (calibration.c) ---- */
#define EE_SIZE 128
static uint8_t ee[EE_SIZE];
static uint32_t write_count;
static int mock_write_enabled = 1;

uint8_t eeprom_read(uint16_t address)
{
    return ee[address];
}

void eeprom_write(uint16_t address, uint8_t data)
{
    if (!mock_write_enabled) return;
    if (ee[address] != data) ee[address] = data;
    write_count++;
}

/* ---- globals the linked modules expect from load.c / settings.c / systick.c ---- */
volatile uint32_t systick;
volatile uint8_t systick_flag;
uint32_t cal_slope;
int32_t cal_offset;
bool cal_active;
uint16_t cal_setpoint;
bool load_active;
bool load_regulated;
uint16_t current_setpoint;
uint8_t load_disable_reason;
error_t error;
uint32_t mAmpere_seconds;
uint32_t mWatt_seconds;
settings_t settings;

void settings_update(void) {}

static int enable_calls;
static int disable_calls;
static uint8_t last_disable_reason;

void load_enable()
{
    enable_calls++;
    load_active = 1;
}

void load_disable(uint8_t reason)
{
    disable_calls++;
    last_disable_reason = reason;
    load_active = 0;
}

/* ---- adc stub ---- */
static uint16_t mock_voltage;

uint16_t adc_get_voltage(void)
{
    return mock_voltage;
}

/* ---- beeper stub ---- */
static int beep_on_calls;

void beeper_on(void)  { beep_on_calls++; }
void beeper_off(void) {}
void beeper_toggle(void) {}

/* ---- display model: records the drawn characters, dots and brightness ---- */
static char top_chars[4];
static char bot_chars[3];
static uint8_t top_dots[4];
static uint8_t bot_dots[3];
static uint8_t bot_leds;
static uint8_t top_bright;
static uint8_t bot_bright;

void disp_char(uint8_t position, uint8_t c, uint8_t dot, uint8_t pin)
{
    if (pin == DP_BOT) {
        if (position < 3) { bot_chars[position] = (char)c; bot_dots[position] = dot; }
    } else {
        if (position < 4) { top_chars[position] = (char)c; top_dots[position] = dot; }
    }
}

void disp_leds(uint8_t leds)
{
    bot_leds = leds;
}

void disp_brightness(uint8_t brightness, uint8_t pin)
{
    if (pin == DP_BOT) bot_bright = brightness;
    else top_bright = brightness;
}

/* ---- test bookkeeping ---- */
static int failures;

static void check(bool ok, const char *what)
{
    printf("  %-62s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static char top_str[5];
static char bot_str[4];

static void read_displays(void)
{
    memcpy(top_str, top_chars, 4); top_str[4] = 0;
    memcpy(bot_str, bot_chars, 3); bot_str[3] = 0;
}

static void check_top(const char *want, const char *what)
{
    read_displays();
    bool ok = strcmp(top_str, want) == 0;
    if (!ok) printf("    top=\"%s\" want \"%s\"\n", top_str, want);
    check(ok, what);
}

static void check_bot(const char *want, const char *what)
{
    read_displays();
    bool ok = strcmp(bot_str, want) == 0;
    if (!ok) printf("    bot=\"%s\" want \"%s\"\n", bot_str, want);
    check(ok, what);
}

/* ---- event injection through the real ISRs ---- */
GPIO_TypeDef mock_gpio_b, mock_gpio_c;

static void enc_up(void)
{
    mock_gpio_b.IDR = PINB_ENC_A; ui_encoder_irq();
    mock_gpio_b.IDR = 0;          ui_encoder_irq();
    ui_timer();
}

static void enc_down(void)
{
    mock_gpio_b.IDR = PINB_ENC_B; ui_encoder_irq();
    mock_gpio_b.IDR = 0;          ui_encoder_irq();
    ui_timer();
}

static void enc_press(void)
{
    mock_gpio_c.IDR &= (uint8_t)~PINC_ENC_P; ui_button_irq();
    mock_gpio_c.IDR |= PINC_ENC_P;           ui_button_irq();
    ui_timer();
}

static void run_press(void)
{
    mock_gpio_c.IDR &= (uint8_t)~PINC_RUN_P; ui_button_irq();
    mock_gpio_c.IDR |= PINC_RUN_P;           ui_button_irq();
    ui_timer();
}

static void tick(void)
{
    ui_timer();
}

/* ---- store a block in the simulated EEPROM (as the wizard would have) ---- */
static uint8_t test_crc8(const cal_block_t *block)
{
    const uint8_t *data = (const uint8_t*)block;
    uint8_t crc = 0;
    for (uint8_t i = 1; i < CAL_EEPROM_SIZE; i++) {
        if (i == 3) continue;
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static void store_block(uint8_t flags, uint32_t slope, int32_t offset,
                        uint16_t set_lo, uint16_t set_hi)
{
    cal_block_t b;
    memset(&b, 0, sizeof(b));
    b.magic = CAL_MAGIC;
    b.version = CAL_VERSION;
    b.flags = flags;
    b.curr_lo = set_lo;
    b.curr_hi = set_hi;
    b.curr_slope = slope;
    b.curr_offset = offset;
    b.crc = test_crc8(&b);
    memcpy(ee + CAL_EEPROM_OFFSET, &b, sizeof(b));
}

static void reset_harness(void)
{
    memset(ee, 0, sizeof(ee));
    write_count = 0;
    mock_write_enabled = 1;
    cal_slope = LOAD_CAL_SLOPE; cal_offset = LOAD_CAL_OFFSET;
    cal_active = 0; cal_setpoint = 0;
    load_active = 0; load_regulated = 0; current_setpoint = 0;
    load_disable_reason = DISABLE_USER;
    error = ERROR_NONE;
    enable_calls = disable_calls = 0; last_disable_reason = 0xFF;
    beep_on_calls = 0;
    memset(top_chars, '?', 4); memset(bot_chars, '?', 3);
    memset(top_dots, 0, 4); memset(bot_dots, 0, 3);
    bot_leds = 0; top_bright = 0; bot_bright = 0;
    mock_voltage = 0;
    mock_gpio_b.IDR = 0; mock_gpio_c.IDR = 0xFF;
    ui_init();
}

/* main -> "..." (settings) -> "CAL " -> "CURR": index 0, one press.
 * The wizard is now on top of the menu stack. */
static void open_wizard(void)
{
    for (int i = 0; i < 3; i++) enc_up();   /* select "..." */
    enc_press();                            /* push the settings submenu */
    for (int i = 0; i < 4; i++) enc_up();   /* select "CAL " */
    enc_press();                            /* push the CAL hub */
    enc_press();                            /* push CURR */
}

/* Navigate to the DEF item of the CAL hub. */
static void open_reset(void)
{
    for (int i = 0; i < 3; i++) enc_up();
    enc_press();
    for (int i = 0; i < 4; i++) enc_up();
    enc_press();
    enc_up();
    enc_press();
}

/* ======================================================================== */
/* W1. Entry refusals                                                        */
/* ======================================================================== */
static int test_enter_refusal(void)
{
    printf("=== W1. entry refused while the load is running ===\n");
    int before = failures;

    reset_harness();
    load_active = 1; /* artificial: the menu cannot be reached this way, but
                        the guard must hold if it ever happens */
    open_wizard();
    check_top("CURR", "refused: back at the CAL hub");
    check_bot("   ", "refused: hub preview keeps the bottom empty");
    check(cal_active == 0 && disable_calls == 0,
          "refused: override untouched, load untouched");
    enc_press();
    check_top("CURR", "still inert after the refused entry");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W2. Happy path: both points, fit accepted, saved, popped                  */
/* ======================================================================== */
static int test_happy_path(void)
{
    printf("=== W2. happy path: LO/HI measured, fit saved ===\n");
    int before = failures;

    reset_harness();
    open_wizard();

    check_top("LO  ", "wizard entered at the LO point");
    check_bot("500", "LO point prefilled from the defaults");
    bool blinked = false;
    for (int i = 0; i < 13; i++) {
        uint8_t b = bot_bright;
        tick();
        if (bot_bright != b) blinked = true;
    }
    check(!blinked, "bottom display does not blink in the point state");

    /* edit LO: press opens the editor, commit advances to the GO screen */
    enc_press();
    check_top("LO  ", "editor: caption stays on the top display");
    check_bot("500", "editor shows the point");
    enc_press(); /* first press selects the second digit stage */
    enc_press(); /* second press commits */
    check_bot("GO ", "committed: GO screen");

    enc_press(); /* apply and run */
    check(load_active == 1 && cal_active == 1 && cal_setpoint == 500,
          "load on at the LO setpoint via the override");
    check_bot("500", "run screen shows the setpoint");
    blinked = false;
    for (int i = 0; i < 13; i++) {
        uint8_t b = bot_bright;
        tick();
        if (bot_bright != b) blinked = true;
    }
    check(blinked, "bottom display blinks while settling");

    /* settle: the simulated load regulates at the calibration setpoint */
    load_regulated = 1;
    current_setpoint = cal_setpoint;
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    check_bot("RDY", "settled: ready for the measurement");

    /* measure: editor prefilled with the setpoint; DMM reads 550 */
    enc_press();
    check_bot("500", "measurement editor prefilled with the setpoint");
    for (int i = 0; i < 5; i++) enc_up(); /* stage 1: +10 each */
    check_bot("550", "edited to the DMM reading");
    enc_press(); /* digit stage */
    enc_press(); /* commit */
    check_top("HI  ", "now at the HI point");
    check_bot("300", "HI point prefilled (3000 mA)");
    check(bot_dots[0] == 1, "HI point shows the decimal point");

    /* HI: accept 3000 (open, digit stage, commit -> GO), then run */
    enc_press(); /* open editor */
    enc_press(); /* digit stage */
    enc_press(); /* commit -> GO screen */
    check_bot("GO ", "HI GO screen");
    enc_press(); /* run */
    check(load_active == 1 && cal_active == 1 && cal_setpoint == 3000,
          "load on at the HI setpoint");
    load_regulated = 1;
    current_setpoint = cal_setpoint;
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    check_bot("RDY", "HI settled");

    /* measured HI: +100 in stage 1 five times -> 3500 */
    enc_press();
    for (int i = 0; i < 5; i++) enc_up();
    check_bot("350", "edited to the DMM reading");
    enc_press(); /* digit stage */
    enc_press(); /* commit -> compute -> save screen */
    check_top("OK  ", "fit computed: save screen");
    check_bot("SAV", "SAVE caption truncated to the 3 bottom digits");
    check(load_active == 0 && cal_active == 0 &&
          last_disable_reason == DISABLE_USER,
          "load disabled before the save");

    /* the fit for (500, 3000, 550, 3500), reference = stock constants */
    cal_slope = LOAD_CAL_SLOPE; cal_offset = LOAD_CAL_OFFSET;
    uint32_t slope_new = 0; int32_t offset_new = 0;
    check(calibration_solve(500, 3000, 550, 3500, &slope_new, &offset_new) == CAL_ERR_NONE,
          "setup: expected fit solves");

    uint32_t wc_before = write_count;
    enc_press(); /* SAVE */
    check_top("DONE", "stored: DONE screen");
    check(cal_slope == slope_new && cal_offset == offset_new, "RAM constants installed");
    cal_block_t stored;
    memcpy(&stored, ee + CAL_EEPROM_OFFSET, sizeof(stored));
    check(stored.magic == CAL_MAGIC && stored.flags == CAL_FLAG_CURRENT &&
          stored.curr_lo == 500 && stored.curr_hi == 3000 &&
          stored.curr_slope == slope_new && stored.curr_offset == offset_new,
          "committed block in the EEPROM");
    check(write_count == wc_before + 40, "one verified block write");

    for (int i = 0; i < 102; i++) tick();
    check_top("CURR", "popped back to the CAL hub");
    check_bot("   ", "hub preview keeps the bottom empty");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W3. HI point must stay above the LO point                                 */
/* ======================================================================== */
static int test_hi_not_below_lo(void)
{
    printf("=== W3. run refused when HI <= LO ===\n");
    int before = failures;

    reset_harness();
    open_wizard();

    /* LO cycle: accept 500 (open, digit stage, commit -> GO), run it
       regulated, accept the measurement */
    enc_press();                            /* editor */
    enc_press(); enc_press();               /* digit stage, commit -> GO */
    load_regulated = 1; current_setpoint = 500;
    enc_press();                            /* run */
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    enc_press(); enc_press(); enc_press();  /* measurement: open, stage, commit */

    /* HI editor: 20 downs reach 1000 (then the increment drops to 10), so
       20 + 41 = 61 downs reach 500 = the LO point. */
    enc_press();
    for (int i = 0; i < 61; i++) enc_down();
    check_bot("500", "HI edited down to the LO point");
    enc_press(); enc_press();               /* digit stage, commit -> GO */
    check_bot("GO ", "GO screen reached");
    /* The load keeps running at the LO setpoint while the HI point is
       edited (the plan only disables it on abort/error/commit). */
    int en = enable_calls;
    uint32_t wc = write_count;
    enc_press();                            /* press: must be refused */
    check(enable_calls == en && cal_active == 1 && cal_setpoint == 500,
          "run refused when hi <= lo, load keeps the LO setpoint");
    check_bot("GO ", "stays on the GO screen");
    check(write_count == wc, "nothing written");
    run_press();
    check(load_active == 0 && cal_active == 0,
          "abort after the refusal: load off");
    check_top("CURR", "aborted back to the hub");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W4. Regulation gate failure aborts without offering the editor            */
/* ======================================================================== */
static int test_gate_failure(void)
{
    printf("=== W4. unregulated load aborts after the settle time ===\n");
    int before = failures;

    reset_harness();
    open_wizard();
    enc_press();               /* editor */
    enc_press(); enc_press();  /* digit stage, commit -> GO */
    enc_press();               /* run */
    check(load_active == 1 && cal_active == 1, "running");
    load_regulated = 0;        /* e.g. PSU too weak: never regulates */
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    check(load_active == 0 && cal_active == 0 &&
          last_disable_reason == DISABLE_USER,
          "gate failure: load off, override cleared");
    check_top("CURR", "aborted back to the hub");
    check(write_count == 0, "no EEPROM write");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W5. RUN button aborts mid-run                                             */
/* ======================================================================== */
static int test_run_abort(void)
{
    printf("=== W5. RUN button aborts during the LO run ===\n");
    int before = failures;

    reset_harness();
    open_wizard();
    enc_press();               /* editor */
    enc_press(); enc_press();  /* digit stage, commit -> GO */
    enc_press();               /* run */
    check(load_active == 1 && cal_active == 1, "running");
    run_press();
    check(load_active == 0 && cal_active == 0 &&
          last_disable_reason == DISABLE_USER, "RUN aborted the wizard");
    check_top("CURR", "aborted back to the hub");
    check(write_count == 0, "no EEPROM write");

    /* The wizard must be cleanly re-enterable after an abort. */
    enc_press();
    check_top("LO  ", "re-entered after the abort, state reset");
    run_press();
    check_top("CURR", "aborted again");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W6. 60 s inactivity timeout, deferred by user events                      */
/* ======================================================================== */
static int test_timeout(void)
{
    printf("=== W6. inactivity timeout, reset by user events ===\n");
    int before = failures;

    reset_harness();
    open_wizard();
    /* ~59.9 s of idle ticks: still alive */
    for (int i = 0; i < CAL_TIMEOUT_TICKS - 10; i++) tick();
    check_top("LO  ", "still active just before the timeout");
    enc_press(); /* user event: resets the counter, opens the editor */
    check_bot("500", "editor reachable again");
    for (int i = 0; i < CAL_TIMEOUT_TICKS - 10; i++) tick();
    check_top("LO  ", "the press deferred the timeout");
    for (int i = 0; i < 20; i++) tick();
    check_top("CURR", "60 s idle aborted back to the hub");
    check(load_active == 0 && cal_active == 0, "override cleared");
    check(write_count == 0, "no EEPROM write");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W7. Error menu pushed over the wizard aborts it on return                 */
/* ======================================================================== */
static int test_error_over_wizard(void)
{
    printf("=== W7. error menu over the wizard ===\n");
    int before = failures;

    reset_harness();
    open_wizard();
    enc_press(); enc_press(); enc_press();  /* editor, digit stage, commit -> GO */
    enc_press();                            /* run */
    check(load_active == 1 && cal_active == 1, "running");
    error = ERROR_OVERLOAD;                 /* e.g. power limit */
    tick();                                 /* ui_timer() pushes menu_error */
    check_top("OVLD", "error menu on top of the wizard");
    uint32_t wc = write_count;
    error = ERROR_NONE;
    tick();                                 /* error menu pops; wizard aborts */
    check(load_active == 0 && cal_active == 0 &&
          last_disable_reason == DISABLE_USER,
          "wizard aborted cleanly when the error menu returned");
    check_top("CURR", "back at the CAL hub");
    check(write_count == wc, "no EEPROM write");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W8. Rejected fit: ERR screen with the reason code, nothing stored         */
/* ======================================================================== */
static int test_fit_rejected(void)
{
    printf("=== W8. fit rejected: ERR screen, nothing stored ===\n");
    int before = failures;

    reset_harness();
    open_wizard();
    /* LO: accept 500, run, accept the measurement */
    enc_press(); enc_press(); enc_press();
    enc_press();
    load_regulated = 1; current_setpoint = 500;
    enc_press();
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    enc_press(); enc_press(); enc_press();
    /* HI: edit the point down to 1000 (20 downs), run it; then enter a
       measured 600 from the prefilled 1000: the first down is 100
       (1000 -> 900), after that the increment drops to 10, so 31 downs
       reach 600. Measured gain (600-500)*4096/(1000-500) = 819 < 2048 ->
       CAL_ERR_GAIN */
    enc_press();
    for (int i = 0; i < 20; i++) enc_down();
    check_bot("100", "HI edited to 1000 mA");
    enc_press(); enc_press();               /* digit stage, commit -> GO */
    current_setpoint = 1000; load_regulated = 1;
    enc_press();                            /* run */
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    check_bot("RDY", "HI settled");
    enc_press();                            /* open the measurement editor */
    check_bot("100", "measurement editor prefilled with the HI point");
    for (int i = 0; i < 31; i++) enc_down();
    check_bot("600", "measured HI edited to 600");
    enc_press(); enc_press();               /* digit stage, commit -> compute */
    check_top("ERR ", "fit rejected: ERR screen");
    check_bot("003", "reason code 3 (CAL_ERR_GAIN + 1)");
    check(write_count == 0, "nothing written");
    check(cal_slope == LOAD_CAL_SLOPE && cal_offset == LOAD_CAL_OFFSET,
          "RAM calibration untouched");
    check(load_active == 0 && cal_active == 0, "load off");
    for (int i = 0; i < 105; i++) tick();
    check_top("CURR", "error screen popped to the hub");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W9. EEPROM write failure at the SAVE step                                 */
/* ======================================================================== */
static int test_write_failure(void)
{
    printf("=== W9. EEPROM write failure at SAVE ===\n");
    int before = failures;

    reset_harness();
    open_wizard();
    /* LO cycle with a +10% reading: open, digit stage, commit -> GO, run */
    enc_press(); enc_press(); enc_press();
    enc_press();
    load_regulated = 1; current_setpoint = 500;
    enc_press(); /* stray press: ignored while settling */
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    enc_press();
    for (int i = 0; i < 5; i++) enc_up();   /* 550 */
    enc_press(); enc_press();
    /* HI cycle: accept 3000, measure 3500 */
    enc_press(); enc_press(); enc_press();
    enc_press();
    current_setpoint = 3000; load_regulated = 1;
    enc_press();
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    enc_press();
    for (int i = 0; i < 5; i++) enc_up();   /* 3500 */
    enc_press(); enc_press();
    check_top("OK  ", "at the save screen");

    mock_write_enabled = 0;
    enc_press();                            /* SAVE */
    mock_write_enabled = 1;
    check_top("ERR ", "write failure: ERR screen");
    check_bot("WR ", "write-failure code");
    check(cal_slope == LOAD_CAL_SLOPE && cal_offset == LOAD_CAL_OFFSET,
          "RAM calibration unchanged by the failed commit");
    check(ee[CAL_EEPROM_OFFSET + 0] == 0x00, "EEPROM untouched");
    check(load_active == 0 && cal_active == 0, "load stays off");
    for (int i = 0; i < 105; i++) tick();
    check_top("CURR", "popped to the hub");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W10. DEF: confirmed reset writes the default block                        */
/* ======================================================================== */
static int test_def_reset(void)
{
    printf("=== W10. DEF resets to the compile-time defaults ===\n");
    int before = failures;

    reset_harness();
    /* give the wizard something to reset first */
    store_block(CAL_FLAG_CURRENT, 318629L, 9140616L, 700, 4000);
    cal_slope = 0; cal_offset = 0;
    calibration_init();
    check(cal_slope == 318629L, "setup: calibrated block loaded");

    open_reset();
    check_top("DEF ", "reset prompt");
    check_bot("SUR", "SURE prompt truncated to the 3 bottom digits");
    uint32_t wc = write_count;
    enc_press();
    check_top("DONE", "defaults restored");
    cal_block_t stored;
    memcpy(&stored, ee + CAL_EEPROM_OFFSET, sizeof(stored));
    check(stored.flags == 0 && stored.magic == CAL_MAGIC &&
          stored.curr_slope == LOAD_CAL_SLOPE && stored.curr_offset == LOAD_CAL_OFFSET,
          "default block written, all flags cleared");
    check(cal_slope == LOAD_CAL_SLOPE && cal_offset == LOAD_CAL_OFFSET,
          "RAM constants back to the compile-time defaults");
    check(write_count == wc + 40, "one verified block write");
    for (int i = 0; i < 102; i++) tick();
    check_top("DEF ", "popped back to the hub");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W11. DEF refuses while the load is running; RUN cancels                    */
/* ======================================================================== */
static int test_def_refuse_and_cancel(void)
{
    printf("=== W11. DEF refuse (load on) and RUN cancel ===\n");
    int before = failures;

    reset_harness();
    load_active = 1; /* artificial, see W1 */
    open_reset();
    check_top("DEF ", "reset prompt");
    uint32_t wc = write_count;
    enc_press();
    check_bot("ON ", "refused while the load is running");
    check(write_count == wc, "no reset performed");
    run_press();
    check_top("DEF ", "cancel pops back to the hub");
    check(write_count == wc, "still nothing written");

    /* RUN cancels without pressing */
    reset_harness();
    open_reset();
    wc = write_count;
    run_press();
    check_top("DEF ", "RUN pops back to the hub");
    check(write_count == wc, "no reset on cancel");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W12. Editor cancel keeps the point                                        */
/* ======================================================================== */
static int test_editor_cancel(void)
{
    printf("=== W12. editor cancel returns to the point state ===\n");
    int before = failures;

    reset_harness();
    open_wizard();
    enc_press();          /* open the LO editor */
    enc_up();             /* 500 -> 510 */
    check_bot("510", "edited value shown");
    run_press();          /* RUN inside the editor: cancel */
    check_bot("500", "cancel kept the old point");
    check_top("LO  ", "back at the LO point");
    run_press();          /* abort the wizard */
    check_top("CURR", "hub");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
/* W13. Points are prefilled from a stored block                             */
/* ======================================================================== */
static int test_prefill_from_block(void)
{
    printf("=== W13. points prefilled from a stored calibration ===\n");
    int before = failures;

    reset_harness();
    store_block(CAL_FLAG_CURRENT, 318629L, 9140616L, 700, 4000);
    cal_slope = 0; cal_offset = 0;
    calibration_init();
    open_wizard();
    check_bot("700", "LO point prefilled from the block");
    /* run the LO cycle to reach the HI point: open, digit stage,
       commit -> GO, run */
    enc_press(); enc_press(); enc_press();
    load_regulated = 1; current_setpoint = 700;
    enc_press();
    for (int i = 0; i < CAL_SETTLE_TICKS + 2; i++) tick();
    enc_press(); enc_press(); enc_press();  /* accept the measurement */
    check_top("HI  ", "at the HI point");
    check_bot("400", "HI point prefilled from the block (4000 mA)");
    run_press();
    check_top("CURR", "aborted back to the hub");

    return failures > before ? 1 : 0;
}

/* ======================================================================== */
int main(void)
{
    printf("ZPB30A1 calibration wizard host tests\n\n");

    int failed = 0;
    failed += test_enter_refusal();
    failed += test_happy_path();
    failed += test_hi_not_below_lo();
    failed += test_gate_failure();
    failed += test_run_abort();
    failed += test_timeout();
    failed += test_error_over_wizard();
    failed += test_fit_rejected();
    failed += test_write_failure();
    failed += test_def_reset();
    failed += test_def_refuse_and_cancel();
    failed += test_editor_cancel();
    failed += test_prefill_from_block();

    printf("\nRESULT: %d failure(s)\n", failures);
    return failed;
}
