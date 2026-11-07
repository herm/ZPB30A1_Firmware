#include "ui.h"
#include "tm1650.h"
#include "load.h"
#include "config.h"
#include "settings.h"
#include "menu_items.h"
#include "beeper.h"
#include "utils.h"
#include "stdio.h" //Debugging only
#include "inc/stm8s_gpio.h"
#include "inc/stm8s_itc.h"
#include "adc.h"
#include "calibration.h"

typedef enum {
    /* Bitmask:
    Bit 0: Brightness
    Bit 1: Blink
    */
    DISP_MODE_BRIGHT       = 0b000,
    DISP_MODE_DIM          = 0b001,
    DISP_MODE_BLINK        = 0b010,
} display_mode_t;

typedef enum {
    EVENT_BITMASK_MENU     = 0b11,
    EVENT_ENTER            = 0b10,
    EVENT_RETURN           = 0b11,
    EVENT_BITMASK_ENCODER  = 0b11100,
    EVENT_ENCODER_UP       = 0b10000,
    EVENT_ENCODER_DOWN     = 0b10100,
    EVENT_ENCODER_BUTTON   = 0b11000,
    EVENT_RUN_BUTTON       = 0b11100,
    EVENT_TIMER            = 0b100000,
    EVENT_PREVIEW          = 0b1000000,
} ui_event_t;

static volatile int8_t encoder_val = 0;
static volatile bool encoder_pressed = 0;
static volatile bool run_pressed = 0;
static uint8_t display_mode[] = {DISP_MODE_BRIGHT, DISP_MODE_BRIGHT};
static bool blink_state = false;
static uint8_t led_state = 0;

/* Maximum menu depth: main -> settings -> submenu -> submenu -> edit value,
   with one level left for the error menu which is pushed on top of any depth. */
#define MENU_STACK_DEPTH 6
static const MenuItem *menu_stack[MENU_STACK_DEPTH];
static uint8_t menu_subitem_index[MENU_STACK_DEPTH];
static uint8_t menu_stack_head = 0;

#define current_item menu_stack[menu_stack_head]
#define current_subitem_index menu_subitem_index[menu_stack_head]
#define current_subitem current_item->subitems[current_subitem_index]

static void ui_leds(uint8_t leds);
static void ui_text(const char *text, uint8_t display);
static void ui_number(uint16_t num, uint8_t dot, uint8_t display);
static void ui_push_item(const MenuItem *item);
static void ui_pop_item(void);

static void ui_set_display_mode(display_mode_t mode, display_t disp)
{
    display_mode[disp] = mode;
    if (mode & DISP_MODE_DIM) {
        disp_brightness(BRIGHTNESS_DIM, disp);
    } else {
        disp_brightness(BRIGHTNESS_BRIGHT, disp);
    }
}

void ui_init(void)
{
    ui_set_display_mode(DISP_MODE_BRIGHT, DP_TOP);
    ui_set_display_mode(DISP_MODE_BRIGHT, DP_BOT);
    menu_stack_head = 0;
    // Reset all button events which might have triggered before the system was fully initialized.
    run_pressed = 0; 
    encoder_val = 0;
    encoder_pressed = 0;
    ui_leds(0);
    current_item = &menu_main;
    current_subitem_index = 0;
    menu_main.handler(EVENT_ENTER, &menu_main);
}

static void ui_timer_blink(void)
{
    static uint16_t blink_timer = 0;

    blink_timer++;
    if (blink_timer == F_SYSTICK/F_DISPLAY_BLINK) {
        blink_timer = 0;
        blink_state = !blink_state;
        for (uint8_t i=0; i<2; i++)
        {
            if (display_mode[i] & DISP_MODE_BLINK) {
                ui_set_display_mode(display_mode[i] ^ DISP_MODE_DIM, i);
            }
        }
    }
    uint8_t leds = led_state;
    /* run led
     * on: load active
     * blinking: load automatically disabled (e.g. undervoltage cutoff, error)
     * off: load disabled by user
     */
    if (load_active || ((load_disable_reason != DISABLE_USER) && blink_state)) {
        leds |= LED_RUN;
    }
    disp_leds(leds);
}

void ui_error_handler(uint8_t event, const MenuItem *item)
{
    (void) item; //Unused
    if (error == 0) {
        // Error has been resolved
        ui_pop_item();
    }
    if (event == EVENT_PREVIEW || event == EVENT_TIMER) return;
    const char msgs[][5] = {"", "POL ", "OVP ", "OVLD", "PWR", "TEMP", "SUP ", "TIME", "INT ", "CMD "};

    ui_text("ERR", DP_BOT);
    ui_text(msgs[error], DP_TOP);
    ui_set_display_mode(DISP_MODE_DIM, DP_TOP);
    ui_set_display_mode(DISP_MODE_DIM, DP_BOT);

    if (event == EVENT_ENCODER_BUTTON) {
        // User acknowledged error
        error = ERROR_NONE;
        ui_pop_item();
    }
}

