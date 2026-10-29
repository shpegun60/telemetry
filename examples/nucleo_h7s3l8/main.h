/**
 * @file main.h
 * @brief NUCLEO-H7S3L8 standalone board initialization contract.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_MAIN_H
#define TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_MAIN_H
#pragma once
#include "stm32h7rsxx_hal.h"
#ifdef __cplusplus
extern "C" {
#endif
extern UART_HandleTypeDef huart3;
void board_init(void);
void Error_Handler(void);
#ifdef __cplusplus
}
#endif
#endif // TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_MAIN_H
