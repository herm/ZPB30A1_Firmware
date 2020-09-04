#include "settings.h"
#include "eeprom.h"
#include "timer.h"
#include "config.h"
settings_t settings;

/* Note: The checksum is placed after the data so when the settings size grows
   The checksum automatically becomes invalid. */
//TODO: Seems to be broken. Needs testing.
static uint8_t settings_calc_checksum(uint8_t *data, uint16_t size)
{
    uint16_t i;
    uint8_t checksum = 0x55;
    for (i = 0; i < size; i++)
    {
        checksum ^= *data++;
    }
    return checksum;
}

void settings_init()
{
    uint16_t addr;
    uint8_t *data = (uint8_t*)(&settings);
    for (addr = 0; addr < sizeof(settings); addr++)
    {
        data[addr] = eeprom_read8(addr);
    }
    uint8_t checksum = eeprom_read8(sizeof(settings));
    if (checksum != settings_calc_checksum(data, sizeof(settings))) {
        // Invalid checksum => initialize default values
        settings.mode = MODE_CC;
        settings.setpoints[MODE_CC] = 150;
        settings.setpoints[MODE_CW] = 30000;
        settings.setpoints[MODE_CR] = 50000;
        settings.setpoints[MODE_CV] = 10000;
        settings.beeper_enabled = 1;
        settings.cutoff_enabled = 0;
        settings.cutoff_voltage = 3300;
        settings.current_limit = CUR_MAX;
        settings.max_power_action = MAX_P_LIM;
    }
}

void settings_update()
{
    uint16_t addr;
    uint8_t *data = (uint8_t*)(&settings);
    for (addr = 0; addr < sizeof(settings); addr++)
    {
        eeprom_write8(addr, data[addr]);
    }
    uint8_t checksum = settings_calc_checksum(data, sizeof(settings));
    eeprom_write8(sizeof(settings), checksum);
    /* TODO: Wrting the eeprom can take several 10s of milliseconds. This leads
    to timer overflow errors. As eeprom writes only happen while the load is
    inactive this should be no problem and we simply delete the error flags.
    However in the future this write function could be split into smalller parts
    */
    systick_flag &= ~(SYSTICK_OVERFLOW|SYSTICK_COUNT);
}
