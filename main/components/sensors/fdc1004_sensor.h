#ifndef FDC1004_SENSOR_H
#define FDC1004_SENSOR_H

#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// FDC1004定数定義
#define FDC1004_ADDR            0x50  // FDC1004のI2Cアドレス（7-bit）
#define FDC1004_DEVICE_ID       0x1004 // デバイスID

// FDC1004レジスタアドレス
#define FDC1004_REG_MEAS1_MSB   0x00  // Measurement 1 MSB
#define FDC1004_REG_MEAS1_LSB   0x01  // Measurement 1 LSB
#define FDC1004_REG_MEAS2_MSB   0x02  // Measurement 2 MSB
#define FDC1004_REG_MEAS2_LSB   0x03  // Measurement 2 LSB
#define FDC1004_REG_MEAS3_MSB   0x04  // Measurement 3 MSB
#define FDC1004_REG_MEAS3_LSB   0x05  // Measurement 3 LSB
#define FDC1004_REG_MEAS4_MSB   0x06  // Measurement 4 MSB
#define FDC1004_REG_MEAS4_LSB   0x07  // Measurement 4 LSB
#define FDC1004_REG_CONF_MEAS1  0x08  // Configuration for Measurement 1
#define FDC1004_REG_CONF_MEAS2  0x09  // Configuration for Measurement 2
#define FDC1004_REG_CONF_MEAS3  0x0A  // Configuration for Measurement 3
#define FDC1004_REG_CONF_MEAS4  0x0B  // Configuration for Measurement 4
#define FDC1004_REG_FDC_CONF    0x0C  // FDC Configuration
#define FDC1004_REG_OFFSET_CAL_CIN1  0x0D  // Offset Calibration CIN1
#define FDC1004_REG_OFFSET_CAL_CIN2  0x0E  // Offset Calibration CIN2
#define FDC1004_REG_OFFSET_CAL_CIN3  0x0F  // Offset Calibration CIN3
#define FDC1004_REG_OFFSET_CAL_CIN4  0x10  // Offset Calibration CIN4
#define FDC1004_REG_GAIN_CAL_CIN1    0x11  // Gain Calibration CIN1
#define FDC1004_REG_GAIN_CAL_CIN2    0x12  // Gain Calibration CIN2
#define FDC1004_REG_GAIN_CAL_CIN3    0x13  // Gain Calibration CIN3
#define FDC1004_REG_GAIN_CAL_CIN4    0x14  // Gain Calibration CIN4
#define FDC1004_REG_MANUFACTURER_ID  0xFE  // Manufacturer ID
#define FDC1004_REG_DEVICE_ID   0xFF  // Device ID

// FDC1004測定チャネル
typedef enum {
    FDC1004_CHANNEL_1 = 0,
    FDC1004_CHANNEL_2 = 1,
    FDC1004_CHANNEL_3 = 2,
    FDC1004_CHANNEL_4 = 3
} fdc1004_channel_t;

// FDC1004入力選択（シングルエンド測定用）
typedef enum {
    FDC1004_CIN1 = 0,     // CIN1ピン
    FDC1004_CIN2 = 1,     // CIN2ピン
    FDC1004_CIN3 = 2,     // CIN3ピン
    FDC1004_CIN4 = 3,     // CIN4ピン
    FDC1004_CAPDAC = 4,   // CAPDAC（オフセット補正用）
    FDC1004_DISABLED = 7  // DISABLED（シングルエンド測定でSHLD1/SHLD2内部ショート用）
} fdc1004_input_t;

// FDC1004サンプルレート
typedef enum {
    FDC1004_RATE_100HZ = 1,   // 100 samples/sec
    FDC1004_RATE_200HZ = 2,   // 200 samples/sec
    FDC1004_RATE_400HZ = 3    // 400 samples/sec
} fdc1004_rate_t;

