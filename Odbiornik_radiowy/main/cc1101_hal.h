#include <stdio.h>
#include <stdint.h>

void spi_init(); // SPI interface initialization
void cc1101_write_reg(uint8_t reg_adr, uint8_t value); // function to overwrite a register
uint8_t cc1101_read_reg(uint8_t reg_adr); // function to read a register -> I will use a queue to write values
void cc1101_write_burst(uint8_t reg_adr, uint8_t *buffer, uint8_t length);
uint8_t cc1101_read_burst(uint8_t reg_adr, uint8_t *buffer, uint8_t length); // burst mode read
uint8_t cc1101_set_strobe(uint8_t strb_cmd);
