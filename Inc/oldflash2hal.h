#ifndef __OLDFLASH2HAL_H
#define __OLDFLASH2HAL_H

#include "stm32f1xx_hal.h"

#define PAGE_REAL_NUM           2

/** 
  * @brief  FLASH Status
  */
typedef enum {
	FLASH_NONE = 0,
    FLASH_BUSY = 1,
    FLASH_ERR_WRP,
    FLASH_ERR_PROGRAM,
    FLASH_COMPLETE,
    FLASH_TIMEOUT
} FLASH_Status;


/* FLASH Memory Programming functions *****************************************/
void FLASH_Unlock(void);
void FLASH_Lock(void);
FLASH_Status FLASH_ErasePage(uint32_t Page_Address);
FLASH_Status FLASH_ProgramWord(uint32_t Address, uint32_t Data);
FLASH_Status FLASH_ProgramHalfWord(uint32_t Address, uint16_t Data);


#endif