static void ui_timer_beeper(void)
{
    uint8_t timer_value = 0;
    static uint8_t timer = 0;
    if (load_disable_reason == DISABLE_CUTOFF) {
        timer_value = F_SYSTICK / F_BEEP_CUTOFF;
    }
    if (error) {
        timer_value = F_SYSTICK / F_BEEP_ERROR;
    }
    if (!timer_value) {
        if (encoder_val) {
            beeper_on();
            _delay_us(30);
        }
        beeper_off();
        timer = 0;
        return;
    }
    if (++timer >= timer_value) {
        timer = 0;
        beeper_toggle();
    }
}

void ui_timer(void)
{
    ui_timer_blink();
    ui_timer_beeper();
    if (error && current_item != &menu_error) {
        // Show error
        ui_push_item(&menu_error);
    }
    if (encoder_val > 0) {
        current_item->handler(EVENT_ENCODER_UP, current_item);
    }
    if (encoder_val < 0) {
        current_item->handler(EVENT_ENCODER_DOWN, current_item);
    }
    if (encoder_pressed) {
        current_item->handler(EVENT_ENCODER_BUTTON, current_item);
    }
    if (run_pressed) {
        current_item->handler(EVENT_RUN_BUTTON, current_item);
    }
    current_item->handler(EVENT_TIMER, current_item);
    encoder_val = 0;
    run_pressed = 0;
    encoder_pressed = 0;
}

/* Display text. */
static void ui_text(const char *text, uint8_t display)
{
    for (uint8_t i=0; i<4; i++) {
        /* Bottom display only has 3 digits, 4th digits are the LEDs. */
        if (display == DP_TOP || i != 3)
            disp_char(i, text[i], 0, display);
    }
}

/* Display a number with automatic scaling.
 * dot = position where the dot should go
 */
static void ui_number(uint16_t num, uint8_t dot, uint8_t display)
{
    uint16_t maximum = (display == DP_TOP) ? 10000 : 1000;
    uint16_t digits = (display == DP_TOP) ? 4 : 3;
    while (num >= maximum) {
        num /= 10;
        dot--;
    }
    for (int8_t i=digits-1; i>=0; i--)
    {
        disp_char(i, num % 10 + '0', dot==(digits-1-i), display);
        num /= 10;
    }
}

/* Set leds. Run led is updated automatically. */
static inline void ui_leds(uint8_t leds)
{
    led_state = leds; // Hardware is updated in ui_timer_blink()
}

/* Get the number of subitems. */
static uint8_t ui_num_subitem(const MenuItem *item)
{
    uint8_t max_ = 0;
    const MenuItem **p = item->subitems;
    while (*p++) max_++;
    return max_;
}


/*********** Menu item handlers ***********/

/* Show a new menu. */
static void ui_push_item(const MenuItem *item)
{
    /* Bounds guard: keep one stack slot free for the error menu, which
       ui_timer() pushes from any depth. ui_timer() retries every tick. */
    if (menu_stack_head >= MENU_STACK_DEPTH - 1) {
        return;
    }
    menu_stack_head++;
    current_item = item;
    current_subitem_index = 0;
    item->handler(EVENT_ENTER, item);
}

/* Return to previous menu. */
static void ui_pop_item(void)
{
    /* Never pop the main menu. */
    if (menu_stack_head != 0) {
        menu_stack_head--;
    }
    ui_leds(0);
    settings_update(); //Store changed value to eeprom
    current_item->handler(EVENT_RETURN, current_item);
}

/* Function to select a subitem. Used to select submenu and subitem.
 * This function must only be called for the currently active item. */
static void ui_select(uint8_t event, const MenuItem *item, uint8_t display)
{
    bool output = event & (EVENT_BITMASK_MENU | EVENT_BITMASK_ENCODER);
    if (event & EVENT_BITMASK_MENU) {
        ui_set_display_mode(DISP_MODE_BLINK, display);
        ui_set_display_mode(DISP_MODE_DIM, display ^ 1); /* Dim second display. */
    }
    if (event == EVENT_ENCODER_UP) {
        if (current_subitem_index < ui_num_subitem(item) - 1) {
            current_subitem_index++;
        } else {
            current_subitem_index = 0;
        }
    }
    if (event == EVENT_ENCODER_DOWN) {
        if (current_subitem_index > 0) {
            current_subitem_index--;
        } else {
            current_subitem_index = ui_num_subitem(item) - 1;
        }
    }
    if (output) {
        ui_text(current_subitem->caption, display);
    }
    if (event == EVENT_RUN_BUTTON) {
        ui_pop_item();
    }
}

/** Allows selecting a menu item in the top display.
    The child handler is called to update the bottom display. */
