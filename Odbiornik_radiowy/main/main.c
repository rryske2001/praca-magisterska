#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

// --- Nagłówki CC1101 ---
#include "cc1101.h"
#include "cc1101_hal.h"
#include "cc1101_regs.h"

// --- Definicje pinów ESP32 (VSPI / SPI2_HOST) ---
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5   // CS dla LCD (MCP23S17)
#define PIN_NUM_RST  17  // RST dla LCD
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

static const char *TAG = "WEATHER_STATION";

uint8_t portB_shadow = 0; // Przechowuje aktualny stan portu B
spi_device_handle_t spi_lcd_handle;

// --- Struktura danych z nadajnika (ATtiny414) ---
typedef struct __attribute__((packed)) {
    int32_t temp_hundredths;
    uint32_t pressure_pa;
    uint32_t hum_x1024;
    uint16_t battery_mv;
} sensor_packet_t;


// ==========================================
// FUNKCJE LCD (MCP23S17)
// ==========================================

void SPI_INIT_BUS() {
    // Wspólna inicjalizacja magistrali SPI dla LCD i CC1101
    gpio_reset_pin(PIN_NUM_RST);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_RST, 1);

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64
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

    spi_device_polling_transmit(spi_lcd_handle, &t);
}

void MCP_Init(void) {
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_MASTER_FREQ_HZ,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS, // Pin CS dedykowany tylko dla ekranu
        .queue_size = 7
    };

    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi_lcd_handle));

    // Reset sprzętowy
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Ustawienie Portu A i B jako wyjścia
    MCP_Write(MCP_IODIRA, 0x00);
    MCP_Write(MCP_IODIRB, 0x00);
    MCP_Write(MCP_GPIOA, 0x00);
    MCP_Write(MCP_GPIOB, 0x00);
}

void LCD_ENABLE() {
    portB_shadow |= LCD_E_BIT;           // E = 1
    MCP_Write(MCP_GPIOB, portB_shadow);
    esp_rom_delay_us(2);                 // Minimalnie dłuższy impuls E
    
    portB_shadow &= ~LCD_E_BIT;          // E = 0
    MCP_Write(MCP_GPIOB, portB_shadow);
    esp_rom_delay_us(100);               // Margines na przetworzenie znaku
}

void LCD_Send8Bit(uint8_t val, uint8_t is_cmd) {
    if (is_cmd) portB_shadow &= ~LCD_RS_BIT; 
    else        portB_shadow |=  LCD_RS_BIT; 
    
    MCP_Write(MCP_GPIOB, portB_shadow); 
    MCP_Write(MCP_GPIOA, val);
    LCD_ENABLE();
}

void LCD_SEND_COMMAND(uint8_t data) { LCD_Send8Bit(data, 1); }
void LCD_SEND_DATA(uint8_t data)    { LCD_Send8Bit(data, 0); }

void LCD_INIT() {
    vTaskDelay(pdMS_TO_TICKS(50)); 
    LCD_SEND_COMMAND(0b00110000); esp_rom_delay_us(5000);
    LCD_SEND_COMMAND(0b00110000); esp_rom_delay_us(150);
    LCD_SEND_COMMAND(0b00110000);

    LCD_SEND_COMMAND(0b00111000); // 8-bit, 2 linie, 5x8
    LCD_SEND_COMMAND(0b00001100); // Włącz LCD, wyłącz kursor
    LCD_SEND_COMMAND(0b00000001); // Wyczyść ekran
    esp_rom_delay_us(3000); 
    LCD_SEND_COMMAND(0b00000110); // Przesuwaj w prawo
}

// ULEPSZONE: Obsługa wyświetlacza 4x20
void LCD_SET_CURSOR(uint8_t row, uint8_t column) {
    // Adresy początkowe wierszy w standardzie HD44780 dla ekranów 4x20
    uint8_t row_offsets[] = { 0x00, 0x40, 0x14, 0x54 }; 
    if (row > 3) row = 3; // Zabezpieczenie przed wyjściem poza tablicę
    LCD_SEND_COMMAND(0x80 | (row_offsets[row] + column));
}

