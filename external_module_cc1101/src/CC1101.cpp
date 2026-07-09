#include <avr/io.h>
#include <util/delay.h>
#include "SPI.h"
#include "CC1101.h"

// Makra do sterowania pinem CSN. Zależnie od tego jak nazwałeś go w Config.h
#ifndef CC1101_CSN_PIN
#define CC1101_CSN_PIN NRF_CSN_PIN // Fallback do nazwy pinu z NRF, jeśli nie zmieniłeś
#endif

#define CC1101_CSN_HIGH()  (SPI_PORT.OUTSET = CC1101_CSN_PIN)
#define CC1101_CSN_LOW()   (SPI_PORT.OUTCLR = CC1101_CSN_PIN)

// Magiczne wartości rejestrów z programu SmartRF Studio
static const cc1101_cfg_t cfg_register[] = {
    { IOCFG0, 0x06 },
    { FIFOTHR, 0x47 },
    { SYNC1, 0x2D },
    { SYNC0, 0xD4 },
    { PKTLEN, 0x3D },
    { PKTCTRL1, 0x04 },
    { PKTCTRL0, 0x45 },
    { FSCTRL1, 0x06 },
    { FREQ2, 0x21 },
    { FREQ1, 0x65 },
    { FREQ0, 0x6A },
    { MDMCFG4, 0xCA },
    { MDMCFG3, 0x83 },
    { MDMCFG2, 0x16 },
    { DEVIATN, 0x40 },
    { MCSM0, 0x18 },
    { FOCCFG, 0x16 },
    { AGCTRL2, 0x43 },
    { AGCTRL1, 0x49 },
    { WORCTRL, 0xFB },
    { FSCAL3, 0xE9 },
    { FSCAL2, 0x2A },
    { FSCAL1, 0x00 },
    { FSCAL0, 0x1F },
    { TEST2, 0x81 },
    { TEST1, 0x35 },
    { TEST0, 0x09 }
};

uint8_t CC1101_set_strobe(uint8_t strb_cmd) {
    CC1101_CSN_LOW();
    uint8_t status = SPI_transfer(strb_cmd);
    CC1101_CSN_HIGH();
    return status;
}

void CC1101_write_reg(uint8_t reg_adr, uint8_t value) {
    CC1101_CSN_LOW();
    SPI_transfer(reg_adr | SINGLE_BYTE_WRITE);
    SPI_transfer(value);
    CC1101_CSN_HIGH();
}

uint8_t CC1101_read_reg(uint8_t reg_adr) {
    CC1101_CSN_LOW();
    SPI_transfer(reg_adr | SINGLE_BYTE_READ);
    uint8_t val = SPI_transfer(0x00);
    CC1101_CSN_HIGH();
    return val;
}

void CC1101_power_on_reset(void) {
    uint8_t status = CC1101_set_strobe(SRES);
    
    // Dopóki bit CHIP_RDYn (bit 7) jest jedynką, układ się jeszcze nie zresetował
    while ((status & 0x80) != 0) {
        status = CC1101_set_strobe(SNOP); // Odpytaj o status
        _delay_us(10);
    }
}

void CC1101_wake_up(void) {
    CC1101_CSN_LOW();
    _delay_ms(1); // 1000us
    CC1101_CSN_HIGH();
}

void CC1101_sleep(void) {
    CC1101_set_strobe(SPWD); // Uśpij radio (Power Down)
}

void CC1101_init(void) {
    SPI_PORT.DIRSET = CC1101_CSN_PIN; // Ustaw CSN jako wyjście
    CC1101_CSN_HIGH();
    
    SPI_init(); // Inicjalizacja sprzętowego SPI mikrokontrolera
    
    CC1101_power_on_reset();
    
    // (Opcjonalnie) Odczyt wersji, żeby zweryfikować magistralę SPI
    // uint8_t version = CC1101_read_reg(VERSION);

    // Wgranie całej konfiguracji rejestrów z tablicy
    for(uint8_t i = 0; i < (sizeof(cfg_register)/sizeof(cfg_register[0])); i++) {
        CC1101_write_reg(cfg_register[i].addr, cfg_register[i].data);
    }
}

void CC1101_send_packet(sensor_packet_t *pkt) {
    CC1101_set_strobe(SFTX); // Wyczyść bufor TX (Flush TX)
    
    uint8_t length = sizeof(sensor_packet_t);
    uint8_t *p = (uint8_t*)pkt;

    CC1101_CSN_LOW();
    // 1. Zapisz adres FIFO w trybie zapisu ciągłego (BURST)
    SPI_transfer(TX_RX_FIFO | BURST_WRITE);
    
    // 2. Pierwszy bajt to długość payloadu (wymagane w Twojej konfiguracji ESP32)
    SPI_transfer(length);
    
    // 3. Wypchnij bezpośrednio sprzętowo resztę paczki (bez zbędnego buforowania RAM)
    for(uint8_t i = 0; i < length; i++) {
        SPI_transfer(p[i]);
    }
    CC1101_CSN_HIGH();

    // Wyślij sygnał STROBE do wysłania ramki
    CC1101_set_strobe(STX);
}
