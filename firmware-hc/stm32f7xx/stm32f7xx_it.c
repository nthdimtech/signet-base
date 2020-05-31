/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f7xx_it.h"

void Error_Handler();

/******************************************************************************/
/*           Cortex-M7 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
	Error_Handler();
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
	Error_Handler();
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
	Error_Handler();
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
	Error_Handler();
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
	HAL_IncTick();
}

extern PCD_HandleTypeDef hpcd;

void OTG_HS_IRQHandler(void)
{
	HAL_PCD_IRQHandler(&hpcd);
}

void OTG_HS_WKUP_IRQHandler(void)
{
	__HAL_USB_OTG_HS_WAKEUP_EXTI_CLEAR_FLAG();
}
/******************************************************************************/
/* STM32F7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f7xx.s).                    */
/******************************************************************************/

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