void LCD_PRINT_CHAR(char character) {
    LCD_SEND_DATA((uint8_t)character);
}

void LCD_PRINT_STRING(const char* str) {
    while (*str) LCD_PRINT_CHAR(*str++);
}


// ==========================================
// ZADANIE ODBIERANIA DANYCH (FreeRTOS)
// ==========================================

void CC1101_receive_weather_task(void *pvParameters) {
    uint8_t data_length = 0;
    uint8_t status_length = 0;
    uint8_t data_buffer[64] = {0};
    uint8_t status_buffer[2] = {0};

    sensor_packet_t pkt;
    char lcd_buffer[21]; // Bufor na 20 znaków + \0

    ESP_LOGI(TAG, "Nasłuchiwanie na antenie ...");

    // Czysty ekran startowy
    LCD_SET_CURSOR(0, 0); LCD_PRINT_STRING("Stacja Pogodowa V1.0");
    LCD_SET_CURSOR(1, 0); LCD_PRINT_STRING("Oczekiwanie na dane.");

    while(1) {
        // Nasłuchiwanie na antenie  (zgodnie z Waszym oryginalnym zamysłem)
        if(cc1101_receive_packet(data_buffer, status_buffer, &data_length, &status_length) == true) {
            
            if(data_length == sizeof(sensor_packet_t)) {
                memcpy(&pkt, data_buffer, sizeof(sensor_packet_t));

                // Przeliczenia
                float temp = pkt.temp_hundredths / 100.0f;
                float press = pkt.pressure_pa / 100.0f; 
                float hum = pkt.hum_x1024 / 1024.0f;
                float bat = pkt.battery_mv / 1000.0f;

                ESP_LOGI(TAG, "Dane: T=%.2fC P=%.1fhPa H=%.1f%% B=%.2fV", temp, press, hum, bat);

                // --- WYŚWIETLANIE NA LCD ---
                // Wiersz 1: Temperatura
                sprintf(lcd_buffer, "Temp: %.2f \xDF" "C   ", temp); // \xDF to znak stopnia w HD44780
                LCD_SET_CURSOR(0, 0);
                LCD_PRINT_STRING(lcd_buffer);

                // Wiersz 2: Wilgotność
                sprintf(lcd_buffer, "Wilg: %.1f %%      ", hum);
                LCD_SET_CURSOR(1, 0);
                LCD_PRINT_STRING(lcd_buffer);

                // Wiersz 3: Ciśnienie
                sprintf(lcd_buffer, "Cisn: %.1f hPa    ", press);
                LCD_SET_CURSOR(2, 0);
                LCD_PRINT_STRING(lcd_buffer);

                // Wiersz 4: Bateria
                sprintf(lcd_buffer, "Bat : %.2f V      ", bat);
                LCD_SET_CURSOR(3, 0);
                LCD_PRINT_STRING(lcd_buffer);
                
            } else {
                ESP_LOGW(TAG, "Zły rozmiar paczki! %d bajtów", data_length);
            }
        }
        
        // Zabezpieczenie przed wygłodzeniem Watchdoga
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ==========================================
// PĘTLA GŁÓWNA
// ==========================================
void app_main(void) {
    ESP_LOGI(TAG, "Inicjalizacja systemu...");

    // 1. Najpierw inicjujemy fizyczną magistralę SPI na płycie
    SPI_INIT_BUS();
    
    // 2. Dodajemy ekran LCD pod magistralę SPI i inicjujemy
    MCP_Init();
    LCD_INIT();
    
    // 3. Dodajemy CC1101 pod magistralę SPI (pamiętaj o modyfikacji spi_init()!)
    spi_init();
    cc1101_init(); // Inicjujemy antenę nr 2

    // 4. Odpalamy zadanie nasłuchu z priorytetem 5
    xTaskCreate(CC1101_receive_weather_task, "CC1101_RX", 4096, NULL, 5, NULL);
}