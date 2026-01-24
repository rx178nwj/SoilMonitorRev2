file(GLOB_RECURSE srcs "main.c" "src/*.c")

idf_component_register(SRCS "nvs_config.c" "main.c"
                           "wifi_manager.c"
                           "time_sync_manager.c"
                           "components/sensors/sht30_sensor.c"
                           "components/sensors/sht40_sensor.c"
                           "components/sensors/tsl2591_sensor.c"
                           "components/sensors/fdc1004_sensor.c"
                           "components/sensors/ds18b20_sensor.c"
                           "components/sensors/tc74_sensor.c"
                           "components/actuators/led_control.c"
                           "components/actuators/ws2812_control.c"
                           "components/plant_logic/plant_manager.c"
                           "components/plant_logic/data_buffer.c"
                           "components/sensors/moisture_sensor.c"
                           "nvs_config.c"
                           "components/ble/ble_manager.c"
                           "components/actuators/switch_input.c"
                       PRIV_REQUIRES
                        # Core & System Components
                         nvs_flash
                         bt
                         esp_timer
                         esp_system
                         esp_common
                         log
                         esp_pm

                        # Networking Components
                         esp_wifi
                         esp_netif
                         esp_event
                         lwip

                         # Driver Components
                         driver
                         esp_adc
                         led_strip
                         espressif__onewire_bus
                       INCLUDE_DIRS "./include")
