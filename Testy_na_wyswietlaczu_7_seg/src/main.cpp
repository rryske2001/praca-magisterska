/*
Start: Program konfiguruje SPI i przyciski, włącza przerwania (sei).

Pętla: Wchodzi do while(1).

Reset: Od razu robi MCP_SoftReset() (reset sprzętowy + konfiguracja I/O).

Test: Sprawdza, jaki jest test_mode (domyślnie 0) i wykonuje sekwencję mrugania (z opóźnieniami delay).

Przycisk: Jeśli w dowolnym momencie (nawet w trakcie delay) naciśniesz przycisk:

Procesor przerywa na mikrosekundę to co robi.

Wykonuje ISR(PCINT2_vect), zmieniając zmienną test_mode.

Wraca do delay i czeka aż ten obecny czas minie.

Zmiana: Gdy delay się skończy i pętla while obróci się jeszcze raz, program zobaczy nową wartość test_mode i wejdzie do nowego bloku if/else.
*/

#include <avr/io.h>
#include <stdio.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define F_CPU 16000000UL //czestotliwość zegara 16 MHz
#define MISO PB4
#define MOSI PB3
#define SCK PB5
#define SS PB2
#define MCP_RESET PB1

//Definicje Rejestrów MCP23S17 (BANK=0)
#define MCP_IODIRA 0x00
#define MCP_IODIRB 0x01
#define MCP_GPIOA  0x12 //Można też użyć OLATA (0x14)
#define MCP_GPIOB  0x13 //Można też użyć OLATB (0x15)
#define MCP_OLATA  0x14
#define MCP_OLATB  0x15

#define MCP_OPCODE 0x40 // A0,A1,A2 = GND

#define BTN1 PD0
#define BTN2 PD1
#define BTN3 PD2
#define BTN4 PD3
#define BTNSEL PD4

volatile uint16_t seconds = 0;

uint8_t portB_shadow = 0;//zmienna globalna dla stanu portu B układu MCP 

volatile uint8_t test_mode = 0;

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

void SPI_INIT()
{
  DDRB |= (1 << MOSI) | (1 << SCK) | (1 << SS) | (1 << MCP_RESET); //ustawienie pinów jako wyjścia
  DDRB &= ~(1 << MISO); //ustawienie pinów jako wejścia
  PORTB |= (1 << SS) | (1 << MCP_RESET); //ustawienie SS i MCP_RESET na wysokim poziomie
  SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0); //włączenie SPI jako tryb master i zegar FCPU/16
}

uint8_t SPI_Transfer(uint8_t data)
{
  SPDR = data; //wysłanie bajtu
  while (!(SPSR & (1 << SPIF))); //czekanie na zakończenie transmisji
  return SPDR;
}

// --- Obsługa MCP23S17 ---
void MCP_Write(uint8_t reg, uint8_t data) {
    PORTB &= ~(1 << SS); // CS Low (Start)
    _delay_ms(1);
    SPI_Transfer(MCP_OPCODE);
    SPI_Transfer(reg);
    SPI_Transfer(data);
    _delay_ms(1);
    PORTB |= (1 << SS);  // CS High (Stop)
}

void MCP_Init(void) {
    // 1. Ustawienie IODIRA i IODIRB na 0x00 (Wszystkie piny jako WYJŚCIA)
    // Dzięki trybowi sekwencyjnemu (domyślnemu), można wysłać to jednym ciągiem!
    
    PORTB &= ~(1 << SS); // CS Low
    SPI_Transfer(MCP_OPCODE);
    SPI_Transfer(MCP_IODIRA); // Startujemy od adresu 0x00
    SPI_Transfer(0x00);       // Wpis do IODIRA (0x00) -> wskaźnik sam skoczy na 0x01
    SPI_Transfer(0x00);       // Wpis do IODIRB (0x01)
    PORTB |= (1 << SS);  // CS High
    _delay_ms(10);
    MCP_Write(MCP_GPIOB, 0x00);
    MCP_Write(MCP_GPIOA, 0x00);
}

void MCP_SoftReset() {
     PORTB &= ~(1 << MCP_RESET);
     _delay_ms(10);
     PORTB |= (1 << MCP_RESET);
     _delay_ms(10);
     MCP_Init(); // Konieczna re-inicjalizacja po resecie!
}

int main(void)
{
  SPI_INIT();
  MCP_Init();
  buttons_init();
  sei();
  
  while (1) {
    if (test_mode == 0) {
      //MCP_SoftReset();
      portB_shadow = 0x60;
      MCP_Write(MCP_GPIOA, portB_shadow);
      MCP_Write(MCP_GPIOB, portB_shadow);
      _delay_ms(3000);
      if( /*!(PIND & (1 << BTNSEL))*/ 1) //jezeli przycisk zwarty do GND
      {
        portB_shadow = 0b00000010;
        MCP_Write(MCP_GPIOA, portB_shadow);
        MCP_Write(MCP_GPIOB, portB_shadow);
        _delay_ms(1000);
      }
    }
    else if(test_mode == 1){
        //MCP_SoftReset();
        portB_shadow = 0x60;
        MCP_Write(MCP_OLATA, portB_shadow);
        MCP_Write(MCP_OLATB, portB_shadow);
        _delay_ms(3000);
        if( !(PIND & (1 << BTNSEL))) //jezeli przycisk zwarty do GND
        {
          portB_shadow = 0b00000010;
          MCP_Write(MCP_OLATA, portB_shadow);
          MCP_Write(MCP_OLATB, portB_shadow);
          _delay_ms(1000);
        }
    }
    else if(test_mode == 2){
        //MCP_SoftReset();
        portB_shadow = 0x60;
        MCP_Write(MCP_OLATA, portB_shadow);
        MCP_Write(MCP_OLATB, portB_shadow);
        _delay_ms(3000);
        portB_shadow = 0xDA;
        MCP_Write(MCP_GPIOA, portB_shadow);
        MCP_Write(MCP_GPIOB, portB_shadow);
        _delay_ms(3000);
    }
    else if(test_mode == 3){
        //MCP_SoftReset();
        portB_shadow = 0x60;
        MCP_Write(MCP_GPIOA, portB_shadow);
        MCP_Write(MCP_GPIOB, portB_shadow);
        _delay_ms(3000);
        portB_shadow = 0xDA;
        MCP_Write(MCP_OLATA, portB_shadow);
        MCP_Write(MCP_OLATB, portB_shadow);
        _delay_ms(3000);
    }
  }
  return 0;
}

ISR(PCINT2_vect) {
    // Przerwanie wywołuje się przy ZMIANIE stanu (wciśnięcie I puszczenie).
    // Musimy sprawdzić, który przycisk jest w stanie NISKIM (wciśnięty).
    
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