void ui_submenu(uint8_t event, const MenuItem *item)
{
    if (event == EVENT_PREVIEW) {
        // Show nothing as preview (this menu's name is shown in top display by parent item's handler)
        ui_text("   ", DP_BOT);
        return;
    }

    ui_select(event, item, DP_TOP);
    if (current_subitem->handler) {
        current_subitem->handler(EVENT_PREVIEW, current_subitem);
    } else {
        ui_text("===", DP_BOT); //Show warning that no handler is defined (makes no sense for ui_submenu!)
    }
    if (event == EVENT_ENCODER_BUTTON) {
        ui_push_item(current_subitem);
    }
}

static uint8_t ui_find_active_subitem(const MenuItem *item)
{
    uint8_t n = ui_num_subitem(item);
    uint8_t value = *((uint8_t*)item->data);
    for (uint8_t i=0; i<n; i++) {
        if (item->subitems[i]->value == value) {
            return i;
        }
    }
    // Item not found => use first subitem as default value!
    return 0;
}

/** Allows selecting an item in the bottom menu.
    If the child element has an event handler it is called on selection.
    Otherwise the parent's data element is expected to point to a uint8_t
    variable which is set to the child's data element.
    Very similar to ui_submenu, but selection happens in the bottom menu and no subitem preview is shown (only the caption). */
void ui_select_item(uint8_t event, const MenuItem *item)
{
    if (event == EVENT_PREVIEW) {
        // Show selected value in bottom display as preview
        ui_text(item->subitems[ui_find_active_subitem(item)]->caption, DP_BOT);
        return;
    }
    if (event == EVENT_ENTER) {
        current_subitem_index = ui_find_active_subitem(item);
    }
    ui_select(event, item, DP_BOT);

    if (event == EVENT_ENCODER_BUTTON) {
        /* If a handler is available use it to set the value. */
        if (current_subitem->handler) {
            current_subitem->handler(EVENT_ENTER, current_subitem);
        } else {
            /* No handler => just set the value ourselves. */
            uint8_t *p = (uint8_t*)item->data;
            *p = current_subitem->value;
            ui_pop_item();
        }
    }
}

//TODO: Change step size depeding on encoder speed
/* Edit numeric values. This function encapsulates almost all the logic
   except setting the secondard display's value and selecting the LEDs.
   The caller must pop the current menu item when the return value is
   UI_EDIT_COMMIT or UI_EDIT_CANCEL. COMMIT persists the new value in
   *edit->var, CANCEL leaves it unchanged. */
ui_edit_result_t ui_edit_value_internal(uint8_t event, const NumericEdit *edit, uint8_t leds)
{
    static uint16_t value;
    uint8_t display = DP_BOT, display2 = DP_TOP;
    ui_edit_result_t result = UI_EDIT_ACTIVE;
    if (event == EVENT_PREVIEW) {
        ui_number(*edit->var, edit->dot_offset, display);
        return result;
    }

    uint16_t inc;
    if (value >= 10000) {
        inc = 100;
    } else if (value >= 1000) {
        inc = 10;
    } else {
        inc = 1;
    }
    if (current_subitem_index == 0) {
        inc *= 10;
    }

    switch (event) {
    case EVENT_ENTER:
        ui_set_display_mode(DISP_MODE_BRIGHT, display);
        ui_set_display_mode(DISP_MODE_DIM, display2);
        ui_leds(leds | LED_DIGIT1);
        value = *edit->var;
        current_subitem_index = 0;
        break;
    case EVENT_RUN_BUTTON:
        result = UI_EDIT_CANCEL;
        break;
    case EVENT_ENCODER_BUTTON:
        if (current_subitem_index == 0) {
            current_subitem_index = 1;
            ui_leds(leds | LED_DIGIT2);
        } else {
            *edit->var = value;
            result = UI_EDIT_COMMIT;
        }
        break;
    case EVENT_ENCODER_UP:
        if (value < edit->max - inc) {
             value += inc;
         } else {
             value = edit->max;
         }
        break;
    case EVENT_ENCODER_DOWN:
        if (value > edit->min + inc) {
            value -= inc;
        } else {
            value = edit->min;
        }
        break;
    }
    bool output = result == UI_EDIT_ACTIVE && event & (EVENT_BITMASK_MENU | EVENT_BITMASK_ENCODER);
    if (output) {
        ui_number(value, edit->dot_offset, display);
    }
    return result;
}

/* Pop the editor's menu item once the edit is done. Must be called after
   ui_edit_value_internal to avoid overwriting the display of the parent. */
static void ui_finish_edit(ui_edit_result_t result)
{
    if (result != UI_EDIT_ACTIVE) {
        ui_pop_item();
    }
}

void ui_edit_value(uint8_t event, const MenuItem *item)
{
    const NumericEdit *edit = item->data;
    ui_finish_edit(ui_edit_value_internal(event, edit, item->value));
}

/* Special numeric edit handler for setpoint which changes
 * its label and the value being edited depending on the
 * current operation mode.
 */
