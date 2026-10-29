/**
 * @file stm32h7rsxx_hal_conf.h
 * @brief Application HAL selection; STM32Cube HAL headers remain external.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_HAL_CONF_H
#define TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_HAL_CONF_H
#pragma once
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HSE_VALUE 24000000UL
#define HSE_STARTUP_TIMEOUT 100UL
#define HSI_VALUE 64000000UL
#define CSI_VALUE 4000000UL
#define HSI48_VALUE 48000000UL
#define LSI_VALUE 32000UL
#define LSE_VALUE 32768UL
#define LSE_STARTUP_TIMEOUT 5000UL
#define EXTERNAL_CLOCK_VALUE 12288000UL
#define VDD_VALUE 3300UL
#define TICK_INT_PRIORITY 15UL
#define USE_RTOS 0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U
#define USE_SPI_CRC 0U
#include "stm32h7rsxx_hal_rcc.h"
#include "stm32h7rsxx_hal_gpio.h"
#include "stm32h7rsxx_hal_dma.h"
#include "stm32h7rsxx_hal_cortex.h"
#include "stm32h7rsxx_hal_flash.h"
#include "stm32h7rsxx_hal_exti.h"
#include "stm32h7rsxx_hal_pwr.h"
#include "stm32h7rsxx_hal_uart.h"
#define assert_param(expression) ((void)0U)
#endif // TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_HAL_CONF_H
