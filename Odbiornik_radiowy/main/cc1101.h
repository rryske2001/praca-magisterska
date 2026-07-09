#include <stdbool.h>

typedef struct 
{
    uint8_t addr;
    uint8_t data;
    
} cc1101_cfg; // structure for address-value type registers for quick initialization

void cc1101_power_on_reset();
void cc1101_wake_up();
void cc1101_init(); // CC1101 integrated circuit initialization
void cc1101_send_packet(uint8_t *payload, uint8_t length);
bool cc1101_receive_packet(uint8_t *data_bufffer, uint8_t *status_buffer, uint8_t *data_length, uint8_t *status_length);
