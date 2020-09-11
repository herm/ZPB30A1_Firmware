/*
Remote control over serial
AUTHOR: Tom Haynes (code@tomhaynes.net)
DATE: September 11th, 2020
*/
#include "remote.h"
#include "uart.h"
#include "settings.h"
#include "config.h"
#include "load.h"
#include "timer.h"
#include "ui.h"
#include "menu_items.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef enum {
  WAITING_FOR_COMMAND,
  PROCESSING_COMMAND,
  DEBUG
} receive_state_t;

typedef struct Command_t Command;
typedef void (*remote_callback_func)(uint8_t *data, Command *cmd);

struct Command_t {
  const uint8_t *name;
  const remote_callback_func handler;
  const uint8_t *help;
};



// To add a new command, extend the Commands[] array below. Then add a new
// handler function in the callback section. Most commands will probably update
// the settings object so check out settings.h for details.

//Define new commands here
static const Command Commands[] = {
  //SETMODE [CC|CV|CR|CP]
  { .name = "SETMODE",
    .handler = &remote_setmode,
    .help = "  SETMODE [CC|CV|CR|CP]\n"
            "  Set the operating Mode of the electronic load.\n"
            "  CC = Constant Current\n"
            "  CV = Constant Voltage\n"
            "  CR = Constant Resistance\n"
            "  CP = Constant Power\n"
  },
  //SETI [amperage] #mA 200(CUR_MIN)-10000(CUR_MAX)
  { .name = "SETI",
    .handler = &remote_seti,
    .help = "  SETI [mA]\n"
            "  Set the target amperage for constant current mode.\n"
            //TODO: This should be calculated based on defined constants
            "  mA = Target amperage in milliamps between 200 (0.2A) and 10000 (10A)\n"
  },
  //SETV [voltage] #mV 500(VOLT_MIN)-30000(VOLT_MAX)
  { .name = "SETV",
    .handler = &remote_setv,
    .help = "  SETV [mV]\n"
            "  Set the target voltage for constant voltage mode.\n"
            //TODO: This should be calculated based on defined constants
            "  mV = Target voltage in millivolts between 500 (0.5V) and 30000 (30V)\n"
  },
  //SETR [resistance] #mR 10(R_MIN)-15000(R_MAX)
  { .name = "SETR",
    .handler = &remote_setr,
    .help = "  SETR [mR]\n"
            "  Set the target resistance for constant resistance mode.\n"
            //TODO: This should be calculated based on defined constants
            "  mR = Target resistance in milliohms between 10 (0.01 ohms) and 15000 (15 ohms)\n"
  },
  //SETP [power] #mW 0(POW_MIN)-60000(POW_MAX)
  { .name = "SETP",
    .handler = &remote_setp,
    .help = "  SETP [mW]\n"
            "  Set the target power for constant power mode.\n"
            //TODO: This should be calculated based on defined constants
            "  mW = Target power in milliwatts between 0 and 60000 (60W)\n"
  },
  //SETBEEP [OFF|ON]
  { .name = "SETBEEP",
    .handler = &remote_setbeep,
    .help = "  SETBEEP [OFF|ON]\n"
            "  Enable or disable alarm and notification sounds.\n"
            "  OFF = Turn off alarms and notifications\n"
            "  ON = Turn on alarms and notifications\n"
  },
  //SETCUTOFF [voltage|OFF] #mV 500(VOLT_MIN)-30000(VOLT_MAX)
  { .name = "SETCUTOFF",
    .handler = &remote_setcutoff,
    .help = "  SETCUTOFF [mV|OFF]\n"
            "  Target cutoff voltage. If the voltage falls below target, the load will be disabled.\n"
            //TODO: This should be calculated based on defined constants
            "  mV = Cutoff voltage in millivolts between 500 (0.5V) and 30000 (30V)\n"
            "  OFF = No action will be taken if voltage falls too low\n"
  },
  //SETILIMIT [current] #mA 200(CUR_MIN)-10000(CUR_MAX)
  { .name = "SETILIMIT",
    .handler = &remote_setilimit,
    .help = "  SETILIMIT [mA]\n"
            "  Set current limit. If this current is exceeded, the load will be disabled.\n"
            //TODO: This should be calculated based on defined constants
            "  mA = Amperage limit in milliamps between 200 (0.2A) and 10000 (10A)\n"
  },
  //SETPLIMIT [NOLIMIT|LIMIT]
  { .name = "SETPLIMIT",
    .handler = &remote_setplimit,
    .help = "  SETPLIMIT [NOLIMIT|LIMIT]\n"
            "  Set action to take if maximum power limit is exceeded.\n"
            "  NOLIMIT = No action is taken\n"
            "  LIMIT = Load will be disabled if power limit is exceeded\n"
  },
  //LOAD [ON|OFF]
  { .name = "LOAD",
    .handler = &remote_load,
    .help = "  LOAD [ON|OFF]\n"
            "  Requests the load to activate or deactivate.\n"
            "  ON = Activates the load\n"
            "  OFF = Deactivates the load\n"
            "  NOTE: ON is similar to pressing the run button and will take\n"
            "  you up one menu level. It will only enable the load if you are\n"
            "  at the top menu level. Send multiple LOAD ON commands until you\n"
            "  see the realtime output indicate the load is on.\n"
            "  NOTE: OFF disables the load but does not return you to the menu.\n"
            "  Use the hardware run button if you need to get in to the menu\n"
  }
};

