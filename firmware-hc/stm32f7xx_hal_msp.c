/* Includes ------------------------------------------------------------------*/
#include "main.h"

DMA_HandleTypeDef hdma_aes_in;
DMA_HandleTypeDef hdma_aes_out;
extern CRYP_HandleTypeDef hcryp;
extern MMC_HandleTypeDef hmmc1;

/**
  * Initializes the Global MSP.
  */
void HAL_MspInit(void)
{
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_RCC_SYSCFG_CLK_ENABLE();
}

void HAL_CRYP_MspInit(CRYP_HandleTypeDef* hcryp)
{
	if(hcryp->Instance==AES) {
		/* Peripheral clock enable */
		__HAL_RCC_AES_CLK_ENABLE();
		hdma_aes_in.Instance = DMA2_Stream6;
		hdma_aes_in.Init.Channel = DMA_CHANNEL_2;
		hdma_aes_in.Init.Direction = DMA_MEMORY_TO_PERIPH;
		hdma_aes_in.Init.PeriphInc = DMA_PINC_DISABLE;
		hdma_aes_in.Init.MemInc = DMA_MINC_ENABLE;
		hdma_aes_in.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
		hdma_aes_in.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
		hdma_aes_in.Init.Mode = DMA_NORMAL;
		hdma_aes_in.Init.Priority = DMA_PRIORITY_HIGH;
		hdma_aes_in.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
		if (HAL_DMA_Init(&hdma_aes_in) != HAL_OK)
		{
			Error_Handler();
		}
		__HAL_LINKDMA(hcryp, hdmain, hdma_aes_in);

		/* AES_OUT Init */
		hdma_aes_out.Instance = DMA2_Stream5;
		hdma_aes_out.Init.Channel = DMA_CHANNEL_2;
		hdma_aes_out.Init.Direction = DMA_PERIPH_TO_MEMORY;
		hdma_aes_out.Init.PeriphInc = DMA_PINC_DISABLE;
		hdma_aes_out.Init.MemInc = DMA_MINC_ENABLE;
		hdma_aes_out.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
		hdma_aes_out.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
		hdma_aes_out.Init.Mode = DMA_NORMAL;
		hdma_aes_out.Init.Priority = DMA_PRIORITY_LOW;
		hdma_aes_out.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
		if (HAL_DMA_Init(&hdma_aes_out) != HAL_OK)
		{
			Error_Handler();
		}

		__HAL_LINKDMA(hcryp, hdmaout, hdma_aes_out);
	}
}

/**
* @brief CRYP MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param hcryp: CRYP handle pointer
* @retval None
*/
void HAL_CRYP_MspDeInit(CRYP_HandleTypeDef* hcryp)
{
	if(hcryp->Instance==AES) {
		/* Peripheral clock disable */
		__HAL_RCC_AES_CLK_DISABLE();
	}
}

/**
* @brief RNG MSP Initialization
* This function configures the hardware resources used in this example
* @param hrng: RNG handle pointer
* @retval None
*/
void HAL_RNG_MspInit(RNG_HandleTypeDef* hrng)
{
	if(hrng->Instance==RNG) {
		/* Peripheral clock enable */
		__HAL_RCC_RNG_CLK_ENABLE();
	}
}

/**
* @brief RNG MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param hrng: RNG handle pointer
* @retval None
*/
void HAL_RNG_MspDeInit(RNG_HandleTypeDef* hrng)
{
	if(hrng->Instance==RNG) {
		/* Peripheral clock disable */
		__HAL_RCC_RNG_CLK_DISABLE();
	}
}

/**
  * @brief  Initializes the PCD MSP.
  * @param  hpcd: PCD handle
  * @retval None
  */
