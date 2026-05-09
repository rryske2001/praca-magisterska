#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include <stdio.h>

#define PCF8574_ADDRESS_A 0x20

#define RS 2 //P2
#define RW 1
#define EN 0 //P0

#define D0 0 //P0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7 //P7

#define BTN1 PD0 //
#define BTN2 PD1
#define BTN3 PD2
#define BTN4 PD3
#define BTNSEL PD4 //

volatile uint8_t test_mode = 0;
uint8_t to_send = 0; //rejestr sygnałów sterujących

// --- Inicjalizacja Przycisków na Przerwaniach (PCINT) ---
void buttons_init() {
    // 1. Ustawienie pinów jako wejścia
    DDRD &= ~((1<<BTN1) | (1<<BTN2) | (1<<BTN3) | (1<<BTN4) | (1<<BTNSEL));
    
    // 2. Włączenie Pull-Up (Wymagane, nawet przy debouncingu sprzętowym, chyba że jest zewnętrzny rezystor)
    PORTD |= (1<<BTN1) | (1<<BTN2) | (1<<BTN3) | (1<<BTN4) | (1<<BTNSEL);

    // 3. Włączenie przerwań PCINT dla grupy PCINT2 (Port D)
    PCICR |= (1 << PCIE2);

    // 4. Wybór konkretnych pinów, które mają wywołać przerwanie
    PCMSK2 |= (1 << PCINT16) | (1 << PCINT17) | (1 << PCINT18) | (1 << PCINT19);
    
    // Globalne przerwania włączamy w main przez sei()
}


void I2C_Init() {
    TWSR = 0x00;
    TWBR = 0x48;
    TWCR = (1 << TWEN);
}

void I2C_Start() {
    TWCR = (1 << TWSTA) | (1 << TWEN) | (1 << TWINT);
    while (!(TWCR & (1 << TWINT)));
}

void I2C_Stop() {
    TWCR = (1 << TWSTO) | (1 << TWEN) | (1 << TWINT);
}

void I2C_Write(uint8_t data) {
    TWDR = data; 
    TWCR = (1 << TWEN) | (1 << TWINT); 
    while (!(TWCR & (1 << TWINT)));
}

void PCF8574_Write(uint8_t data) {
    I2C_Start();
    I2C_Write(PCF8574_ADDRESS_A << 1); 
    I2C_Write(data);              
    I2C_Stop();
    
}


uint8_t PCF8574_Read() { 
    uint8_t data;
    I2C_Start();
    I2C_Write((PCF8574_ADDRESS_A << 1) | 1);
    TWCR = (1 << TWEN) | (1 << TWINT);    
    while (!(TWCR & (1 << TWINT)));
    data = TWDR;                      
    I2C_Stop();
    return data;
}

int main() {
    I2C_Init();
    buttons_init();
    sei();

    while (1) {
    if (test_mode == 0) {
      to_send= 0x60;
      PCF8574_Write(to_send);
      _delay_ms(3000);
      if( !(PIND & (1 << BTNSEL))) //jezeli przycisk zwarty do GND
      {
        to_send = 0b00000010;
        PCF8574_Write(to_send);
        _delay_ms(1000);
      }
    }
    else if(test_mode == 1){
        to_send = 0x60;
        PCF8574_Write(to_send);
        _delay_ms(3000);
        if( !(PIND & (1 << BTNSEL))) //jezeli przycisk zwarty do GND
        {
          to_send = 0b00000010;
          PCF8574_Write(to_send);
          _delay_ms(1000);
        }
    }
    else if(test_mode == 2){
        to_send = 0x60;
        PCF8574_Write(to_send);
        _delay_ms(3000);
        to_send = 0xDA;
        PCF8574_Write(to_send);
        _delay_ms(3000);
    }
    else if(test_mode == 3){
        to_send = 0x60;
        PCF8574_Write(to_send);
        _delay_ms(3000);
        to_send = 0xDA;
        PCF8574_Write(to_send);
        _delay_ms(3000);
    }
  }
  return 0;
}

ISR(PCINT2_vect) {
    //Przerwanie wywołuje się przy ZMIANIE stanu (wciśnięcie I puszczenie).
    //Należy sprawdzić, który przycisk jest w stanie NISKIM (wciśnięty).
    
    if (!(PIND & (1 << BTN1))) {
        test_mode = 0;
    }
    else if (!(PIND & (1 << BTN2))) {
        test_mode = 1;
    }
    else if (!(PIND & (1 << BTN3))) {
        test_mode = 2;
    }
    else if (!(PIND & (1 << BTN4))) {
        test_mode = 3;
    }
}
