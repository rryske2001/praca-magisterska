/*
ESP32                    MCP23S17
+-------------+         +----------------+
|             |         |                |
|         3V3 +---------+ 9 (VDD)        |
|         GND +----+----+ 10 (VSS)       |
|             |    |    |                |
| GPIO 5 (CS) +----+----+ 11 (CS)        |
| GPIO 23(MOSI)+---+----+ 13 (SI)        |
| GPIO 19(MISO)+---+----+ 14 (SO)        |
| GPIO 18(SCK)+----+----+ 12 (SCK)       |
|             |    |    |                |
| GPIO 17(RST)+----+----+ 18 (RESET)     |
+-------------+         +----------------+
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

// --- Definicje pinów ESP32 (VSPI) ---
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5
#define PIN_NUM_RST  17
#define SPI_MASTER_FREQ_HZ 500000

// --- Definicje rejestrów MCP23S17 ---
#define MCP_OPCODE 0x40
#define MCP_IODIRA 0x00
#define MCP_IODIRB 0x01
#define MCP_GPIOA  0x12
#define MCP_GPIOB  0x13

// --- Piny sterujące na PORCIE B (MCP23S17) ---
#define LCD_RS_BIT (1 << 0) // GPB0
#define LCD_E_BIT  (1 << 1) // GPB1

static const char *TAG = "LCD_MAIN";

volatile uint32_t seconds = 0;
uint8_t portB_shadow = 0; // Przechowuje aktualny stan portu B
spi_device_handle_t spi_handle;

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
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000)); // 1 sekunda
}

// --- Funkcje SPI i MCP ---

void SPI_INIT() {
    gpio_reset_pin(PIN_NUM_RST);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_RST, 1);

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    
}

void MCP_Write(uint8_t reg, uint8_t data) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 24;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = MCP_OPCODE;
    t.tx_data[1] = reg;
    t.tx_data[2] = data;

    spi_device_polling_transmit(spi_handle, &t);
}

void MCP_Init(void) {

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_MASTER_FREQ_HZ,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7
    };

    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle));

    // Reset sprzętowy
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

// --- Obsługa LCD HD44780 ---
void LCD_ENABLE() {
    portB_shadow |= LCD_E_BIT;           // E = 1
    MCP_Write(MCP_GPIOB, portB_shadow);
    esp_rom_delay_us(2);                 // Minimalnie dłuższy impuls E
    
    portB_shadow &= ~LCD_E_BIT;          // E = 0
    MCP_Write(MCP_GPIOB, portB_shadow);
    esp_rom_delay_us(100);               // 100us zamiast 50us - bezpieczny margines na przetworzenie znaku
}

void LCD_Send8Bit(uint8_t val, uint8_t is_cmd) {
    // 1. Ustawienie RS (Port B)
    if (is_cmd) portB_shadow &= ~LCD_RS_BIT; // RS = 0 (Command)
    else        portB_shadow |=  LCD_RS_BIT; // RS = 1 (Data)
    
    MCP_Write(MCP_GPIOB, portB_shadow); // Aktualizuj linie sterujące
    
    // 2. Wystawienie Danych (Port A)
    MCP_Write(MCP_GPIOA, val);
    
    // 3. Zatwierdzenie (Enable Pulse)
    LCD_ENABLE();
}

void LCD_SEND_COMMAND(uint8_t data_to_send) {
    LCD_Send8Bit(data_to_send, 1);
}

void LCD_SEND_DATA(uint8_t data_to_send) {
    LCD_Send8Bit(data_to_send, 0);
}

void LCD_INIT() {
    vTaskDelay(pdMS_TO_TICKS(50)); // Tutaj RTOS zadziała, bo 50ms to minimum 5 tików

    LCD_SEND_COMMAND(0b00110000);
    esp_rom_delay_us(5000); // 5 ms - twarde opóźnienie (zamiast vTaskDelay)
    LCD_SEND_COMMAND(0b00110000);
    esp_rom_delay_us(150);
    LCD_SEND_COMMAND(0b00110000);

    LCD_SEND_COMMAND(0b00111000); // 8-bit, 2 linie, 5x8
    LCD_SEND_COMMAND(0b00001100); // Włącz LCD, wyłącz kursor
    LCD_SEND_COMMAND(0b00000001); // Wyczyść ekran
    
    // TUTAJ JEST POPRAWKA: Czekamy sztywne 3000 mikrosekund (3 ms) na wyczyszczenie pamięci
    esp_rom_delay_us(3000); 
    
    LCD_SEND_COMMAND(0b00000110); // Inkrementacja adresu (przesuwaj w prawo)
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
    ESP_LOGI(TAG, "Inicjalizacja systemu...");

    SPI_INIT();
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
