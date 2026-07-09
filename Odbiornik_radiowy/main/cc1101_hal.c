#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "cc1101_regs.h"
#include "cc1101_hal.h"

// SPI pin definitions

#define SCLK GPIO_NUM_18
#define MOSI GPIO_NUM_23
#define MISO GPIO_NUM_19
#define CS_CC1101 GPIO_NUM_21
#define GD0_1 GPIO_NUM_13
#define GD0_2 GPIO_NUM_12

// SPI handles for antennas

spi_device_handle_t antena;

// SPI Initialization

void spi_init(){

    spi_device_interface_config_t spi_config = {
        .mode = 0, // mode 0
        .clock_speed_hz = 500000, // 500 kHz
        .spics_io_num = -1, // manual CS control
        .queue_size = 7, // queue size
    };

    spi_bus_config_t bus_config = {
        .sclk_io_num = SCLK,
        .mosi_io_num = MOSI,
        .miso_io_num = MISO,
        .quadhd_io_num = -1,
        .quadwp_io_num = -1,
        .max_transfer_sz = 32, // maximum data size in bytes
    };
    
    spi_bus_add_device(SPI2_HOST, &spi_config, &antena);
    gpio_set_direction(CS_CC1101, GPIO_MODE_OUTPUT);
    gpio_set_direction(GD0_1, GPIO_MODE_INPUT);
    gpio_set_direction(GD0_2, GPIO_MODE_INPUT);
    gpio_set_level(CS_CC1101, 1); // initially set CS to 1 so it's inactive
    static const char *TAG = "SPI"; 
    ESP_LOGI(TAG, "SPI Initialization ready!");
}

// Functions for single-byte mode

void cc1101_write_reg(uint8_t reg_adr, uint8_t value){

    static const char *TAG = "CC1101 WRITE";

    uint8_t tx_data[2] = {(reg_adr | SINGLE_BYTE_WRITE), value}; // add mask for writing a single message
    spi_transaction_t tx_transaction = {
        .tx_buffer = &tx_data,
        .length = 8*sizeof(tx_data),
    };

    spi_device_handle_t temp_handle; // variable because we handle two antennas

   
        gpio_set_level(CS_CC1101, 0); // pull down CS to wake up the receiver
        temp_handle = antena;
        spi_device_polling_transmit(temp_handle, &tx_transaction); // send our data
        gpio_set_level(CS_CC1101, 1); // end transmission
    

}

uint8_t cc1101_read_reg(uint8_t reg_adr){

    static const char *TAG = "CC1101 READ";

    uint8_t tx_data[2] = {(reg_adr | SINGLE_BYTE_READ), 0x00}; 
    // query the given register in the first round, and then send 0x00 to query nothing but receive register value
    uint8_t rx_data[2]; // buffer for received register data

    spi_transaction_t rx_transaction = {
        .tx_buffer = &tx_data,
        .rx_buffer = &rx_data,
        .length = 8*sizeof(tx_data),
    };

     spi_device_handle_t temp_handle; // variable because we handle two antennas

        gpio_set_level(CS_CC1101, 0); 
        temp_handle = antena;
        spi_device_polling_transmit(temp_handle, &rx_transaction); // send our data
        gpio_set_level(CS_CC1101, 1); 
        return rx_data[1]; 
        // get data from the second transmission round. In the 1st we sent the register address and the receiver sent us garbage. 
        // In the 2nd round we receive the correct register value and send 0
    
    
}

// Functions for burst mode (BURST)

void cc1101_write_burst(uint8_t reg_adr, uint8_t *buffer, uint8_t length){
    static const char *TAG = "CC1101 BURST WRITE";

    // Create a buffer larger by 1 byte (for the register address)
    uint8_t tx_data[length + 1];

    // 1. Put address with BURST_WRITE command into the first spot
    tx_data[0] = (reg_adr | BURST_WRITE);

    // 2. Copy our actual data right after the address (starting from index 1)
    memcpy(&tx_data[1], buffer, length);

    // 3. Configure ONE common transaction
    spi_transaction_t tx_transaction = {
        .tx_buffer = tx_data,
        .length = 8 * (length + 1), // Total length: 1 byte address + data length
    };

    spi_device_handle_t temp_handle; 

        gpio_set_level(CS_CC1101, 0); 
        temp_handle = antena;
        // Send everything in one go!
        spi_device_polling_transmit(temp_handle, &tx_transaction); 
        gpio_set_level(CS_CC1101, 1); 
    
}

uint8_t cc1101_read_burst(uint8_t reg_adr, uint8_t *buffer, uint8_t length) {
    static const char *TAG = "CC1101 BURST READ";

    // Create enlarged buffers: +1 byte for the register address itself
    uint8_t tx_temp[length + 1];
    uint8_t rx_temp[length + 1];

    // Fill transmit buffer with zeros (dummy clock cycles for reading)
    memset(tx_temp, 0, length + 1);
    
    // Put register address with BURST_READ flag in the first spot
    tx_temp[0] = (reg_adr | BURST_READ); 

    // Configure ONE uninterrupted transaction
    spi_transaction_t transaction = {
        .length = 8 * (length + 1),
        .tx_buffer = tx_temp,
        .rx_buffer = rx_temp,
    };

    spi_device_handle_t temp_handle;

    
        gpio_set_level(CS_CC1101, 0); 
        temp_handle = antena;
        spi_device_polling_transmit(temp_handle, &transaction); 
        gpio_set_level(CS_CC1101, 1); 


    // Extract clean data
    // rx_temp[0] is status (garbage), so we copy data starting from rx_temp[1]
    memcpy(buffer, &rx_temp[1], length);

    return rx_temp[0]; // Return status byte if ever needed in the future
}

// Strobe configuration

uint8_t cc1101_set_strobe(uint8_t strb_cmd){

    static const char *TAG = "CC1101 STROBE";

    uint8_t tx_data = strb_cmd;
    uint8_t rx_data; 

    spi_transaction_t rx_transaction = {
        .tx_buffer = &tx_data,
        .rx_buffer = &rx_data,
        .length = 8*sizeof(tx_data),
    };

     spi_device_handle_t temp_handle; // variable because we handle two antennas

        gpio_set_level(CS_CC1101, 0); 
        temp_handle = antena;
        spi_device_polling_transmit(temp_handle, &rx_transaction); 
        gpio_set_level(CS_CC1101, 1); 
        return rx_data; // get status after strobe
   
}