// FDC1004測定設定構造体
typedef struct {
    fdc1004_input_t cha;      // 正入力（CIN1-CIN4）
    fdc1004_input_t chb;      // 負入力（CIN1-CIN4 or CAPDAC）
    uint8_t capdac;           // CAPDAC値（0-31）、オフセット補正用
} fdc1004_meas_config_t;

// FDC1004センサーデータ構造体
typedef struct {
    float capacitance_ch1;    // チャネル1の静電容量 [pF]
    float capacitance_ch2;    // チャネル2の静電容量 [pF]
    float capacitance_ch3;    // チャネル3の静電容量 [pF]
    float capacitance_ch4;    // チャネル4の静電容量 [pF]
    int32_t raw_ch1;          // チャネル1の生データ（24-bit符号付き）
    int32_t raw_ch2;          // チャネル2の生データ
    int32_t raw_ch3;          // チャネル3の生データ
    int32_t raw_ch4;          // チャネル4の生データ
    bool error;               // エラーフラグ
} fdc1004_data_t;

// FDC1004初期化
esp_err_t fdc1004_init(void);

// FDC1004デバイスID確認
esp_err_t fdc1004_check_device_id(uint16_t *device_id);

/**
 * @brief FDC1004測定設定（シングルエンド測定、SHLD1でシールド）
 *
 * シングルエンド測定（CINn vs GND）を設定します。
 * CHB=DISABLED (0b111) かつ CAPDAC=0 の設定により、SHLD1とSHLD2が
 * デバイス内部で短絡され、SHLD1のみで全チャネル（CIN1〜CIN4）の
 * シールドが可能になります。
 *
 * @param channel 測定チャネル (CHANNEL_1〜CHANNEL_4)
 * @param input 測定対象入力ピン (CIN1〜CIN4)
 * @param capdac CAPDAC値 (0-31)、通常は0を使用
 *               0以外を設定するとSHLD2がフローティングになり
 *               SHLD1のみでシールドできなくなるため注意
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t fdc1004_configure_single_measurement(
    fdc1004_channel_t channel,
    fdc1004_input_t input,
    uint8_t capdac
);

// FDC1004測定設定（差動測定）
esp_err_t fdc1004_configure_differential_measurement(
    fdc1004_channel_t channel,
    fdc1004_input_t cha,
    fdc1004_input_t chb,
    uint8_t capdac
);

// FDC1004測定トリガー
esp_err_t fdc1004_trigger_measurement(
    uint8_t channel_mask,      // 測定するチャネル（ビットマスク: bit0-3）
    fdc1004_rate_t rate
);

// FDC1004測定完了待機
esp_err_t fdc1004_wait_for_measurement(uint8_t channel_mask, uint32_t timeout_ms);

// FDC1004生データ読み取り
esp_err_t fdc1004_read_raw_capacitance(fdc1004_channel_t channel, int32_t *raw_value);

// FDC1004静電容量値読み取り（pF単位）
esp_err_t fdc1004_read_capacitance(fdc1004_channel_t channel, float *capacitance, uint8_t capdac);

/**
 * @brief FDC1004全チャネル測定（シングルエンド、SHLD1でシールド）
 *
 * 全チャネル（CIN1〜CIN4）をシングルエンド測定で一括測定します。
 * CAPDAC=0で設定されるため、SHLD1とSHLD2が内部で短絡され、
 * SHLD1のみで全チャネルのシールドが行われます。
 *
 * @param data 測定結果の格納先
 * @param rate サンプルレート (100Hz, 200Hz, 400Hz)
 * @return ESP_OK: 成功, その他: エラー
 */
esp_err_t fdc1004_measure_all_channels(fdc1004_data_t *data, fdc1004_rate_t rate);

// FDC1004レジスタ読み取り（16-bit）
esp_err_t fdc1004_read_register(uint8_t reg_addr, uint16_t *value);

// FDC1004レジスタ書き込み（16-bit）
esp_err_t fdc1004_write_register(uint8_t reg_addr, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif // FDC1004_SENSOR_H
