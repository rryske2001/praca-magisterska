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

static const char *TAG = "MCP_BADANIE";

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

// Funkcja pomocnicza do odczytu jednego rejestru
uint8_t MCP_ReadReg(uint8_t reg) {
    uint8_t data = 0;
    i2c_master_write_read_device(I2C_MASTER_NUM, MCP23017_ADDR, 
                                 &reg, 1, &data, 1, pdMS_TO_TICKS(100));
    return data;
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

void Test_Autoinkrementacji_Rollover_Task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "--- ROZPOCZECIE BADANIA ROLLOVER ---");

        // ... TUTAJ ZNAJDUJE SIĘ TWÓJ KOD TESTOWY ...
        // 1. Zapis 0xFF do IODIRA i IODIRB
        uint8_t setup_buf[3] = {0x00, 0xFF, 0xFF}; 
        i2c_master_write_to_device(I2C_MASTER_NUM, MCP23017_ADDR, setup_buf, sizeof(setup_buf), pdMS_TO_TICKS(100));

        // 2. Test zawijania wskaźnika
        uint8_t test_buf[4] = {0x15, 0x11, 0x22, 0x33};
        i2c_master_write_to_device(I2C_MASTER_NUM, MCP23017_ADDR, test_buf, sizeof(test_buf), pdMS_TO_TICKS(100));

        // 3. Odczyt i weryfikacja
        uint8_t iodira_po = MCP_ReadReg(0x00);
        uint8_t iodirb_po = MCP_ReadReg(0x01);
        
        if(iodira_po == 0x22 && iodirb_po == 0x33) {
            ESP_LOGI(TAG, "WYNIK: POZYTYWNY! Wskaźnik przewinął się na początek.");
        } else {
            ESP_LOGE(TAG, "WYNIK: NEGATYWNY.");
        }
        ESP_LOGI(TAG, "--- KONIEC BADANIA ---");

        //Ponowna konfiguracja MCP, aby przygotować go do kolejnego testu (reset i ustawienie pinów jako wyjścia)
        MCP_Init(); 
        // Opóźnienie przed kolejnym testem
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// --- Funkcja Główna ---
void app_main(void) {

    ESP_LOGI(TAG, "Inicjalizacja systemu (I2C)...");
    I2C_INIT();
    MCP_Init();
    // Tworzenie taska badawczego we FreeRTOS
    // Parametry: (Nazwa funkcji, "Nazwa_dla_debuggera", rozmiar stosu w bajtach, argumenty, priorytet, uchwyt)
    xTaskCreate(&Test_Autoinkrementacji_Rollover_Task, "Test_Rollover", 4096, NULL, 5, NULL);
}
