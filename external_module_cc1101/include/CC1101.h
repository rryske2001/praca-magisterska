#ifndef CC1101_H
#define CC1101_H

#include <stdint.h>
#include <stdbool.h>
#include "Config.h" // Zakładam, że tu masz strukturę sensor_packet_t

// Typy transmisji SPI
#define BURST_READ         0xC0
#define SINGLE_BYTE_READ   0x80
#define BURST_WRITE        0x40
#define SINGLE_BYTE_WRITE  0x00

// Strobe commands
#define SRES    0x30 // Reset CC1101
#define SFSTXON 0x31
#define SXOFF   0x32
#define SCAL    0x33
#define SRX     0x34
#define STX     0x35
#define SIDLE   0x36
#define SWOR    0x38
#define SPWD    0x39
#define SFRX    0x3A
#define SFTX    0x3B
#define SWORRST 0x3C
#define SNOP    0x3D

// Wybrane rejestry
#define IOCFG2   0x00
#define IOCFG0   0x02
#define FIFOTHR  0x03
#define SYNC1    0x04
#define SYNC0    0x05
#define PKTLEN   0x06
#define PKTCTRL1 0x07
#define PKTCTRL0 0x08
#define FSCTRL1  0x0B
#define FREQ2    0x0D
#define FREQ1    0x0E
#define FREQ0    0x0F
#define MDMCFG4  0x10
#define MDMCFG3  0x11
#define MDMCFG2  0x12
#define DEVIATN  0x15
#define MCSM0    0x18
#define FOCCFG   0x19
#define AGCTRL2  0x1B
#define AGCTRL1  0x1C
#define WORCTRL  0x20
#define FSCAL3   0x23
#define FSCAL2   0x24
#define FSCAL1   0x25
#define FSCAL0   0x26
#define TEST2    0x2C
#define TEST1    0x2D
#define TEST0    0x2E
#define VERSION  0x31
#define TX_RX_FIFO 0x3F

typedef struct {
    uint8_t addr;
    uint8_t data;
} cc1101_cfg_t;

// Deklaracje funkcji
void CC1101_init(void);
void CC1101_wake_up(void);
void CC1101_sleep(void);
uint8_t CC1101_set_strobe(uint8_t strb_cmd);
void CC1101_write_reg(uint8_t reg_adr, uint8_t value);
uint8_t CC1101_read_reg(uint8_t reg_adr);
void CC1101_send_packet(sensor_packet_t *pkt);

#endif // CC1101_H