void ui_edit_setpoint(uint8_t event, const MenuItem *item)
{
    (void) item; //unused
    const NumericEdit *edit = 0;
    const char *label = 0;
    uint8_t leds = 0;
    switch (settings.mode) {
        case MODE_CC:
            edit = &menu_value_edit_CC;
            label = "AMP ";
            leds = LED_A;
            break;
        case MODE_CV:
            edit = &menu_value_edit_CV;
            label = "VOLT";
            leds = LED_V;
            break;
        case MODE_CR:
            edit = &menu_value_edit_CR;
            label = "OHM ";
            break;
        case MODE_CW:
            edit = &menu_value_edit_CW;
            label = "WATT";
            break;
        default:
            edit = 0;
            label = "===";
            break;
    }
    if (event != EVENT_PREVIEW) ui_text(label, DP_TOP);
    if (edit) ui_finish_edit(ui_edit_value_internal(event, edit, leds));
}

/* Show measured values and setpoint in run mode.
 * Switch automatically between values if user didn't select one. */
void ui_show_values(uint8_t event)
{
    static uint16_t switch_timer = 0;
    static uint8_t update_timer = 0;
    static bool manual_mode = false;
    enum {
        STATE_V,
        STATE_AH,
        STATE_WH,
        STATE_MAX,
    };
    static uint8_t state = STATE_V;
    bool update = false;

    if (event == EVENT_ENTER || event == EVENT_RETURN)
    {
        switch_timer = 0;
        update_timer = F_SYSTICK/F_UI_UPDATE_DISPLAY - 1;
        manual_mode = 0;
    }

    if (event == EVENT_ENCODER_UP) {
        manual_mode = true;
        update = true;
        if (++state == STATE_MAX) state = STATE_V;
    }

    if (event == EVENT_ENCODER_DOWN) {
        manual_mode = true;
        update = true;
        if (state-- == STATE_V) state = STATE_MAX - 1;
    }

    if (!manual_mode && (++switch_timer == F_SYSTICK/F_UI_SWITCH_DISPLAY)) {
        switch_timer = 0;
        if (++state == STATE_MAX) state = STATE_V;
    }

    if (((event == EVENT_TIMER) && (++update_timer == F_SYSTICK/F_UI_UPDATE_DISPLAY)) ||
         update) {
        update_timer = 0;
        switch (state) {
            case STATE_V:
                ui_leds(LED_A|LED_V);
                ui_number(adc_get_voltage(), VOLT_DOT_OFFSET, DP_TOP);
                break;
            case STATE_AH:
                ui_leds(LED_A|LED_AH);
                ui_number(mAmpere_seconds/3600, AS_DOT_OFFSET, DP_TOP);
                break;
            case STATE_WH:
                ui_leds(LED_A|LED_WH);
                ui_number(mWatt_seconds/3600, WS_DOT_OFFSET, DP_TOP);
                break;
        }

        ui_number(current_setpoint, CUR_DOT_OFFSET, DP_BOT);

        /* Blink bottom display if load is unregulated. */
        if (load_regulated) {
            display_mode[DP_BOT] &= ~DISP_MODE_BLINK;
        } else {
            display_mode[DP_BOT] |= DISP_MODE_BLINK;
        }
    }
}

/* Shared code from run and info mode */
static void ui_run_info_mode(uint8_t event)
{
    if (event == EVENT_PREVIEW) {
        // Show nothing as preview (this menu's name is shown in top display by parent item's handler)
        ui_text("   ", DP_BOT);
        return;
    }

    if (event == EVENT_RUN_BUTTON) {
        load_disable(DISABLE_USER);
        ui_pop_item();
    }

    if (event == EVENT_ENTER || event == EVENT_RETURN) {
        ui_set_display_mode(DISP_MODE_DIM, DP_TOP);
        ui_set_display_mode(DISP_MODE_DIM, DP_BOT);
    }

    ui_show_values(event);
}

void ui_run_mode(uint8_t event, const MenuItem *item)
{
    if (event == EVENT_ENTER) {
        load_enable();
    }
    if (event == EVENT_ENCODER_BUTTON) {
        if (load_disable_reason != DISABLE_USER) {
            /* User acknowledged stop. */
            load_disable_reason = DISABLE_USER;
        } else {
            /* User wants to change value while load is active. */
            ui_push_item(item->subitems[0]);
        }
    }
    ui_run_info_mode(event);
}

void ui_info_mode(uint8_t event, const MenuItem *item)
{
    (void) item; // unused
    if (event == EVENT_ENCODER_BUTTON) {
        // Encoder button acts as a back button in info mode
        ui_pop_item();
    }
    ui_run_info_mode(event);
}

void ui_clear_counters(uint8_t event, const MenuItem *item)
{
    static uint8_t timer = 0;
    (void) item; // unused
    if (event == EVENT_PREVIEW) {
        // Show nothing as preview (this menu's name is shown in top display by parent item's handler)
        ui_text("CNT", DP_BOT);
        return;
    }
    if (event == EVENT_ENTER || event == EVENT_RETURN) {
        ui_set_display_mode(DISP_MODE_DIM, DP_TOP);
        ui_set_display_mode(DISP_MODE_DIM, DP_BOT);
        ui_text("DONE", DP_TOP);
        ui_text("   ", DP_BOT);
        timer = 100;
        mWatt_seconds = 0;
        mAmpere_seconds = 0;
    }

    if (event == EVENT_TIMER) {
        if (--timer == 0) {
            ui_pop_item();
        }
    }

    if (event == EVENT_RUN_BUTTON) {
        ui_pop_item();
    }
}

