/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f7xx_hal.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_multi.h"
#include "flash.h"
#include "rng.h"
#include "rtc_rand.h"
#include "commands.h"
#include "usb_keyboard.h"
#include "crc.h"
#include "usbd_msc_scsi.h"

#include "memory_layout.h"

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);


#include "usbd_core.h"
#include "usbd_msc_ops.h"
#include "commands.h"

/* Private variables ---------------------------------------------------------*/
CRYP_HandleTypeDef hcryp;
__ALIGN_BEGIN static const uint32_t pKeyAES[4] __ALIGN_END = {
	0x00000000,0x00000000,0x00000000,0x00000000
};

RNG_HandleTypeDef hrng;
MMC_HandleTypeDef hmmc1;

UART_HandleTypeDef huart1;

PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* Private function prototypes -----------------------------------------------*/
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_AES_Init(void);
static void MX_RNG_Init(void);
static void MX_SDMMC1_MMC_Init(void);
#ifdef USE_UART
static void MX_USART1_UART_Init(void);
#endif
USBD_HandleTypeDef USBD_Device;

#define LED1_PORT GPIOI
#define LED1_PIN GPIO_PIN_11

#define LED2_PORT GPIOD
#define LED2_PIN GPIO_PIN_13

#define BUTTON_PORT GPIOD
#define BUTTON_PORT_NUM (3)
#define BUTTON_PIN GPIO_PIN_0
#define BUTTON_PIN_NUM (0)
#define BUTTON_IRQ (EXTI0_IRQn)
#define BUTTON_HANDLER (EXTI0_IRQHandler)

