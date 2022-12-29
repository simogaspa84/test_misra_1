
#ifndef __APP_BTL_H__
#define __APP_BTL_H__

#include <stdint.h>

#define APP_BTL_SHARE_ADDR      0x20000000
#define APP_BTL_HEAD_CODE       0xC057
#define APP_BTL_TAIL_CODE       0x61AD
#define APP_BTL_VER             1
#define APP_BTL_AREA_LIM        64      // limite dimensione della struct app_btl

#define APP_BTL_APP_KEY         0xAB01  // chiave di verifica per l'app

typedef struct {
	uint16_t head_code;      // codice di riconoscimento area
	uint8_t offset;          // offeset dove si trova tail_code
	uint8_t ver;             // versione area condivisa ovvero di questa struttura

	// dati bootloader
	uint16_t btl_v_maj;      // bootloader major
	uint16_t btl_v_min;      // bootloader minor
	uint16_t btl_v_patch;    // bootloader patch
	int8_t brd_name[20];     // nome scheda

	// dati condivisi/comunicazione fra app e bootloader
	uint16_t code_app;       // codice di check per l'applicazione
	uint16_t upgrade  :1;    // se 1 avvio dell'aggiornamento

	// free
	uint16_t dummy    :15;
	uint16_t fool[12];

	uint16_t tail_code;      // codice di riconoscimento area
} app_btl;


#endif
