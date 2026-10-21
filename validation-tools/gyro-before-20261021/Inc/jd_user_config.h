#pragma once

#define __WORDSIZE 32

#define JD_CONFIG_TEMPERATURE 0
#define JD_CONFIG_CONTROL_FLOOD 0
#define JD_CONFIG_WATCHDOG 0
#define JD_CONFIG_STATUS 1
#define JD_CLIENT 0
#define JD_PIPES 0
#define JD_USB_BRIDGE 0
#define JD_NET_BRIDGE 0
#define JD_BRIDGE 0
#define JD_DEVICESCRIPT 0
#define JD_LSTORE 0
#define JD_MS_TIMER 0
#define JD_LED_ERRORS 0
#define JD_SIGNAL_RDWR 0
#define JD_VERBOSE_ASSERT 0
#define JD_ADVANCED_STRING 0
#define JD_FREE_SUPPORTED 0
#define JD_SIMPLE_ALLOC 0
#define JD_HW_ALLOC 0
#define JD_SEND_FRAME 0
#define JD_RX_QUEUE 0
#define JD_HANDLE_ALL_PACKETS 1
#define JD_DMESG_BUFFER_SIZE 0
#define JD_EVENT_QUEUE_SIZE 64
#define JD_INSTANCE_NAME 1
#define JD_MAX_SERVICES 8

#define PIN_LED_R 1
#define PIN_LED_G 2
#define PIN_LED_B 3
/* Common-anode / active-low RGB LED: leave LED_RGB_COMMON_CATHODE undefined. */
/* Channel balance from the official jacdac-module-template reference design
 * (calibrated to ~50 lux per channel at RGB_LED_PERIOD 600). */
#define RGB_LED_PERIOD 600
#define LED_R_MULT 250
#define LED_G_MULT 76
#define LED_B_MULT 221
/* Event (blink) layer peak: 0x2000 = 1/8 of full scale, one step above the
 * ~3% glow layer, which stays the dimmest level. Raise/lower this single
 * value to tune blink brightness (0xff00 = full). */
#define JD_STATUS_BLINK_LEVEL 0x2000

#define PY32_QMI8656_GYRO_FORCE_STREAMING 1
#define PY32_QMI8656_GYRO_STREAMING_INTERVAL_MS 50

#define JD_LOG(...) ((void)0)
