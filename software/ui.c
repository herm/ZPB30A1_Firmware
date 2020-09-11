#include "ui.h"
#include "tm1650.h"
#include "timer.h"
#include "eeprom.h"
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

typedef enum {
    /* Bitmask:
    Bit 0: Brightness
    Bit 1: Blink fast
    Bit 2: Blink slow
    Setting bit 1 and 2 at the same time results in undefinded behaviour.
    */
    DISP_MODE_BRIGHT       = 0b000,
    DISP_MODE_DIM          = 0b001,
    DISP_MODE_BLINK_FAST   = 0b010,
    DISP_MODE_BLINK_SLOW   = 0b100,
} display_mode_t;

static volatile int8_t encoder_val = 0;
static volatile bool encoder_pressed = 0;
static volatile bool run_pressed = 0;
static uint8_t display_mode[] = {DISP_MODE_BRIGHT, DISP_MODE_BRIGHT};

#define MENU_STACK_DEPTH 5
static const MenuItem *menu_stack[MENU_STACK_DEPTH];
static uint8_t menu_subitem_index[MENU_STACK_DEPTH];
static uint8_t menu_stack_head = 0;

#define current_item menu_stack[menu_stack_head]
#define current_subitem_index menu_subitem_index[menu_stack_head]
#define current_subitem current_item->subitems[current_subitem_index]

static void ui_text(char text[], uint8_t display);
static void ui_number(uint16_t num, uint8_t dot, uint8_t display);
static void ui_push_item(MenuItem *item);
static void ui_pop_item();

static void ui_set_display_mode(display_mode_t mode, display_t disp)
{
    display_mode[disp] = mode;
    if (mode & DISP_MODE_DIM) {
        disp_brightness(BRIGHTNESS_DIM, disp);
    } else {
        disp_brightness(BRIGHTNESS_BRIGHT, disp);
    }
}

void ui_init()
{
    ui_set_display_mode(DISP_MODE_BRIGHT, DP_TOP);
    ui_set_display_mode(DISP_MODE_BRIGHT, DP_BOT);
    menu_stack_head = 0;
    // Reset all button events which might have triggered before the system was fully initialized.
    run_pressed = 0; 
    encoder_val = 0;
    encoder_pressed = 0;
    ui_push_item(&menu_main);
}

static void ui_blink(uint8_t mode)
{
    for (uint8_t i=0; i<2; i++)
    {
        if (display_mode[i] & mode) {
            ui_set_display_mode(display_mode[i] ^ DISP_MODE_DIM, i);
        }
    }
}

static void ui_timer_blink()
{
    static uint16_t slow_timer = 0;
    static uint16_t fast_timer = 0;
    slow_timer++;
    if (slow_timer == F_SYSTICK/F_DISPLAY_BLINK_SLOW) {
        slow_timer = 0;
        ui_blink(DISP_MODE_BLINK_SLOW);
    }

    fast_timer++;
    if (fast_timer == F_SYSTICK/F_DISPLAY_BLINK_FAST) {
        fast_timer = 0;
        ui_blink(DISP_MODE_BLINK_FAST);
    }
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
    load_disable(DISABLE_ERROR);
    ui_text("ERR", DP_BOT);
    ui_text(msgs[error], DP_TOP);
    ui_set_display_mode(DISP_MODE_DIM, DP_TOP);
    ui_set_display_mode(DISP_MODE_DIM, DP_BOT);
    if (event == EVENT_ENCODER_BUTTON) {
        ui_pop_item();
        error = ERROR_NONE;
    }
}


static void ui_timer_beeper()
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