/* ------------------------------------------------------------------------ */
/* On-device calibration wizard (menu item CURR, channel descriptor in       */
/* item->data) and the defaults reset (DEF).                                 */
/* ------------------------------------------------------------------------ */

/* Wizard states. */
enum {
    WIZ_POINT_LO,      /* top "LO  ", bottom: LO setpoint; press -> editor */
    WIZ_EDIT_LO,       /* inline editor for the LO setpoint */
    WIZ_GO_LO,         /* top "LO  ", bottom "GO "; press -> apply and run */
    WIZ_RUN_LO,        /* load on at LO, settling (bottom display blinks) */
    WIZ_MEAS_LO,       /* bottom "RDY"; press -> editor for the DMM reading */
    WIZ_EDIT_MEAS_LO,  /* inline editor for the measured LO current */
    WIZ_POINT_HI,
    WIZ_EDIT_HI,
    WIZ_GO_HI,
    WIZ_RUN_HI,
    WIZ_MEAS_HI,
    WIZ_EDIT_MEAS_HI,
    /* transient: solve inline in WIZ_EDIT_MEAS_HI, then WIZ_SAVE or WIZ_ERR */
    WIZ_SAVE,          /* top "OK  ", bottom "SAVE"; press -> commit */
    WIZ_DONE,          /* "DONE", pops after ~1 s */
    WIZ_ERR,           /* fit rejected, one-digit reason, pops after ~1 s */
    WIZ_ERRW,          /* EEPROM write failed, pops after ~1 s */
};

static uint8_t wiz_state;
static bool wiz_active;       /* false once refused, aborted or finished */
static uint16_t wiz_point_lo; /* LO setpoint, mA */
static uint16_t wiz_point_hi; /* HI setpoint, mA */
static uint16_t wiz_meas_lo;  /* DMM reading at the LO point, mA */
static uint16_t wiz_meas_hi;  /* DMM reading at the HI point, mA */
static uint32_t wiz_fit_slope;  /* solver output, staged for the commit */
static int32_t  wiz_fit_offset;
static uint8_t wiz_result;    /* cal_result_t of the last solve */
static uint16_t wiz_settle;   /* ticks since the load was applied */
static uint16_t wiz_inactive; /* ticks since the last user event */
static uint8_t wiz_pop;       /* ticks left before a result screen pops */
static NumericEdit wiz_edit;  /* rebuilt for every editor use */

/* Full abort: load off (user reason), transient override cleared, back to
   the CAL hub. Idempotent: a wizard that never started or already aborted
   does nothing. */
static void wiz_abort(void)
{
    if (!wiz_active) return;
    wiz_active = 0;
    /* Disable = load off first, then the override cleared; never the
       reverse order (rule in the calibration plan). Clear a stale
       DISABLE_ERROR (the RUN LED would otherwise blink forever), but
       preserve DISABLE_CUTOFF so the user can still see why the run
       ended. */
    if (load_active || load_disable_reason == DISABLE_ERROR) {
        load_disable(DISABLE_USER);
    }
    cal_active = 0;
    ui_pop_item();
}

/* Draw the screen of the current state. The editor states draw themselves
   through ui_edit_value_internal(); top captions are padded to 4 chars. */
