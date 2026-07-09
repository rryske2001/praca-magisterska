#include<stdio.h>
#include <stdint.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include"FreeRTOSConfig.h"
#include"freertos\FreeRTOS.h"
#include"cc1101_regs.h"
#include"cc1101_hal.h"
#include"cc1101.h"

// Register values needed for configuration

static const cc1101_cfg cfg_regiser[] =
{
    { IOCFG0, 0x06 }, // iocfg0 - GDO0 Output Pin Configuration
    { FIFOTHR, 0x47 }, // fifothr - RX FIFO and TX FIFO Thresholds
    { SYNC1, 0x2D }, // sync1 - Sync Word, High Byte
    { SYNC0, 0xD4 }, // sync0 - Sync Word, Low Byte
    { PKTLEN, 0x3D }, // pktlen - Packet Length
    { PKTCTRL1, 0x04 }, // pktctrl1 - Packet Automation Control
    { PKTCTRL0, 0x45 }, // pktctrl0 - Packet Automation Control
    { FSCTRL1, 0x06 }, // fsctrl1 - Frequency Synthesizer Control
    { FREQ2, 0x21 }, // freq2 - Frequency Control Word, High Byte
    { FREQ1, 0x65 }, // freq1 - Frequency Control Word, Middle Byte
    { FREQ0, 0x6A }, // freq0 - Frequency Control Word, Low Byte
    { MDMCFG4, 0xCA }, // mdmcfg4 - Modem Configuration
    { MDMCFG3, 0x83 }, // mdmcfg3 - Modem Configuration
    { MDMCFG2, 0x16 }, // mdmcfg2 - Modem Configuration
    { DEVIATN, 0x40 }, // deviatn - Modem Deviation Setting
    { MCSM0, 0x18 }, // mcsm0 - Main Radio Control State Machine Configuration
    { FOCCFG, 0x16 }, // foccfg - Frequency Offset Compensation Configuration
    { AGCTRL2, 0x43 }, // agcctrl2 - AGC Control
    { AGCTRL1, 0x49 }, // agcctrl1 - AGC Control
    { WORCTRL, 0xFB }, // worctrl - Wake On Radio Control
    { FSCAL3, 0xE9 }, // fscal3 - Frequency Synthesizer Calibration
    { FSCAL2, 0x2A }, // fscal2 - Frequency Synthesizer Calibration
    { FSCAL1, 0x00 }, // fscal1 - Frequency Synthesizer Calibration
    { FSCAL0, 0x1F }, // fscal0 - Frequency Synthesizer Calibration
    { TEST2, 0x81 }, // test2 - Various Test Settings
    { TEST1, 0x35 }, // test1 - Various Test Settings
    { TEST0, 0x09 }, // test0 - Various Test Settings
};

// Pin configuration

#define GD0_1 GPIO_NUM_13
#define GD0_2 GPIO_NUM_12
#define CS_CC1101 GPIO_NUM_21

void cc1101_power_on_reset(){
    static const char *TAG = "RESET";
    uint8_t temp_rx_data; // variable to store the status log from the strobe
    temp_rx_data = cc1101_set_strobe(SRES); // reset the chip

    while ((temp_rx_data & 0b10000000) != 0)
    {
        ESP_LOGW(TAG, "The chip has not reset yet"); 
        // as long as the CHIP_RDYn status bit is 1, the chip has not reset yet
        temp_rx_data = cc1101_set_strobe(SNOP); 
        // generate a dummy strobe to update the status value
    }
    ESP_LOGI(TAG, "Antenna reset successfully");
}

void cc1101_wake_up(){
    static const char *TAG = "CC1101 WAKE UP";
    gpio_num_t temp_gpio;
    temp_gpio = CS_CC1101;

    gpio_set_level(temp_gpio, 0);
    esp_rom_delay_us(1000);
    gpio_set_level(temp_gpio, 1);
}

void cc1101_init(){
    static const char *TAG = "INIT";
    cc1101_power_on_reset(); // reset the chip
    uint8_t buffer = 0xFF;
    cc1101_read_burst(VERSION, &buffer, 1);

    if((int)buffer != 20){ // check communication with CC1101
        ESP_LOGW(TAG, "Expected 20, but read: %d", buffer);
        ESP_LOGE(TAG, "Incorrect register value! Check connections!");
    }
    else ESP_LOGI(TAG, "Register value correct. Communication works");

    int i = 0;
    while(i<(sizeof(cfg_regiser)/sizeof(cfg_regiser[0]))) // check the entire array size
    {
        cc1101_write_reg(cfg_regiser[i].addr, cfg_regiser[i].data); // write to the address
        i++;
    }
    ESP_LOGI(TAG, "Initialized CC1101");
}

void cc1101_send_packet(uint8_t *payload, uint8_t length){ // send packet
    cc1101_set_strobe(SFTX); // reset the transmit data buffer
    uint8_t tx_data [length + 1]; // buffer specifically created for variable length data
    tx_data[0] = length; // first send the data length info
    memcpy(&tx_data[1], payload, length); // start pasting our data into the artificially created frame
    cc1101_write_burst(TX_RX_FIFO, tx_data, sizeof(tx_data)); // send data to the transmit buffer
    cc1101_set_strobe(STX); // set strobe to send data
}

bool cc1101_receive_packet(uint8_t *data_buffer, uint8_t *status_buffer, 
                uint8_t *data_length, uint8_t *status_length){
    static const char *TAG = "CC1101 RECEIVE";
    gpio_num_t temp_gpio;
    temp_gpio = GD0_1;


    cc1101_set_strobe(SRX); 
    // set to read mode - we are now listening
    uint32_t time_cnt = 0;
    while(gpio_get_level(temp_gpio) == 0){ 
        // do nothing until the interrupt finishes, initially it is 0 according to the documentation
        if (time_cnt == 5000000) return false;
        else time_cnt++;
    }
    time_cnt = 0;
    while(gpio_get_level(temp_gpio) == 1){ 
        // do nothing until the interrupt finishes 
        // here we receive data and wait for it to completely move to the buffer
        if (time_cnt == 500000) return false;
        else time_cnt++;
    }
    uint8_t length = 0;
    cc1101_read_burst(TX_RX_FIFO, &length ,1); // we want to read 1 byte, which is the length
    if(length > 63){ // when the packet is dangerously large
        ESP_LOGW(TAG, "Detected packet length too large. Packet discarded. Length %d", length);
        cc1101_set_strobe(SFRX); // clear data from the buffer
        return false; // we don't want to pass this packet further
    }
    *data_length = length; 
    // save size to an external variable to use it in the later part of the code
    cc1101_read_burst(TX_RX_FIFO, data_buffer , length); 
    // write data to our array and pass it further into the code
    cc1101_read_burst(TX_RX_FIFO, status_buffer, 2); // fetch metadata
    *status_length = 2; 
    // known constant value that never changes

    if((status_buffer[1] & 0x80) != 128){
        ESP_LOGW(TAG, "CRC error detected. Packet discarded. Parameters: Length %d, Data: %d, LQI: %d", length, data_buffer[0], status_buffer[1]);
        cc1101_set_strobe(SFRX); // Clear buffer
        return false;
    }

    cc1101_set_strobe(SFRX); // Clear buffer
    return true; // data received, finished
}