// Define new callback functions here
//SETMODE [CC|CV|CR|CP]
void remote_setmode(uint8_t *data, Command *cmd) {
  if (strcmp(data, "CC") == 0) {
    settings.mode = MODE_CC;
    printf("Mode set to constant current\n");
  } else if (strcmp(data, "CV") == 0) {
    settings.mode = MODE_CV;
    printf("Mode set to constant voltage\n");
  } else if (strcmp(data, "CR") == 0) {
    settings.mode = MODE_CR;
    printf("Mode set to constant resistance\n");
  } else if (strcmp(data, "CP") == 0) {
    settings.mode = MODE_CW;
    printf("Mode set to constant power\n");
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
  }
}

//SETI [amperage] #mA 200(CUR_MIN)-10000(CUR_MAX)
void remote_seti (uint8_t *data, Command *cmd) {
  uint16_t value = atoi(data);
  if (value >= CUR_MIN && value <= CUR_MAX) {
    settings.setpoints[MODE_CC] = value;
    printf("Target current set to %d mA\n", value);
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
    printf("Must be %d <= value <= %d\n", CUR_MIN, CUR_MAX);
  }
}

//SETV [voltage] #mV 500(VOLT_MIN)-30000(VOLT_MAX)
void remote_setv (uint8_t *data, Command *cmd) {
  uint16_t value = atoi(data);
  if (value >= VOLT_MIN && value <= VOLT_MAX) {
    settings.setpoints[MODE_CV] = value;
    printf("Target voltage set to %d mV\n", value);
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
    printf("Must be %d <= value <= %d\n", VOLT_MIN, VOLT_MAX);
  }
}

//SETR [resistance] #mR 10(R_MIN)-15000(R_MAX)
void remote_setr (uint8_t *data, Command *cmd) {
  uint16_t value = atoi(data);
  if (value >= R_MIN && value <= R_MAX) {
    settings.setpoints[MODE_CR] = value;
    printf("Target resistance set to %d mOhm\n", value);
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
    printf("Must be %d <= value <= %d\n", R_MIN, R_MAX);
  }
}

//SETP [power] #mW 0(POW_MIN)-60000(POW_MAX)
//NOTE: value >= POW_MIN throws a compile time warning when POW_MIN = 0
//because value is an unsigned integer so it will always be >= 0. this
//can safely be ignored.
#pragma save
#pragma disable_warning 94
void remote_setp (uint8_t *data, Command *cmd) {
  uint16_t value = atoi(data);
  if (value == 0 && strcmp(data, "0") != 0) {
    // 0 may a valid value but atoi also returns 0 if it doesn't see a number.
    // If the string value of data is not "0" then the int value of value
    // means there was an error in the atoi conversion.
      printf("ERROR Invalid command data \"%s\"\n", data);
      printf("Must be %d <= value <= %d\n", CUR_MIN, CUR_MAX);
  } else if (value >= POW_MIN && value <= POW_MAX) {
    settings.setpoints[MODE_CW] = value;
    printf("Target power set to %d mW\n", value);
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
    printf("Must be %d <= value <= %d\n", CUR_MIN, CUR_MAX);
  }
}
#pragma restore

//SETBEEP [OFF|ON]
void remote_setbeep (uint8_t *data, Command *cmd) {
  if (strcmp(data, "ON") == 0) {
    settings.beeper_enabled = true;
    printf("Beeper enabled\n");
  } else if (strcmp(data, "OFF") == 0) {
    settings.beeper_enabled = false;
    printf("Beeper disabled\n");
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
  }
}

//SETCUTOFF [voltage|OFF] #mV 500(VOLT_MIN)-30000(VOLT_MAX)
void remote_setcutoff (uint8_t *data, Command *cmd) {
  uint16_t value = atoi(data);
  if(strcmp(data, "OFF") == 0) {
    settings.cutoff_enabled = false;
    printf("Voltage cutoff disabled\n");
  } else if (value >= VOLT_MIN && value <= VOLT_MAX) {
    settings.cutoff_voltage = value;
    settings.cutoff_enabled = true;
    printf("Voltage cutoff set to %d mV and enabled\n", value);
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
    printf("Must be %d <= value <= %d\n", VOLT_MIN, VOLT_MAX);
  }
}