void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd)
{
	GPIO_InitTypeDef  GPIO_InitStruct;

	if(hpcd->Instance == USB_OTG_FS) {
		/* Configure USB FS GPIOs */
		__HAL_RCC_GPIOA_CLK_ENABLE();

		/* Configure DM DP Pins */
		GPIO_InitStruct.Pin = (GPIO_PIN_11 | GPIO_PIN_12);
		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

		/* Enable USB FS Clock */
		__HAL_RCC_USB_OTG_FS_CLK_ENABLE();

		/* Set USBFS Interrupt priority */
		HAL_NVIC_SetPriority(OTG_FS_IRQn, DEFAULT_INT_PRIORITY, 0);

		/* Enable USBFS Interrupt */
		HAL_NVIC_EnableIRQ(OTG_FS_IRQn);

		if(hpcd->Init.low_power_enable == 1) {
			/* Enable EXTI Line 18 for USB wakeup*/
			__HAL_USB_OTG_FS_WAKEUP_EXTI_CLEAR_FLAG();
			__HAL_USB_OTG_FS_WAKEUP_EXTI_ENABLE_RISING_EDGE();
			__HAL_USB_OTG_FS_WAKEUP_EXTI_ENABLE_IT();

			/* Set EXTI Wakeup Interrupt priority*/
			HAL_NVIC_SetPriority(OTG_FS_WKUP_IRQn, DEFAULT_INT_PRIORITY, 0);

			/* Enable EXTI Interrupt */
			HAL_NVIC_EnableIRQ(OTG_FS_WKUP_IRQn);
		}
	} else if(hpcd->Instance == USB_OTG_HS) {
		/* Enable HS PHY Clock */
		__HAL_RCC_OTGPHYC_CLK_ENABLE();

		/* Enable USB HS Clocks */
		__HAL_RCC_USB_OTG_HS_CLK_ENABLE();

		__HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();

		/* Set USBHS Interrupt to the lowest priority */
		HAL_NVIC_SetPriority(OTG_HS_IRQn, DEFAULT_INT_PRIORITY, 0);

		/* Enable USBHS Interrupt */
		HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

		if(hpcd->Init.low_power_enable == 1) {
			/* Enable EXTI Line 20 for USB wakeup*/
			__HAL_USB_OTG_HS_WAKEUP_EXTI_CLEAR_FLAG();
			__HAL_USB_OTG_HS_WAKEUP_EXTI_ENABLE_RISING_EDGE();
			__HAL_USB_OTG_HS_WAKEUP_EXTI_ENABLE_IT();

			/* Set EXTI Wakeup Interrupt priority*/
			HAL_NVIC_SetPriority(OTG_HS_WKUP_IRQn, 0, 0);

			/* Enable EXTI Interrupt */
			HAL_NVIC_EnableIRQ(OTG_HS_WKUP_IRQn);
		}
	}
}

/**
  * @brief  De-Initializes the PCD MSP.
  * @param  hpcd: PCD handle
  * @retval None
  */
void HAL_PCD_MspDeInit(PCD_HandleTypeDef *hpcd)
{
	if(hpcd->Instance == USB_OTG_FS) {
		/* Disable USB FS Clock */
		__HAL_RCC_USB_OTG_FS_CLK_DISABLE();
		__HAL_RCC_SYSCFG_CLK_DISABLE();
	} else if(hpcd->Instance == USB_OTG_HS) {
		/* Disable USB HS Clocks */
		__HAL_RCC_USB_OTG_HS_CLK_DISABLE();
		__HAL_RCC_SYSCFG_CLK_DISABLE();
	}
}

DMA_HandleTypeDef emmc_dma_in;
DMA_HandleTypeDef emmc_dma_out;

#define MMC_CONTEXT_READ_FLAGS (MMC_CONTEXT_READ_MULTIPLE_BLOCK | MMC_CONTEXT_READ_SINGLE_BLOCK)
#define MMC_CONTEXT_WRITE_FLAGS (MMC_CONTEXT_WRITE_MULTIPLE_BLOCK | MMC_CONTEXT_WRITE_SINGLE_BLOCK)

void DMA2_Stream3_IRQHandler(void)
{
	if (hmmc1.Context & MMC_CONTEXT_DMA) {
		if (hmmc1.Context & MMC_CONTEXT_WRITE_FLAGS) {
			HAL_DMA_IRQHandler(&emmc_dma_out);
		} else if (hmmc1.Context & MMC_CONTEXT_READ_FLAGS) {
			HAL_DMA_IRQHandler(&emmc_dma_in);
		}
	}
}

void DMA2_Stream5_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_aes_out);
}

void AES_IRQHandler()
{
	HAL_CRYP_IRQHandler(&hcryp);
}

void DMA2_Stream6_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_aes_in);
}

void SDMMC1_IRQHandler()
{
	HAL_MMC_IRQHandler(&hmmc1);
}



/**
* @brief MMC MSP Initialization
* This function configures the hardware resources used in this example
* @param hmmc: MMC handle pointer
* @retval None
*/

extern MMC_HandleTypeDef hmmc1;

