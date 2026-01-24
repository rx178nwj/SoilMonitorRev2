#ifndef TC74_SENSOR_H
#define TC74_SENSOR_H

#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// TC74定数定義
// TC74のI2Cアドレスは型番の末尾によって決まる (TC74Ax, x=0-7)
#define TC74_ADDR_A0        0x48  // TC74A0のI2Cアドレス (7ビットアドレス)
#define TC74_ADDR_A1        0x49  // TC74A1のI2Cアドレス
#define TC74_ADDR_A2        0x4A  // TC74A2のI2Cアドレス
#define TC74_ADDR_A3        0x4B  // TC74A3のI2Cアドレス
#define TC74_ADDR_A4        0x4C  // TC74A4のI2Cアドレス
#define TC74_ADDR_A5        0x4D  // TC74A5のI2Cアドレス
#define TC74_ADDR_A6        0x4E  // TC74A6のI2Cアドレス
#define TC74_ADDR_A7        0x4F  // TC74A7のI2Cアドレス

// デフォルトアドレス (TC74A5を想定)
#define TC74_ADDR_DEFAULT   TC74_ADDR_A5

// TC74レジスタアドレス
#define TC74_REG_TEMP       0x00  // 温度レジスタ (読み取り専用)
#define TC74_REG_CONFIG     0x01  // 設定レジスタ (読み書き可能)

// TC74設定レジスタビット定義
#define TC74_CONFIG_STANDBY     (1 << 7)  // Bit 7: スタンバイモード (1=スタンバイ, 0=ノーマル)
#define TC74_CONFIG_DATA_READY  (1 << 6)  // Bit 6: データレディフラグ (1=準備完了, 0=準備中)

// TC74測定範囲
#define TC74_TEMP_MIN       -40   // 最低温度 [°C]
#define TC74_TEMP_MAX       125   // 最高温度 [°C]
#define TC74_TEMP_RESOLUTION 1.0f // 温度分解能 [°C]

// TC74センサーデータ構造体
typedef struct {
    float temperature;      // 温度 [°C]
    bool data_ready;        // データ準備完了フラグ
    bool error;             // エラーフラグ
} tc74_data_t;

// TC74初期化 (デフォルトアドレス使用)
esp_err_t tc74_init(void);

// TC74初期化 (アドレス指定)
esp_err_t tc74_init_with_address(uint8_t i2c_addr);

// TC74温度読み取り
esp_err_t tc74_read_temperature(float *temperature);

// TC74温度読み取り (データ構造体使用)
esp_err_t tc74_read_data(tc74_data_t *data);

// TC74設定レジスタ読み取り
esp_err_t tc74_read_config(uint8_t *config);

// TC74設定レジスタ書き込み
esp_err_t tc74_write_config(uint8_t config);

// TC74スタンバイモード設定
esp_err_t tc74_set_standby_mode(bool enable);

// TC74データレディ状態確認
esp_err_t tc74_is_data_ready(bool *ready);

// TC74ウェイクアップ (スタンバイモードから復帰)
esp_err_t tc74_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif // TC74_SENSOR_H