void ui_timer()
{
    ui_timer_blink();
    ui_timer_beeper();
    if (error && current_item != &menu_error) {
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

static void ui_text(const char *text, uint8_t display)
{
    for (uint8_t i=0; i<4; i++) {
        if (display == DP_TOP || i != 3) disp_char(i, text[i], 0, display);
    }
}

static void ui_number(uint16_t num, uint8_t dot, uint8_t display)
{
    uint16_t maximum = (display == DP_TOP)?10000:1000;
    uint16_t digits = (display == DP_TOP)?4:3;
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

static void ui_leds(uint8_t leds)
{
    /* TODO: Blink run LED when load is out of regulation. */
    uint8_t run_led = load_active ? LED_RUN : 0;
    disp_leds(leds | run_led);
}

static uint8_t ui_num_subitem(const MenuItem *item)
{
    uint8_t max_ = 0;
    const MenuItem **p = item->subitems;
    while (*p++) max_++;
    return max_;
}

// Menu item handlers
static void ui_push_item(const MenuItem *item)
{
    if (menu_stack_head != 0 || menu_stack[0] != 0) {
        menu_stack_head++; //First push is for the main menu
    }
    current_item = item;
    current_subitem_index = 0;
    item->handler(EVENT_ENTER, item);
}

static void ui_pop_item()
{
    if (menu_stack_head != 0) {
        menu_stack_head--;
    }
    ui_leds(0);
    settings_update(); //Store changed value to eeprom
    current_item->handler(EVENT_RETURN, current_item);
}

/* This function must only be called for the currently active item. */
static void ui_select(uint8_t event, const MenuItem *item, uint8_t display)
{
    uint8_t display2 = display==DP_TOP?DP_BOT:DP_TOP;
    bool output = event & (EVENT_BITMASK_MENU | EVENT_BITMASK_ENCODER);
    if (event & EVENT_BITMASK_MENU) {
        ui_set_display_mode(DISP_MODE_BLINK_FAST, display);
        ui_set_display_mode(DISP_MODE_DIM, display2);
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
    if (event & EVENT_PREVIEW) {
        // Show nothing in bottom display as preview
        ui_text("   ", DP_BOT);
        return;
    }
    if (event == EVENT_RUN_BUTTON && menu_stack_head == 0)
    {
        //Main menu + Run button => turn on load
        ui_activate_load();
        return;
    }
    ui_select(event, item, DP_TOP);
    if (current_subitem->handler) {
        current_subitem->handler(event | EVENT_PREVIEW, current_subitem);
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
    variable which is set to the child's data element. */
void ui_select_item(uint8_t event, const MenuItem *item)
{
    if (event & EVENT_PREVIEW) {
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
   except setting the secondard display's value and selecting the LEDs. */
void ui_edit_value_internal(uint8_t event, const NumericEdit *edit, uint8_t leds)
{
    static uint16_t value;
    uint8_t display = DP_BOT, display2 = DP_TOP;
    if (event & EVENT_PREVIEW) {
        ui_number(*edit->var, edit->dot_offset, display);
        return;
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

    bool pop = false;

    switch (event) {
    case EVENT_ENTER:
        ui_set_display_mode(DISP_MODE_BRIGHT, display);
        ui_set_display_mode(DISP_MODE_DIM, display2);
        ui_leds(leds | LED_DIGIT1);
        value = *edit->var;
        current_subitem_index = 0;
        break;
    case EVENT_RUN_BUTTON:
        pop = true;
        break;
    case EVENT_ENCODER_BUTTON:
        if (current_subitem_index == 0) {
            current_subitem_index = 1;
            ui_leds(leds | LED_DIGIT2);
        } else {
            *edit->var = value;
            pop = true;
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
    bool output = !pop && event & (EVENT_BITMASK_MENU | EVENT_BITMASK_ENCODER);
    if (output) {
        ui_number(value, edit->dot_offset, display);
    }
    if (pop) {
        // Pop must be the last action to avoid overwriting the display
        ui_pop_item();
    }
}

void ui_edit_value(uint8_t event, const MenuItem *item)
{
    const NumericEdit *edit = item->data;
    ui_edit_value_internal(event, edit, item->value);
}

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
    if (!(event & EVENT_PREVIEW)) ui_text(label, DP_TOP);
    if (edit) ui_edit_value_internal(event, edit, leds);
}

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
                ui_leds(LED_A|LED_V); //Update run led
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
        ui_number(actual_current_setpoint, CUR_DOT_OFFSET, DP_BOT);
    }
}

void ui_active(uint8_t event, const MenuItem *item)
{
    (void) item; //unused
    if (event & EVENT_PREVIEW) return; //Unsupported
    ui_show_values(event);
    if (event == EVENT_RUN_BUTTON ||
        (event == EVENT_RETURN && error != ERROR_NONE)) {
        ui_disable_load();
        return;
    }
    if (event == EVENT_ENTER || event == EVENT_RETURN) {

        ui_set_display_mode(DISP_MODE_DIM, DP_TOP);
        ui_set_display_mode(DISP_MODE_DIM, DP_BOT);
    }

    if (event == EVENT_ENCODER_BUTTON) {
        ui_push_item(&menu_value);
    }
}

void ui_activate_load()
{
    if (!load_active) {
        load_enable();
        //First return to main menu and then push the special "active" menu item.
        while (menu_stack_head) {
            ui_pop_item();
        }
        ui_push_item(&menu_active);
    }
}

void ui_disable_load()
{
    if (load_active) {
        load_disable(DISABLE_USER);
        //Return to main menu, removing the "active" menu item and any subitems from stack
        while (menu_stack_head) {
            ui_pop_item();
        }
    }
}

//TODO: Correctly handle bouncing encoder
void ui_encoder_irq() __interrupt(ITC_IRQ_PORTB)
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

void ui_button_irq() __interrupt(ITC_IRQ_PORTC)
{
    static uint8_t input_values = 0xFF;
    input_values &= ~GPIOC->IDR; // store changes (H->L) for buttons
    encoder_pressed |= input_values & PINC_ENC_P;
    run_pressed |= input_values & PINC_RUN_P;
    input_values = GPIOC->IDR;
}
