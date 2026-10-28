/*
 * NUCLEO-H7S3L8 USART3 PD8/PD9, GPDMA1 normal TX10/RX11.
 * Configuration follows the local CubeMX H7S3 bench and STM32CubeH7RS
 * NUCLEO-H7S3L8 UART examples. HAL/CMSIS code is consumed from the external
 * STM32Cube SDK under its original licenses; see LICENSES.md. This file: MIT.
 */
#include "main.h"

UART_HandleTypeDef huart3;
static DMA_HandleTypeDef tx_dma, rx_dma;

void Error_Handler(void)
{
	__disable_irq();
	for (;;) {
	}
}

void HAL_MspInit(void)
{
	if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
		Error_Handler();
}

static void clock_init(void)
{
	if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
		Error_Handler();
	RCC_OscInitTypeDef osc = {0};
	osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	osc.HSEState = RCC_HSE_ON;
	osc.PLL1.PLLState = RCC_PLL_ON;
	osc.PLL1.PLLSource = RCC_PLLSOURCE_HSE;
	osc.PLL1.PLLM = 2;
	osc.PLL1.PLLN = 50;
	osc.PLL1.PLLP = 1;
	osc.PLL1.PLLQ = osc.PLL1.PLLR = osc.PLL1.PLLS = osc.PLL1.PLLT = 2;
	osc.PLL2.PLLState = osc.PLL3.PLLState = RCC_PLL_NONE;
	if (HAL_RCC_OscConfig(&osc) != HAL_OK)
		Error_Handler();
	RCC_ClkInitTypeDef clk = {0};
	clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
	                RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK4 | RCC_CLOCKTYPE_PCLK5;
	clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
	clk.AHBCLKDivider = RCC_HCLK_DIV2;
	clk.APB1CLKDivider = clk.APB2CLKDivider = clk.APB4CLKDivider = clk.APB5CLKDivider =
	    RCC_APB1_DIV2;
	if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK)
		Error_Handler();
}

static void dma_init(DMA_HandleTypeDef* dma, DMA_Channel_TypeDef* channel, uint32_t request,
                     uint32_t direction)
{
	dma->Instance = channel;
	dma->Init.Request = request;
	dma->Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
	dma->Init.Direction = direction;
	dma->Init.SrcInc = direction == DMA_MEMORY_TO_PERIPH ? DMA_SINC_INCREMENTED : DMA_SINC_FIXED;
	dma->Init.DestInc = direction == DMA_PERIPH_TO_MEMORY ? DMA_DINC_INCREMENTED : DMA_DINC_FIXED;
	dma->Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
	dma->Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
	dma->Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
	dma->Init.SrcBurstLength = dma->Init.DestBurstLength = 1;
	dma->Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
	dma->Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
	dma->Init.Mode = DMA_NORMAL;
	if (HAL_DMA_Init(dma) != HAL_OK ||
	    HAL_DMA_ConfigChannelAttributes(dma, DMA_CHANNEL_NPRIV) != HAL_OK)
		Error_Handler();
}

void HAL_UART_MspInit(UART_HandleTypeDef* uart)
{
	if (uart->Instance != USART3)
		return;
	RCC_PeriphCLKInitTypeDef clock = {0};
	clock.PeriphClockSelection = RCC_PERIPHCLK_USART234578;
	clock.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_PCLK1;
	if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
		Error_Handler();
	__HAL_RCC_USART3_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPDMA1_CLK_ENABLE();
	GPIO_InitTypeDef gpio = {0};
	gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
	gpio.Mode = GPIO_MODE_AF_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	gpio.Alternate = GPIO_AF7_USART3;
	HAL_GPIO_Init(GPIOD, &gpio);
	dma_init(&tx_dma, GPDMA1_Channel10, GPDMA1_REQUEST_USART3_TX, DMA_MEMORY_TO_PERIPH);
	dma_init(&rx_dma, GPDMA1_Channel11, GPDMA1_REQUEST_USART3_RX, DMA_PERIPH_TO_MEMORY);
	__HAL_LINKDMA(uart, hdmatx, tx_dma);
	__HAL_LINKDMA(uart, hdmarx, rx_dma);
	/* UART and RX DMA share a preemption priority, as required by Uart.h. */
	HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
	HAL_NVIC_SetPriority(GPDMA1_Channel10_IRQn, 5, 0);
	HAL_NVIC_SetPriority(GPDMA1_Channel11_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(USART3_IRQn);
	HAL_NVIC_EnableIRQ(GPDMA1_Channel10_IRQn);
	HAL_NVIC_EnableIRQ(GPDMA1_Channel11_IRQn);
}

void board_init(void)
{
	HAL_Init();
	clock_init();
	huart3.Instance = USART3;
	huart3.Init.BaudRate = 115200;
	huart3.Init.WordLength = UART_WORDLENGTH_8B;
	huart3.Init.StopBits = UART_STOPBITS_1;
	huart3.Init.Parity = UART_PARITY_NONE;
	huart3.Init.Mode = UART_MODE_TX_RX;
	huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart3.Init.OverSampling = UART_OVERSAMPLING_16;
	huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
	huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&huart3) != HAL_OK ||
	    HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK ||
	    HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK ||
	    HAL_UARTEx_EnableFifoMode(&huart3) != HAL_OK)
		Error_Handler();
	/* AXI SRAM .bss is DMA accessible. Uart performs real line-aligned cache
       clean/invalidate; no DTCM buffer and no disabled maintenance shortcut. */
	SCB_EnableICache();
	SCB_EnableDCache();
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}

void USART3_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart3);
}

void GPDMA1_Channel10_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&tx_dma);
}

void GPDMA1_Channel11_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&rx_dma);
}

void HardFault_Handler(void)
{
	Error_Handler();
}

void MemManage_Handler(void)
{
	Error_Handler();
}

void BusFault_Handler(void)
{
	Error_Handler();
}

void UsageFault_Handler(void)
{
	Error_Handler();
}