static void wiz_render(const cal_channel_t *channel)
{
    bool lo = (wiz_state <= WIZ_MEAS_LO);
    switch (wiz_state) {
    case WIZ_POINT_LO: case WIZ_POINT_HI:
    case WIZ_GO_LO: case WIZ_GO_HI:
    case WIZ_RUN_LO: case WIZ_RUN_HI:
    case WIZ_MEAS_LO: case WIZ_MEAS_HI:
        ui_text(lo ? "LO  " : "HI  ", DP_TOP);
        if (wiz_state == WIZ_GO_LO || wiz_state == WIZ_GO_HI) {
            ui_text("GO ", DP_BOT);
        } else if (wiz_state == WIZ_MEAS_LO || wiz_state == WIZ_MEAS_HI) {
            ui_text("RDY", DP_BOT);
        } else {
            ui_number(lo ? wiz_point_lo : wiz_point_hi,
                      channel->dot_offset, DP_BOT);
        }
        if (wiz_state == WIZ_RUN_LO || wiz_state == WIZ_RUN_HI ||
            wiz_state == WIZ_MEAS_LO || wiz_state == WIZ_MEAS_HI) {
            ui_set_display_mode(DISP_MODE_DIM, DP_TOP);
            ui_set_display_mode(DISP_MODE_BLINK, DP_BOT);
        } else {
            ui_set_display_mode(DISP_MODE_BRIGHT, DP_TOP);
            ui_set_display_mode(DISP_MODE_BRIGHT, DP_BOT);
        }
        ui_leds(channel->led);
        break;
    case WIZ_SAVE:
        ui_text("OK  ", DP_TOP);
        ui_text("SAVE", DP_BOT);
        ui_set_display_mode(DISP_MODE_BRIGHT, DP_TOP);
        ui_set_display_mode(DISP_MODE_BRIGHT, DP_BOT);
        ui_leds(channel->led);
        break;
    case WIZ_DONE:
        ui_text("DONE", DP_TOP);
        ui_text("   ", DP_BOT);
        ui_set_display_mode(DISP_MODE_BRIGHT, DP_TOP);
        ui_set_display_mode(DISP_MODE_BRIGHT, DP_BOT);
        ui_leds(0);
        break;
    case WIZ_ERR:
        ui_text("ERR ", DP_TOP);
        /* One-digit reason code (cal_result_t + 1); dot_offset 0xFF keeps
           ui_number from drawing a decimal point on the digit. */
        ui_number((uint16_t)(wiz_result + 1), 0xFF, DP_BOT);
        ui_set_display_mode(DISP_MODE_DIM, DP_TOP);
        ui_set_display_mode(DISP_MODE_DIM, DP_BOT);
        ui_leds(0);
        break;
    case WIZ_ERRW:
        ui_text("ERR ", DP_TOP);
        ui_text("WR ", DP_BOT);
        ui_set_display_mode(DISP_MODE_DIM, DP_TOP);
        ui_set_display_mode(DISP_MODE_DIM, DP_BOT);
        ui_leds(0);
        break;
    }
}

/* On-device two-point current calibration wizard.
 *
 * Gestures (user documentation is Phase 3):
 *   POINT  bottom shows the setpoint; the encoder button opens the inline
 *          editor for it.
 *   GO     bottom shows "GO "; the encoder button applies the setpoint and
 *          starts the load. The GO step exists to disambiguate "press again
 *          to run" from "press to edit": POINT and GO would otherwise both
 *          react to a press with a different action.
 *   RUN    the load settles for CAL_SETTLE_TICKS while the bottom display
 *          blinks, then the regulation gate decides whether a measurement
 *          can be taken.
 *   MEAS   bottom shows "RDY"; the encoder button opens the editor prefilled
 *          with the setpoint; enter the current measured on the external
 *          meter.
 *   SAVE   after the HI point the fit is computed; OK/SAVE asks for one
 *          final press that writes the EEPROM.
 *   The RUN button aborts from every state without saving.
 */
