#ifndef _UI_H_
#define _UI_H_
#include <stdbool.h>
#include <stdint.h>
#include "menu_items.h"

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

void ui_init();
void ui_timer();

void ui_submenu(uint8_t event, MenuItem *item);
void ui_select_item(uint8_t event, MenuItem *item);
void ui_edit_value(uint8_t event, const MenuItem *item);
void ui_edit_setpoint(uint8_t event, const MenuItem *item);
void ui_active(uint8_t event, const MenuItem *item);
void ui_error_handler(uint8_t event, const MenuItem *item);

void ui_activate_load();
void ui_disable_load();
#endif