void HAL_MMC_MspInit(MMC_HandleTypeDef* hmmc)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	if(hmmc->Instance == SDMMC1) {
		/* Peripheral clock enable */
		__HAL_RCC_SDMMC1_CLK_ENABLE();

		__HAL_RCC_GPIOB_CLK_ENABLE();
		__HAL_RCC_GPIOC_CLK_ENABLE();
		__HAL_RCC_GPIOD_CLK_ENABLE();
		/**SDMMC1 GPIO Configuration
		PB8     ------> SDMMC1_D4
		PC12     ------> SDMMC1_CK
		PB9     ------> SDMMC1_D5
		PC11     ------> SDMMC1_D3
		PC10     ------> SDMMC1_D2
		PD2     ------> SDMMC1_CMD
		PC9     ------> SDMMC1_D1
		PC8     ------> SDMMC1_D0
		PC7     ------> SDMMC1_D7
		PC6     ------> SDMMC1_D6
		*/
		GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
		HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

		GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_11|GPIO_PIN_10|GPIO_PIN_9
		                      |GPIO_PIN_8|GPIO_PIN_7|GPIO_PIN_6;
		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
		HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

		GPIO_InitStruct.Pin = GPIO_PIN_2;
		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
		HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

		/* EMMC OUT Init */
		emmc_dma_out.Instance = DMA2_Stream3;
		emmc_dma_out.Init.Channel = DMA_CHANNEL_4;
		emmc_dma_out.Init.Direction = DMA_MEMORY_TO_PERIPH;
		emmc_dma_out.Init.PeriphInc = DMA_PINC_DISABLE;
		emmc_dma_out.Init.MemInc = DMA_MINC_ENABLE;
		emmc_dma_out.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
		emmc_dma_out.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
		emmc_dma_out.Init.Mode = DMA_PFCTRL;
		emmc_dma_out.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
		emmc_dma_out.Init.MemBurst = DMA_MBURST_SINGLE;
		emmc_dma_out.Init.PeriphBurst = DMA_PBURST_INC4;
		emmc_dma_out.Init.Priority = DMA_PRIORITY_LOW;
		if (HAL_DMA_Init(&emmc_dma_out) != HAL_OK) {
			Error_Handler();
		}

		__HAL_LINKDMA(&hmmc1, hdmatx, emmc_dma_out);

		/* EMMC IN Init */
		emmc_dma_in.Instance = DMA2_Stream3;
		emmc_dma_in.Init.Channel = DMA_CHANNEL_4;
		emmc_dma_in.Init.Direction = DMA_PERIPH_TO_MEMORY;
		emmc_dma_in.Init.PeriphInc = DMA_PINC_DISABLE;
		emmc_dma_in.Init.MemInc = DMA_MINC_ENABLE;
		emmc_dma_in.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
		emmc_dma_in.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
		emmc_dma_in.Init.Mode = DMA_PFCTRL;
		emmc_dma_in.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
		emmc_dma_in.Init.MemBurst = DMA_MBURST_SINGLE;
		emmc_dma_in.Init.PeriphBurst = DMA_PBURST_INC4;
		emmc_dma_in.Init.Priority = DMA_PRIORITY_LOW;
		if (HAL_DMA_Init(&emmc_dma_in) != HAL_OK) {
			Error_Handler();
		}

		__HAL_LINKDMA(&hmmc1, hdmarx, emmc_dma_in);

		HAL_NVIC_SetPriority(SDMMC1_IRQn, LOW_INT_PRIORITY, 0);
		HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
	}
}

/**
* @brief MMC MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param hmmc: MMC handle pointer
* @retval None
*/
void HAL_MMC_MspDeInit(MMC_HandleTypeDef* hmmc)
{
	if(hmmc->Instance==SDMMC1) {
		/* Peripheral clock disable */
		__HAL_RCC_SDMMC1_CLK_DISABLE();

		/**SDMMC1 GPIO Configuration
		PB8     ------> SDMMC1_D4
		PC12     ------> SDMMC1_CK
		PB9     ------> SDMMC1_D5
		PC11     ------> SDMMC1_D3
		PC10     ------> SDMMC1_D2
		PD2     ------> SDMMC1_CMD
		PC9     ------> SDMMC1_D1
		PC8     ------> SDMMC1_D0
		PC7     ------> SDMMC1_D7
		PC6     ------> SDMMC1_D6
		*/
		HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8|GPIO_PIN_9);

		HAL_GPIO_DeInit(GPIOC, GPIO_PIN_12|GPIO_PIN_11|GPIO_PIN_10|GPIO_PIN_9
		                |GPIO_PIN_8|GPIO_PIN_7|GPIO_PIN_6);

		HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
	}
}

/**
* @brief UART MSP Initialization
* This function configures the hardware resources used in this example
* @param huart: UART handle pointer
* @retval None
*/
void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	if(huart->Instance==USART1) {
		/* Peripheral clock enable */
		__HAL_RCC_USART1_CLK_ENABLE();

		__HAL_RCC_GPIOB_CLK_ENABLE();
		/**USART1 GPIO Configuration
		PB7     ------> USART1_RX
		PB6     ------> USART1_TX
		*/
		GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_6;
		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
		HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	}
}

/**
* @brief UART MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param huart: UART handle pointer
* @retval None
*/
void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)
{
	if(huart->Instance==USART1) {
		/* Peripheral clock disable */
		__HAL_RCC_USART1_CLK_DISABLE();

		/**USART1 GPIO Configuration
		PB7     ------> USART1_RX
		PB6     ------> USART1_TX
		*/
		HAL_GPIO_DeInit(GPIOB, GPIO_PIN_7|GPIO_PIN_6);
	}
}
