#pragma once

/* A fresh kernel handle per diagnostic write survives storage remounts. */
#ifdef __cplusplus
extern "C" {
#endif
void vitaRuntimeLogInit(void);
#ifdef __cplusplus
}
#endif