//SETILIMIT [current] #mA 200(CUR_MIN)-10000(CUR_MAX)
void remote_setilimit (uint8_t *data, Command *cmd) {
  uint16_t value = atoi(data);
  if (value >= CUR_MIN && value <= CUR_MAX) {
    settings.current_limit = value;
    printf("Current limit set to %d mA\n", value);
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
    printf("Must be %d <= value <= %d\n", CUR_MIN, CUR_MAX);
  }
}

//SETPLIMIT [NOLIMIT|LIMIT]
void remote_setplimit (uint8_t *data, Command *cmd) {
  if (strcmp(data, "LIMIT") == 0) {
    settings.max_power_action = MAX_P_LIM;
    printf("Maximum power limit enabled\n");
  } else if (strcmp(data, "NOLIMIT") == 0) {
    settings.max_power_action = MAX_P_OFF;
    printf("Maximum power limit disabled\n");
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
  }
}

//LOAD [ON|OFF]
void remote_load (uint8_t *data, Command *cmd) {
  (void) cmd; //unused
  uint16_t value = atoi(data);
  if (strcmp(data, "ON") == 0) {
    //load_enable();
    ui_submenu(EVENT_RUN_BUTTON, NULL);
    printf("Load enabled\n");
  } else if (strcmp(data, "OFF") == 0) {
    load_disable(DISABLE_USER);
    printf("Load disabled\n");
  } else if (strcmp(data, "HELP") == 0) {
    printf("%s\n", cmd->help);
  } else {
    printf("ERROR Invalid command data \"%s\"\n", data);
  }
}

#define NUMBER_OF_COMMANDS (sizeof(Commands) / sizeof(Command))
// TODO: Set automatically based on command definitions above
#define MAX_COMMAND_LENGTH 10
// TODO: Set automatically based on command definitions above
#define MIN_COMMAND_LENGTH 4
// TODO: Set automatically based on command definitions above
#define MAX_DATA_LENGTH 7
#define COMMAND_BUFFER_SIZE (MAX_COMMAND_LENGTH + MAX_DATA_LENGTH + 2)

// Set initial state to DEBUG to print start up info and commands.
// Set to WAITING_FOR_COMMAND to skip debug info.
static receive_state_t state = DEBUG;
static uint8_t rx_command[COMMAND_BUFFER_SIZE];

void clear_command_buffer() {
  rx_command[0] = '\0';
}

void append_char_to_command_buffer(char c) {
  size_t len = strlen(rx_command);
	if((len+1) < sizeof(rx_command)) {
		rx_command[len++] = c;
		rx_command[len] = '\0';
	}
}

void remote_init() {
}

void remote_timer() {
  //TODO: Setup pacing for large printf output to avoid Timer errors
  static uint8_t tmp;
  switch (state) {
  case WAITING_FOR_COMMAND:
    while(UART_BUFFER_COUNT(guart_buffer)) {
      UART_BUFFER_RD(guart_buffer,tmp);
      if (tmp == '\r' || tmp == '\n') {
        if (sizeof(rx_command) == 0) {
          //nothing interesting happened
          continue;
        }
        state = PROCESSING_COMMAND;
        break;
      } else {
        append_char_to_command_buffer(tmp);
      }
    }
    break;
  case DEBUG:
    printf("\nSTARTING\n");
    printf("Ignoring input longer that %d characters\n", COMMAND_BUFFER_SIZE);
    printf("Preparing %d available commands:\n", NUMBER_OF_COMMANDS);
    for (size_t i = 0; i < NUMBER_OF_COMMANDS; i++) {
      printf("\n----\n%s\n----\n%s\n", Commands[i].name, Commands[i].help);
    }
    state = WAITING_FOR_COMMAND;
    systick_flag = 0;
    break;
  case PROCESSING_COMMAND:
    //misc command here becasue compile breaks if variable creation is the first thing after a case statement
    printf("\n%s\n", rx_command);
    char * cmd = strtok(rx_command, " ");
    char * data = strtok(NULL, "\0");
    if (strcmp(cmd, "HELP") == 0) {
      clear_command_buffer();
      state = DEBUG;
      break;
    }
    for (size_t c = 0; c <= NUMBER_OF_COMMANDS; c++) {
      if (c == NUMBER_OF_COMMANDS) {
        printf("ERROR Received invalid command \"%s\"\n", cmd);
        printf("HINT Commands are case sensitive. Type HELP for list of commands.\n");
      } else if (strcmp(cmd,Commands[c].name) == 0) {
        if(Commands[c].handler) {
          Commands[c].handler(data, &Commands[c]);
        }
        //printf("%s\n", Commands[c].help);
        break;
      }
    }

    clear_command_buffer();
    state = WAITING_FOR_COMMAND;
    systick_flag = 0;
    break;
  }
}
