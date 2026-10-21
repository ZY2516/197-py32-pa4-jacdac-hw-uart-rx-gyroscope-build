#ifndef __PY32_JACDAC_BOARD_H
#define __PY32_JACDAC_BOARD_H

#include "py32f0xx_hal.h"

#define APP_JACDAC_GPIO_PORT            GPIOA
#define APP_JACDAC_GPIO_PIN             GPIO_PIN_4
#define APP_JACDAC_GPIO_CLK_ENABLE()    __HAL_RCC_GPIOA_CLK_ENABLE()

#define APP_QMI_I2C_INSTANCE            I2C1
#define APP_QMI_I2C_GPIO_PORT           GPIOB
#define APP_QMI_I2C_SCL_PIN             GPIO_PIN_3
#define APP_QMI_I2C_SDA_PIN             GPIO_PIN_4
#define APP_QMI_I2C_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOB_CLK_ENABLE()
#define APP_QMI_I2C_CLK_ENABLE()        __HAL_RCC_I2C_CLK_ENABLE()

#define APP_QMI_ADDR_6A                 0x6AU
#define APP_QMI_ADDR_6B                 0x6BU

#define APP_I2C_TIMEOUT_MS              20U
#define APP_SCAN_INTERVAL_MS            900U

#define APP_ENABLE_48MHZ_CLOCK          0U

/* Hardware USART RX path (build 194): PA4 has USART1_RX alternate function
 * (RX only, no TX), so RX uses the USART peripheral while TX stays GPIO
 * bit-bang. Set to 0U to fall back to the 193 assembly bit-bang sampler. */
#define APP_JACDAC_RX_USE_HW_UART       1U
#define APP_JACDAC_USART                USART1
#define APP_JACDAC_USART_CLK_ENABLE()   (RCC->APBENR2 |= RCC_APBENR2_USART1EN)
/* PA4->USART1_RX AF index: datasheet AF table not in provided materials.
 * Candidates are GPIO_AF1_USART1 (1) and GPIO_AF3_USART1 (3). If SWD shows
 * g_app_jd_rx_hw_rxne_count stuck at 0, rebuild with 3U. */
#define APP_JACDAC_USART_RX_AF          1U
/* PCLK 24MHz, 16x oversampling: USARTDIV = 24/16 = 1.5 -> mantissa 1,
 * fraction 8 -> exactly 1Mbaud, 0% error. */
#define APP_JACDAC_USART_BRR_1M         0x0018U

#endif /* __PY32_JACDAC_BOARD_H */
