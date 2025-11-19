#ifndef SWITCH_INPUT_H
#define SWITCH_INPUT_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// スイッチ入力制御関数
esp_err_t switch_input_init(void);
bool switch_input_is_pressed(void);
void switch_input_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // SWITCH_INPUT_H