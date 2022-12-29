
#include <stdio.h>
#include <string.h>

#include "main.h"

#include "machine.h"
#include "canmsg.h"
#include "version.h"
#include "analog.h"
#include "app_btl.h"

#define LED_SLOW_SEMI_PERIOD                  20 /* in quanti di 10ms */
#define LED_FAST_SEMI_PERIOD                  5  /* in quanti di 10ms */
#define TEMP_LIMIT                            80 /* gradi C */

extern volatile uint8_t tick_10ms;

uint16_t VirtAddVarTab[NumbOfVar];

static void MachineMangeError(machine_status *machine)
{
	machine->enable_power = 0;
	machine->switch_on = 0;

	MachineFault();
}


static void ParamsVersion(void)
{
	uint16_t ver = 0;

	if (EE_ReadVariable(FLASH_ADDR_PARAMS_VER, &ver) != 0)
		ver = 0;

	if (ver != PARAMS_VER && ver != 0) {
		/* allo stato attuale non ci sono aggiornamenti dei parametri, da implementare se mai qui */
	}
}


static void MachineInit(machine_status *machine)
{
	uint16_t j;

	memset(&machine, 0, sizeof(machine));

	/* e2prom emul */
	memset(VirtAddVarTab, 0, sizeof(VirtAddVarTab));
	j = 0;
	VirtAddVarTab[j++] = FLASH_ADDR_PARAMS_VER;
	VirtAddVarTab[j++] = FLASH_ADDR_CANID_H;
	VirtAddVarTab[j++] = FLASH_ADDR_CANID_L;
	VirtAddVarTab[j++] = FLASH_ADDR_SPEED_ID;
	VirtAddVarTab[j++] = FLASH_ADDR_OUTPUT_EN_H;
	VirtAddVarTab[j++] = FLASH_ADDR_OUTPUT_EN_L;
	VirtAddVarTab[j++] = FLASH_ADDR_CANID_SEND_H;
	VirtAddVarTab[j++] = FLASH_ADDR_CANID_SEND_L;
	VirtAddVarTab[j++] = FLASH_ADDR_CANID_SEND_OFFS_H;
	VirtAddVarTab[j++] = FLASH_ADDR_CANID_SEND_OFFS_L;
	VirtAddVarTab[j++] = FLASH_ADDR_CANID_REC_OFFS_H;
	VirtAddVarTab[j++] = FLASH_ADDR_CANID_REC_OFFS_L;

	FLASH_Unlock();
	EE_Init();
	FLASH_Lock();

	/* gestiene cambio versione parametri */
	ParamsVersion();

	/* inizializzazione lettura analogiche */
	AnalogInit();

	/* inizializzazione CAN bus */
	CanMsgInit();
}


static void UpdateCheck(machine_status *machine)
{
	app_btl *share_app = (app_btl *)APP_BTL_SHARE_ADDR;

	if (machine->bootloader) {
		/* avviso al bootloader dell'aggiornamento e salto */
		share_app->upgrade = 1;
		MachineMangeError(machine);
		HAL_Delay(2);

		HAL_NVIC_SystemReset();
		while (1) {
			/* a vita */
		}
	}
}


static void Logic(machine_status *machine)
{
	/* check della temperatura */
	if (machine->t_a >= TEMP_LIMIT) {
		machine->error_temp_sens_1 = 1;
	}
	else {
		machine->error_temp_sens_1 = 0;
	}
	if (machine->t_b >= TEMP_LIMIT) {
		machine->error_temp_sens_2 = 1;
	}
	else {
		machine->error_temp_sens_2 = 0;
	}

	/* lettura ingressi digitali */
	if (HAL_GPIO_ReadPin(TH_micro_GPIO_Port, TH_micro_Pin) == GPIO_PIN_RESET) {
		machine->error_th = 1;
		machine->enable_power = 0; /* togliere potenza */
	}

	if (HAL_GPIO_ReadPin(Overcurrent_micro_GPIO_Port, Overcurrent_micro_Pin) == GPIO_PIN_RESET) {
		machine->error_overcurrent = 1;
		machine->enable_power = 0; /* togliere potenza */
	}

	if (HAL_GPIO_ReadPin(OV_UV_micro_GPIO_Port, OV_UV_micro_Pin) == GPIO_PIN_RESET) {
		machine->error_ov_uv = 1;
	}
	else {
		machine->error_ov_uv = 0;
	}

	/* comando delle uscite */
	if (machine->enable_power) {
		HAL_GPIO_WritePin(ENABLE_POWER_GPIO_Port, ENABLE_POWER_Pin, GPIO_PIN_SET);
	}
	else {
		HAL_GPIO_WritePin(ENABLE_POWER_GPIO_Port, ENABLE_POWER_Pin, GPIO_PIN_RESET);
	}

	if (machine->switch_on) {
		HAL_GPIO_WritePin(SWITCH_ON_micro_GPIO_Port, SWITCH_ON_micro_Pin, GPIO_PIN_SET);
	}
	else {
		HAL_GPIO_WritePin(SWITCH_ON_micro_GPIO_Port, SWITCH_ON_micro_Pin, GPIO_PIN_RESET);
	}
}


static void Leds(machine_status *machine) /* si giunge qui ongi 10ms */
{
	static uint32_t led_period = 0;

	if (machine->enable_power) {
		HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
	}
	else {
		HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
	}

	if (machine->error_overcurrent) {
		HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
	}
	else if (machine->error_temp_sens_1 || machine->error_temp_sens_2) {
		led_period++;
		if (led_period > LED_SLOW_SEMI_PERIOD) {
			led_period = 0;
			HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
		}
	}
	else if (machine->error_ov_uv) {
		led_period++;
		if (led_period > LED_FAST_SEMI_PERIOD) {
			led_period = 0;
			HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
		}
	}
	else {
		led_period = 0;
		HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
	}
}


void MachineLogic(void)
{
	uint8_t tick;
	machine_status machine;

	/* caricamento configurazione e valori salvata/default */
	MachineInit(&machine);
	Logic(&machine);

	/* ciclo macchina */
	tick = 0;
	for (;;) {
		do {
			if (CanMsgManager(tick, &machine) != 0) {
				MachineMangeError(&machine);
			}
			tick = 0;
		} while (tick_10ms == 0);
		tick = 1;

		tick_10ms = 0;

		AnalogManager(&machine);

		Logic(&machine);

		Leds(&machine);

		UpdateCheck(&machine);
	}
}


void MachineFault(void)
{
	HAL_GPIO_WritePin(ENABLE_POWER_GPIO_Port, ENABLE_POWER_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(SWITCH_ON_micro_GPIO_Port, SWITCH_ON_micro_Pin, GPIO_PIN_RESET);
}
