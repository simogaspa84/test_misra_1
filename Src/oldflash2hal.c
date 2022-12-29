#include "stm32f1xx_hal.h"

#include "oldflash2hal.h"


static FLASH_Status StatusConvert(HAL_StatusTypeDef old)
{
  FLASH_Status ret = FLASH_NONE;

  switch (old) {
  case HAL_OK:
    ret = FLASH_COMPLETE;
    break;

  case HAL_ERROR:
    ret = FLASH_ERR_PROGRAM;
    break;

  case HAL_BUSY:
    ret = FLASH_BUSY;
    break;

  case HAL_TIMEOUT:
    ret = FLASH_TIMEOUT;
    break;
  }

  return ret;
}

void FLASH_Unlock(void)
{
  HAL_FLASH_Unlock();
}

void FLASH_Lock(void)
{
  HAL_FLASH_Lock();
}

FLASH_Status FLASH_ErasePage(uint32_t Page_Address)
{
  HAL_StatusTypeDef res;
  FLASH_EraseInitTypeDef erase_init;
  uint32_t page_error;

  erase_init.Banks = FLASH_BANK_1;
  erase_init.PageAddress = Page_Address;
  erase_init.NbPages = PAGE_REAL_NUM;
  erase_init.TypeErase = FLASH_TYPEERASE_PAGES;

  res = HAL_FLASHEx_Erase(&erase_init, &page_error);

  return StatusConvert(res);
}

FLASH_Status FLASH_ProgramWord(uint32_t Address, uint32_t Data)
{
  HAL_StatusTypeDef res;

  res = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, Address, Data);

  return StatusConvert(res);
}

FLASH_Status FLASH_ProgramHalfWord(uint32_t Address, uint16_t Data)
{
  HAL_StatusTypeDef res;

  res = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, Address, Data);

  return StatusConvert(res);
}
