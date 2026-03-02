/*
ESP32                    MCP23017 (I2C)
+-------------+         +----------------+
|             |         |                |
|         3V3 +----+----+ 9 (VDD)        |
|         GND +--+-|-+--+ 10 (VSS)       |
|             |  | | |  |                |
| GPIO 21(SDA)+--|-|-|--+ 13 (SDA)       |
| GPIO 22(SCL)+--|-|-|--+ 12 (SCL)       |
|             |  | | |  |                |
| GPIO 17(RST)+--|-|-|--+ 18 (RESET)     |
|             |  | | |  |                |
|             |  +-|-|--+ 15 (A0) -> GND |
|             |    +-|--+ 16 (A1) -> GND |
|             |      +--+ 17 (A2) -> GND |
+-------------+         +----------------+
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

// --- Definicje pinów ESP32 (I2C) ---
#define I2C_MASTER_SCL_IO           22      // GPIO dla zegara SCL
#define I2C_MASTER_SDA_IO           21      // GPIO dla danych SDA
#define I2C_MASTER_NUM              I2C_NUM_0 // Port I2C
#define I2C_MASTER_FREQ_HZ          100000  // Częstotliwość I2C (100 kHz - bardzo bezpieczna)
#define I2C_MASTER_TIMEOUT_MS       1000

#define PIN_NUM_RST  17 // Sprzętowy RESET układu MCP

// --- Definicje I2C i Rejestrów MCP23017 ---
#define MCP23017_ADDR 0x20 // 7-bitowy adres I2C (gdy A0, A1, A2 = GND)
#define MCP_IODIRA    0x00
#define MCP_IODIRB    0x01
#define MCP_GPIOA     0x12
#define MCP_GPIOB     0x13

// --- Piny sterujące na PORCIE B (MCP23017) ---
#define LCD_RS_BIT (1 << 0) // GPB0
#define LCD_E_BIT  (1 << 1) // GPB1

static const char *TAG = "LCD_I2C";

volatile uint32_t seconds = 0;
uint8_t portB_shadow = 0;

// --- Timer zliczający sekundy ---
void timer1_callback(void* arg) {
    seconds++;
}

void timer1_init() {
    const esp_timer_create_args_t timer_args = {
        .callback = &timer1_callback,
        .name = "seconds_timer"
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000));
}

void I2C_INIT() {
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_DISABLE, // Wyłączenie wewnętrznych pull-upy
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0));
}

// --- Funkcja wysyłająca dane do MCP po I2C ---
void MCP_Write(uint8_t reg, uint8_t data) {
    // Ramka I2C dla MCP: [Adres Rejestru] -> [Wartość]
    uint8_t write_buf[2] = { reg, data };
    
    // Prosta wbudowana funkcja zapisu w ESP-IDF
    esp_err_t err = i2c_master_write_to_device(I2C_MASTER_NUM, MCP23017_ADDR, write_buf, sizeof(write_buf), pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd komunikacji I2C z MCP23017! Sprawdź kable.");
    }
}

void MCP_Init(void) {
    // Reset sprzętowy układu
    gpio_reset_pin(PIN_NUM_RST);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Ustawienie Portu A i B jako wyjścia
    MCP_Write(MCP_IODIRA, 0x00);
    MCP_Write(MCP_IODIRB, 0x00);
    
    // Wyzerowanie stanów
    MCP_Write(MCP_GPIOA, 0x00);
    MCP_Write(MCP_GPIOB, 0x00);
}

// --- Obsługa LCD HD44780 (Bez zmian względem poprawionego SPI) ---
void LCD_ENABLE() {
    portB_shadow |= LCD_E_BIT;           // E = 1
    MCP_Write(MCP_GPIOB, portB_shadow);
    esp_rom_delay_us(2);                 
    
    portB_shadow &= ~LCD_E_BIT;          // E = 0
    MCP_Write(MCP_GPIOB, portB_shadow);
    esp_rom_delay_us(100);               
}

void LCD_Send8Bit(uint8_t val, uint8_t is_cmd) {
    if (is_cmd) {
        portB_shadow &= ~LCD_RS_BIT; // Komenda
    } else {
        portB_shadow |= LCD_RS_BIT;  // Dane
    }
    MCP_Write(MCP_GPIOB, portB_shadow);
    MCP_Write(MCP_GPIOA, val);
    LCD_ENABLE();
}

void LCD_SEND_COMMAND(uint8_t data_to_send) {
    LCD_Send8Bit(data_to_send, 1);
}

void LCD_SEND_DATA(uint8_t data_to_send) {
    LCD_Send8Bit(data_to_send, 0);
}

void LCD_INIT() {
    vTaskDelay(pdMS_TO_TICKS(50));

    LCD_SEND_COMMAND(0b00110000);
    esp_rom_delay_us(5000); // 5 ms
    LCD_SEND_COMMAND(0b00110000);
    esp_rom_delay_us(150);
    LCD_SEND_COMMAND(0b00110000);

    LCD_SEND_COMMAND(0b00111000); 
    LCD_SEND_COMMAND(0b00001100); 
    LCD_SEND_COMMAND(0b00000001); // Wyczyść
    esp_rom_delay_us(3000);       // Sztywne 3ms
    LCD_SEND_COMMAND(0b00000110); // Przesuwaj w prawo
}

void LCD_SET_CURSOR(uint8_t row, uint8_t column) {
    uint8_t address;
    if (row == 0)
        address = 0x00 + column;
    else
        address = 0x40 + column;
    LCD_SEND_COMMAND(0b10000000 | address);
}

void LCD_PRINT_CHAR(char character) {
    LCD_SEND_DATA((uint8_t)character);
}

void LCD_PRINT_STRING(const char* str) {
    while (*str) {
        LCD_PRINT_CHAR(*str++);
    }
}

// --- Pętla Główna ---
void app_main(void) {
    ESP_LOGI(TAG, "Inicjalizacja systemu (I2C)...");

    I2C_INIT();
    MCP_Init();
    LCD_INIT();
    timer1_init();
    
    LCD_SET_CURSOR(0, 0);
    LCD_PRINT_STRING("Hello, ESP32!");

    char buffer[16];
    while (1) {
        LCD_SET_CURSOR(1, 0);
        sprintf(buffer, "%lu", seconds);
        LCD_PRINT_STRING(buffer);
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}