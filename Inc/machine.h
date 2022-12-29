

#ifndef __MACHINE_H__
#define __MACHINE_H__

#include <stdint.h>

#include "main.h"

#include "eeprom.h"

// versione parametri
#define PARAMS_VER                     0x0001

// flash address
#define FLASH_ADDR_PARAMS_VER          1
#define FLASH_ADDR_CANID_H             6
#define FLASH_ADDR_CANID_L             7
#define FLASH_ADDR_SPEED_ID            9
#define FLASH_ADDR_OUTPUT_EN_H         10
#define FLASH_ADDR_OUTPUT_EN_L         11
#define FLASH_ADDR_CANID_SEND_H        12
#define FLASH_ADDR_CANID_SEND_L        13
#define FLASH_ADDR_CANID_SEND_OFFS_H   14
#define FLASH_ADDR_CANID_SEND_OFFS_L   15
#define FLASH_ADDR_CANID_REC_OFFS_H    16
#define FLASH_ADDR_CANID_REC_OFFS_L    17
/* se si aggiungono ellementi MODIFICARE: NumbOfVar */

#if NumbOfVar < FLASH_ADDR_CANID_REC_OFFS_L
# error "Dimensione errata di NumbOfVar"
#endif

typedef struct  {
	uint16_t bootloader         :1; /* avvio bootloader */

	uint16_t error_th           :1; /* errore per segnalazione del th_micro */
	uint16_t error_overcurrent  :1; /* errore sovracorrente */
	uint16_t error_ov_uv        :1; /* errore over/under voltage */
	uint16_t error_temp_sens_1  :1; /* errore sovratemperatura sensore 1 */
	uint16_t error_temp_sens_2  :1; /* errore sovratemperatura sensore 1 */

	uint16_t enable_power       :1; /* abilita/disabilita uscita di potenza */
	uint16_t switch_on          :1; /* abilita/disabilita lo switch on */

	uint32_t i;                    /* I out */
	uint32_t t_a;                  /* T ntc pwm 1-2 */
	uint32_t t_b;                  /* T ntc pwm 1-2 */
} machine_status;

void MachineLogic(void);
void MachineFault(void);

#endif