void setLED1(int x)
{
	HAL_GPIO_WritePin(LED1_PORT, LED1_PIN, x ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void setLED2(int x)
{
	HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, x ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static int buttonState()
{
	return HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN);
}

//Used for old read/write test
volatile int mmcReadComplete = 0;
volatile int mmcWriteComplete = 0;

static void MX_DMA_Init(void);

#include "buffer_manager.h"

#define USB_BULK_BUFFER_SIZE (16384)
#define USB_BULK_BUFFER_COUNT (6)

static uint8_t g_usbBulkBuffer[USB_BULK_BUFFER_SIZE * USB_BULK_BUFFER_COUNT] __attribute__((aligned(16)));
struct bufferFIFO usbBulkBufferFIFO;

static int g_ms_last_pressed = 0;
static int g_blink_state = 0;
static int g_blink_start = 0;
static int g_blink_period = 0;
static int g_blink_duration = 0;
static int g_button_state = 0;
static int g_pause_start;

static int g_timer_target;

static int g_press_pending = 0;

__weak void blink_timeout()
{
}
__weak void button_press()
{
}

__weak void long_button_press()
{
}

__weak void button_release()
{
}

static void fail(int l1, int l2)
{
	setLED1(l1);
	setLED2(l2);
	while(1);
}

void assert(int cond)
{
	assert_lit(cond, 1, 1);
}

void assert_lit(int cond, int l1, int l2)
{
	if (!cond) {
		fail(l1,l2);
	}
}

void timer_start(int ms)
{
	int ms_count = HAL_GetTick();
	g_timer_target = ms_count + ms;
}

void timer_stop()
{
	g_timer_target = 0;
}

__weak void timer_timeout()
{
}

void led_off()
{
	setLED1(0);
	setLED2(0);
}

void led_on()
{
	setLED1(1);
	setLED2(1);
}

static int g_blink_paused = 0;

static uint8_t g_timeout_event_secs;

void blink_idle()
{
	int ms_count = HAL_GetTick();
	if (g_blink_period > 0 && !g_blink_paused) {
		int next_blink_state = ((ms_count - g_blink_start)/(g_blink_period/2)) % 2;
		if (next_blink_state != g_blink_state) {
			if (next_blink_state) {
				led_off();
			} else {
				led_on();
			}
		}
		g_blink_state = next_blink_state;
		int timeout_event_msecs = g_blink_duration - (ms_count - g_blink_start);
		int next_timeout_event_secs = (timeout_event_msecs+999)/1000;
		if ((timeout_event_msecs <= 0) && (g_blink_duration > 0)) {
			g_blink_period = 0;
			led_off();
			blink_timeout();
		}
		if (next_timeout_event_secs != g_timeout_event_secs) {
			g_timeout_event_secs = next_timeout_event_secs;
			if (g_device_state != DS_DISCONNECTED && g_device_state != DS_RESET) {
				cmd_event_send(2, &g_timeout_event_secs, sizeof(g_timeout_event_secs));
			}
		}
	}
}

void start_blinking(int period, int duration)
{
	int ms_count = HAL_GetTick();
	g_blink_start = ms_count;
	g_blink_period = period;
	g_blink_duration = duration;
	g_timeout_event_secs = (g_blink_duration + 999)/1000;
	led_on();
}

void pause_blinking()
{
	int ms_count = HAL_GetTick();
	led_off();
	g_blink_paused = 1;
	g_pause_start = ms_count;
}

void resume_blinking()
{
	int ms_count = HAL_GetTick();
	if (g_blink_paused) {
		g_blink_start += (ms_count - g_pause_start);
		g_blink_paused = 0;
		blink_idle();
	}
}

void stop_blinking()
{
	g_blink_period = 0;
	g_blink_paused = 0;
	led_off();
}

int is_blinking()
{
	return g_blink_period > 0;
}


#ifdef ENABLE_FIDO2
#include "mini-gmp.h"
#include "fido2/crypto.h"
#include "fido2/ctaphid.h"

__attribute__ ((aligned (8))) uint8_t heap[8192];
int heap_idx = 0;

void *mp_alloc(size_t sz)
{
	while(1) {
		uint32_t sz = *((uint32_t *)(heap + heap_idx - 4));
		if (sz | 0x80000000) {
			break;
		} else {
			heap_idx -= (sz + 4);
		}
	}
	void *ret = (void *)(heap + heap_idx);
	heap_idx += sz;
	*((uint32_t *)(heap + heap_idx)) = sz | 0x80000000;
	heap_idx += 4;
	return ret;
}

void mp_dealloc(void *p, size_t sz)
{
	*((uint32_t *)((uint8_t *)p + sz)) = sz;
}

void *mp_realloc(void *p, size_t before, size_t after)
{
	void *ret = mp_alloc(after);
	memset(p, 0, after);
	memcpy(ret, p, before);
	mp_dealloc(p, before);
	return ret;
}
#endif

void BUTTON_HANDLER()
{
	int ms_count = HAL_GetTick();
	g_ms_last_pressed = ms_count;
	g_press_pending = 1;
	EXTI->PR = (1 << BUTTON_PIN_NUM);
}

int main (void)
{
	SCB_EnableICache();
	//We don't enable DCACHE tp ensure coherency for DMA transfers
	//SCB_EnableDCache();
	HAL_Init();
	SystemClock_Config();
	MX_GPIO_Init();
	int blink_duration = 500;
	HAL_Delay(blink_duration);
	led_on();
	HAL_Delay(blink_duration);
	led_off();

	MX_DMA_Init();
	MX_AES_Init();
	MX_SDMMC1_MMC_Init();

	//TODO: move this elsewhere
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	__HAL_RCC_CRC_CLK_ENABLE();
	crc_init();

#ifdef BOOT_MODE_B
	HAL_NVIC_SetPriority(RNG_IRQn, DEFAULT_INT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(RNG_IRQn);
	__HAL_RCC_RNG_CLK_ENABLE();
	rng_init();

	HAL_NVIC_SetPriority(RTC_WKUP_IRQn, DEFAULT_INT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
	EXTI->IMR |= (1<<RTC_EXTI_LINE);
	EXTI->RTSR |= (1<<RTC_EXTI_LINE);
	__HAL_RCC_RTC_CLK_ENABLE();
	__HAL_RCC_RTC_ENABLE();
	rtc_rand_init(0x7f);
#endif

#ifdef USE_UART
	MX_USART1_UART_Init();
#endif
#ifdef ENABLE_FIDO2
	mp_set_memory_functions(mp_alloc, mp_realloc, mp_dealloc);
	crypto_ecc256_init();
#endif

	SYSCFG->EXTICR[BUTTON_PIN_NUM / 4] = (BUTTON_PORT_NUM << (4 * (BUTTON_PIN_NUM % 4)));
	EXTI->FTSR |= (1 << BUTTON_PIN_NUM);
	EXTI->IMR |= (1 << BUTTON_PIN_NUM);
	HAL_NVIC_SetPriority(EXTI0_IRQn, HIGH_INT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(EXTI0_IRQn);

	usbBulkBufferFIFO.maxBufferSize = USB_BULK_BUFFER_SIZE;
	usbBulkBufferFIFO.bufferStorage = g_usbBulkBuffer;
	usbBulkBufferFIFO.bufferCount = USB_BULK_BUFFER_COUNT;

	HAL_Delay(5);

	cmd_init();
#ifdef ENABLE_FIDO2
	ctaphid_init();
#endif

	usbd_scsi_init();

	USBD_Init(&USBD_Device, &Multi_Desc, 0);
	USBD_RegisterClass(&USBD_Device, USBD_MULTI_CLASS);
	USBD_MSC_RegisterStorage(&USBD_Device, &USBD_MSC_Template_fops);
	USBD_Start(&USBD_Device);

	while (1) {
		int work_to_do = 0;
		__asm__("cpsid i");
		work_to_do |= usb_keyboard_idle_ready();
		work_to_do |= command_idle_ready();
		work_to_do |= flash_idle_ready();
		work_to_do |= usbd_scsi_idle_ready();
		work_to_do |= (g_timer_target != 0) ? 1 : 0;
		work_to_do |= (g_blink_period > 0) ? 1 : 0;
		work_to_do |= g_press_pending;
		work_to_do |= g_button_state;
		if (!work_to_do) {
			__asm__("wfi");
		}
		__asm__("cpsie i");
		int ms_count = HAL_GetTick();
		if (ms_count > g_timer_target && g_timer_target != 0) {
			timer_timeout();
			g_timer_target = 0;
		}
		usb_keyboard_idle();
		blink_idle();
		command_idle();
		flash_idle();
		usbd_scsi_idle();
		int current_button_state = buttonState() ? 0 : 1;

		if (g_press_pending) {
			g_press_pending = 0;
			if (!g_button_state) {
				button_press();
				g_button_state = 1;
			}
		}
		if (!current_button_state && g_button_state && (ms_count - g_ms_last_pressed) > 100) {
			button_release();
			g_button_state = 0;
		}
		if (g_button_state && ((ms_count - g_ms_last_pressed) > 2000)) {
			g_button_state = 0;
			long_button_press();
		}
	}
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
static void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

	/** Configure the main internal regulator output voltage
	*/
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
	/** Initializes the CPU, AHB and APB busses clocks
	*/
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = 6;
	RCC_OscInitStruct.PLL.PLLN = 216;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ = 9;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		Error_Handler();
	}
	/** Activate the Over-Drive mode
	*/
	if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
		Error_Handler();
	}

	RCC_OscInitStruct.OscillatorType =  RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
	RCC_OscInitStruct.LSIState = RCC_LSI_ON;
	RCC_OscInitStruct.LSEState = RCC_LSE_OFF;
	if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK) {
		Error_Handler();
	}
	PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1|RCC_PERIPHCLK_SDMMC1 |RCC_PERIPHCLK_CLK48;
#ifdef BOOT_MODE_B
	PeriphClkInitStruct.PeriphClockSelection |= RCC_PERIPHCLK_RTC;
#endif
	PeriphClkInitStruct.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
	PeriphClkInitStruct.Clk48ClockSelection = RCC_CLK48SOURCE_PLL;
	PeriphClkInitStruct.Sdmmc1ClockSelection = RCC_SDMMC1CLKSOURCE_CLK48;
#ifdef BOOT_MODE_B
	PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
#endif
	if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
		Error_Handler();
	}
}

static void MX_AES_Init(void)
{
#ifdef BOOT_MODE_B
	hcryp.Instance = AES;
	hcryp.Init.DataType = CRYP_DATATYPE_32B;
	hcryp.Init.KeySize = CRYP_KEYSIZE_128B;
	hcryp.Init.pKey = (uint32_t *)pKeyAES;
	hcryp.Init.Algorithm = CRYP_AES_CBC;
	hcryp.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_WORD;
	if (HAL_CRYP_Init(&hcryp) != HAL_OK) {
		Error_Handler();
	}
#endif
}

/**
  * @brief SDMMC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SDMMC1_MMC_Init(void)
{
	hmmc1.Instance = SDMMC1;
	hmmc1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
	hmmc1.Init.ClockBypass = SDMMC_CLOCK_BYPASS_ENABLE;
	hmmc1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
	hmmc1.Init.BusWide = SDMMC_BUS_WIDE_1B;
	hmmc1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
	hmmc1.Init.ClockDiv = 0;
	if (HAL_MMC_Init(&hmmc1) != HAL_OK) {
		Error_Handler();
	}
	if (HAL_MMC_ConfigWideBusOperation(&hmmc1, SDMMC_BUS_WIDE_8B) != HAL_OK) {
		Error_Handler();
	}
}

static void MX_DMA_Init(void)
{
	__HAL_RCC_DMA2_CLK_ENABLE();
	HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, LOW_INT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
	HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, LOW_INT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
	HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, LOW_INT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */

#ifdef USE_UART
static void MX_USART1_UART_Init(void)
{
	huart1.Instance = USART1;
	huart1.Init.BaudRate = 115200;
	huart1.Init.WordLength = UART_WORDLENGTH_8B;
	huart1.Init.StopBits = UART_STOPBITS_1;
	huart1.Init.Parity = UART_PARITY_NONE;
	huart1.Init.Mode = UART_MODE_TX_RX;
	huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart1.Init.OverSampling = UART_OVERSAMPLING_16;
	huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&huart1) != HAL_OK) {
		Error_Handler();
	}
}
#endif

static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOI_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(GPIOI, GPIO_PIN_11, GPIO_PIN_RESET);

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);

	/*Configure GPIO pin : PD0 (switch) */
	GPIO_InitStruct.Pin = GPIO_PIN_0;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	/*Configure GPIO pin : PI11 (Status LED 1)*/
	GPIO_InitStruct.Pin = GPIO_PIN_11;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

	/*Configure GPIO pin : PD13 (Status LED 2)*/
	GPIO_InitStruct.Pin = GPIO_PIN_13;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

void Error_Handler(void)
{
	/* User can add his own implementation to report the HAL error return state */
	while(1);
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	   tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
}
#endif /* USE_FULL_ASSERT */
