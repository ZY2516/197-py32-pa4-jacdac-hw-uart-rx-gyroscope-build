#ifndef APP_PY32_JACDAC_RUNTIME_H
#define APP_PY32_JACDAC_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void APP_JacdacRuntimeInit(void);
void APP_JacdacRuntimeProcess(void);
void APP_JacdacExtiIrqHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_PY32_JACDAC_RUNTIME_H */