void ui_cal_wizard(uint8_t event, const MenuItem *item)
{
    const cal_channel_t *channel = (const cal_channel_t*)item->data;
    bool editing = (wiz_state == WIZ_EDIT_LO || wiz_state == WIZ_EDIT_MEAS_LO ||
                    wiz_state == WIZ_EDIT_HI || wiz_state == WIZ_EDIT_MEAS_HI);
    ui_edit_result_t edit_result;

    if (event == EVENT_PREVIEW) {
        /* The CAL hub shows the caption on the top display; leave the
           bottom display empty while the wizard is only being previewed. */
        ui_text("   ", DP_BOT);
        return;
    }

    if (event == EVENT_ENTER) {
        /* Refuse to start while the load is running or an error is present:
           pop back immediately, so the press looks ignored. */
        if (load_active || error != ERROR_NONE) {
            ui_pop_item();
            return;
        }
        wiz_active = 1;
        wiz_state = WIZ_POINT_LO;
        /* Prefill the points: stored values when the channel is already
           calibrated, otherwise the descriptor defaults. Clamp into the
           editor range so a damaged block cannot produce nonsense. */
        calibration_get_points(&wiz_point_lo, &wiz_point_hi);
        if (wiz_point_lo < channel->min) wiz_point_lo = channel->min;
        if (wiz_point_lo > channel->max) wiz_point_lo = channel->max;
        if (wiz_point_hi < channel->min) wiz_point_hi = channel->min;
        if (wiz_point_hi > channel->max) wiz_point_hi = channel->max;
        wiz_meas_lo = 0;
        wiz_meas_hi = 0;
        wiz_settle = 0;
        wiz_inactive = 0;
        wiz_pop = 0;
        wiz_render(channel);
        return;
    }

    if (!wiz_active) return; /* inert: entry was refused or we are done */

    /* Every error aborts immediately. This also covers the error menu being
       pushed over the wizard without notice: the wizard receives no events
       while menu_error is on top of it and aborts on the return below. */
    if (error != ERROR_NONE) {
        wiz_abort();
        return;
    }

    /* Pop (return of the error menu pushed above us) aborts from every
       state; the RUN button aborts from every state except the editor
       states, where it belongs to the editor and only cancels the edit. */
    if (event == EVENT_RETURN) {
        wiz_abort();
        return;
    }
    if (event == EVENT_RUN_BUTTON && !editing) {
        wiz_abort();
        return;
    }

    /* Inactivity timeout in every state; any user event resets the counter. */
    if (event == EVENT_TIMER) {
        if (++wiz_inactive >= CAL_TIMEOUT_TICKS) {
            wiz_abort();
            return;
        }
    } else {
        wiz_inactive = 0;
    }

    switch (wiz_state) {

    case WIZ_POINT_LO:
    case WIZ_POINT_HI:
        if (event == EVENT_ENCODER_BUTTON) {
            bool lo = (wiz_state == WIZ_POINT_LO);
            /* Rebuild the editor for this use (min/max/dot from the
               descriptor). The edited variable is a wizard-local, never a
               settings field, so ui_pop_item()'s settings_update() cannot
               persist wizard state. */
            wiz_edit.var = lo ? &wiz_point_lo : &wiz_point_hi;
            wiz_edit.min = channel->min;
            wiz_edit.max = channel->max;
            wiz_edit.dot_offset = channel->dot_offset;
            wiz_state = lo ? WIZ_EDIT_LO : WIZ_EDIT_HI;
            ui_text(lo ? "LO  " : "HI  ", DP_TOP);
            ui_edit_value_internal(EVENT_ENTER, &wiz_edit, channel->led);
        }
        break;

    case WIZ_EDIT_LO:
    case WIZ_EDIT_HI:
        edit_result = ui_edit_value_internal(event, &wiz_edit, channel->led);
        if (edit_result == UI_EDIT_COMMIT) {
            /* Commit advances to the GO screen with the entered value. */
            wiz_state = (wiz_state == WIZ_EDIT_LO) ? WIZ_GO_LO : WIZ_GO_HI;
            wiz_render(channel);
        } else if (edit_result == UI_EDIT_CANCEL) {
            wiz_state = (wiz_state == WIZ_EDIT_LO) ? WIZ_POINT_LO : WIZ_POINT_HI;
            wiz_render(channel);
        }
        break;

    case WIZ_GO_LO:
    case WIZ_GO_HI:
        if (event == EVENT_ENCODER_BUTTON) {
            bool lo = (wiz_state == WIZ_GO_LO);
            /* The HI point must stay above the LO point; refuse to run
               otherwise (the solver would reject the fit anyway). */
            if (!lo && wiz_point_hi <= wiz_point_lo) break;
            /* Enable: override first, load second; never the reverse.
               Clear any stale error from the previous ADC cycle (load was off). */
            error = ERROR_NONE;
            cal_active = 1;
            cal_setpoint = lo ? wiz_point_lo : wiz_point_hi;
            load_enable();
            wiz_settle = 0;
            wiz_state = lo ? WIZ_RUN_LO : WIZ_RUN_HI;
            wiz_render(channel);
        }
        break;

    case WIZ_RUN_LO:
    case WIZ_RUN_HI:
        if (event == EVENT_TIMER) {
            wiz_settle++;
            if (wiz_settle >= CAL_SETTLE_TICKS) {
                /* Regulation gate: only a regulated run at exactly the
                   calibration setpoint is a usable measurement; anything
                   else (weak PSU, cutoff, ...) aborts and does not offer
                   the editor. */
                if (load_active && load_regulated && current_setpoint == cal_setpoint) {
                    wiz_state = (wiz_state == WIZ_RUN_LO) ? WIZ_MEAS_LO : WIZ_MEAS_HI;
                    wiz_render(channel);
                } else {
                    wiz_abort();
                    return;
                }
            }
        }
        break;

    case WIZ_MEAS_LO:
    case WIZ_MEAS_HI:
        if (event == EVENT_ENCODER_BUTTON) {
            bool lo = (wiz_state == WIZ_MEAS_LO);
            /* Editor prefilled with the setpoint; the user corrects it to
               the reading on the external meter. */
            uint16_t point = lo ? wiz_point_lo : wiz_point_hi;
            if (lo) wiz_meas_lo = point; else wiz_meas_hi = point;
            wiz_edit.var = lo ? &wiz_meas_lo : &wiz_meas_hi;
            wiz_edit.min = channel->min;
            wiz_edit.max = channel->max;
            wiz_edit.dot_offset = channel->dot_offset;
            wiz_state = lo ? WIZ_EDIT_MEAS_LO : WIZ_EDIT_MEAS_HI;
            ui_text(lo ? "LO  " : "HI  ", DP_TOP);
            ui_edit_value_internal(EVENT_ENTER, &wiz_edit, channel->led);
        }
        break;

    case WIZ_EDIT_MEAS_LO:
    case WIZ_EDIT_MEAS_HI:
        edit_result = ui_edit_value_internal(event, &wiz_edit, channel->led);
        if (edit_result == UI_EDIT_COMMIT) {
            if (wiz_state == WIZ_EDIT_MEAS_LO) {
                wiz_state = WIZ_POINT_HI;
                wiz_render(channel);
            } else {
                /* The load comes off before anything is computed or
                   stored; the fit is only applied by the commit below. */
                load_disable(DISABLE_USER);
                cal_active = 0;
                wiz_result = (uint8_t)calibration_solve(wiz_point_lo, wiz_point_hi,
                                                        wiz_meas_lo, wiz_meas_hi,
                                                        &wiz_fit_slope, &wiz_fit_offset);
                wiz_pop = 0;
                wiz_state = (wiz_result == CAL_ERR_NONE) ? WIZ_SAVE : WIZ_ERR;
                wiz_render(channel);
            }
        } else if (edit_result == UI_EDIT_CANCEL) {
            wiz_state = (wiz_state == WIZ_EDIT_MEAS_LO) ? WIZ_MEAS_LO : WIZ_MEAS_HI;
            wiz_render(channel);
        }
        break;

    case WIZ_SAVE:
        if (event == EVENT_ENCODER_BUTTON) {
            /* The load is already off (disabled when the fit was computed);
               a write failure leaves the RAM calibration unchanged, which
               calibration_commit_current() guarantees. */
            if (calibration_commit_current(wiz_point_lo, wiz_point_hi,
                                           wiz_fit_slope, wiz_fit_offset)) {
                wiz_state = WIZ_DONE;
            } else {
                wiz_state = WIZ_ERRW;
            }
            wiz_pop = 0;
            wiz_render(channel);
        }
        break;

    case WIZ_DONE:
    case WIZ_ERR:
    case WIZ_ERRW:
        if (event == EVENT_TIMER) {
            if (++wiz_pop >= 100) { /* ~1 s at 100 Hz */
                wiz_active = 0;
                ui_pop_item();
            }
        }
        break;
    }
}

/* Restore the compile-time calibration defaults (menu item DEF). Stage 1 is
   the confirmation prompt ("DEF "/"SURE"), the encoder press performs the
   reset; RUN cancels. Refused while the load is running. */
void ui_cal_reset(uint8_t event, const MenuItem *item)
{
    static bool confirmed;
    static uint8_t pop_timer;
    (void) item; // unused

    if (event == EVENT_PREVIEW) {
        /* The CAL hub shows the caption on the top display; leave the
           bottom display empty while this item is only being previewed. */
        ui_text("   ", DP_BOT);
        return;
    }
    if (event == EVENT_ENTER) {
        confirmed = 0;
        pop_timer = 0;
        ui_text("DEF ", DP_TOP);
        ui_text("SURE", DP_BOT);
        ui_set_display_mode(DISP_MODE_BRIGHT, DP_TOP);
        ui_set_display_mode(DISP_MODE_BRIGHT, DP_BOT);
        ui_leds(0);
        return;
    }
    /* The error menu was pushed above us and popped back: leave. */
    if (event == EVENT_RETURN || event == EVENT_RUN_BUTTON) {
        ui_pop_item();
        return;
    }
    if (event == EVENT_ENCODER_BUTTON) {
        if (confirmed) return; /* result screen: ignore further presses */
        if (load_active) {
            /* Never erase the calibration while the load is running. */
            ui_text("ON ", DP_BOT);
        } else {
            confirmed = 1;
            if (calibration_reset()) {
                ui_text("DONE", DP_TOP);
            } else {
                ui_text("ERR ", DP_TOP);
            }
            ui_text("   ", DP_BOT);
            ui_leds(0);
            pop_timer = 100; /* show the result for ~1 s */
        }
        return;
    }
    if (event == EVENT_TIMER && pop_timer) {
        if (--pop_timer == 0) ui_pop_item();
    }
}

void ui_activate_load(void)
{
    /* A pending calibration run must not survive the menu being replaced
       (e.g. by the UART run command). */
    cal_active = 0;
    if (!load_active) {
        //First return to main menu and then push the special "active" menu item.
        while (menu_stack_head) {
            ui_pop_item();
        }
        ui_push_item(&menu_run);
    }
}

void ui_disable_load(void)
{
    load_disable(DISABLE_USER);
    /* A running calibration must not survive an external stop either. */
    cal_active = 0;
    //Return to main menu, removing the "active" menu item and any subitems from stack
    while (menu_stack_head) {
        ui_pop_item();
    }
}

//TODO: Correctly handle bouncing encoder
void ui_encoder_irq(void) __interrupt(ITC_IRQ_PORTB)
{
    static uint8_t _encoder_dir = 0xFF;
    uint8_t cur = (GPIOB->IDR >> 4) & 3;
    if (cur == 0) {
        if (_encoder_dir == 2) {
            encoder_val++;
        } else if (_encoder_dir == 1) {
            encoder_val--;
        }
    }
    _encoder_dir = cur;
}

void ui_button_irq(void) __interrupt(ITC_IRQ_PORTC)
{
    static uint8_t last_input_values = 0xFF;

    uint8_t input_values = last_input_values & ~GPIOC->IDR; // store changes (H->L) for buttons
    encoder_pressed |= input_values & PINC_ENC_P;
    run_pressed |= input_values & PINC_RUN_P;

    last_input_values = GPIOC->IDR;
}
