/*
AVR (np. UNO)           MCP23S17 (DIP28)                 LCD 16x2 (HD44780)
      +-------------+        +-------U--------+              +------------------+
      |             |        |                |              |                  |
      |         +5V +--------+ 9 (VDD)        |      GND ----+ 1  VSS           |
      |         GND +---+----+ 10 (VSS)       |      +5V ----+ 2  VDD           |
      |             |   |    |                |       |      | 3  V0 (Kontrast) |
      |             |   |    | [PORT B - Ster]|       |      |                  |
(SS)  | PB2 (D10)   +--------+ 11 (CS)    GPB0+--------------+ 4  RS            |
(MOSI)| PB3 (D11)   +--------+ 13 (SI)    GPB1+--------------+ 6  E             |
(MISO)| PB4 (D12)   +--------+ 14 (SO)        |      GND ----+ 5  RW            |
(SCK) | PB5 (D13)   +--------+ 12 (SCK)   GPB3+--[220R]------+ 15 LED+ (Anoda)  |
      |             |   |    |                |      GND ----+ 16 LED- (Katoda) |
      |             |   +----+ 15 (A0) -> GND |              |                  |
      |             |   +----+ 16 (A1) -> GND |              | [PORT A - Dane]  |
      |             |   +----+ 17 (A2) -> GND |          +---+                  |
      |             |   |    |                |          |   |                  |
      |             |   +----+ 18 (RESET) +5V |      GPA0+---+ 7  D0            |
      |             |        |                |      GPA1+---+ 8  D1            |
      +-------------+        |                |      GPA2+---+ 9  D2            |
                             |                |      GPA3+---+ 10 D3            |
 POTENCJOMETR 10k            |                |      GPA4+---+ 11 D4            |
  +5V ---+                   |                |      GPA5+---+ 12 D5            |
         |                   |                |      GPA6+---+ 13 D6            |
         <-------do V0 (Pin3)|                |      GPA7+---+ 14 D7            |
  GND ---+        LCD        |                |              |                  |
                             +----------------+              +------------------+
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

//Adres I2C układu MCP23017
//Bazowy to 0x20. Gdy A0, A1, A2 = GND -> Adres = 0x20.
//Do zapisu przesuwamy o 1 w lewo: 0x20 << 1 = 0x40
#define MCP_OPCODE 0x40 // A0,A1,A2 = GND

//Piny sterujące na PORCIE B (MCP23S17)
#define LCD_RS_BIT (1 << 0)
#define LCD_E_BIT  (1 << 1)
#define LCD_BL_BIT (1 << 3)

volatile uint16_t seconds = 0;

uint8_t portB_shadow = 0;//zmienna globalna dla stanu portu B układu MCP 

void timer1_init(){
  TCCR1B |= (1<<WGM12)|(1<<CS12)|(1<<CS10);
  OCR1A = 15624; 
  TIMSK1 |= (1<<OCIE1A);
  sei();
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

void LCD_ENABLE()
{
    // E High
    portB_shadow |= LCD_E_BIT;
    MCP_Write(MCP_GPIOB, portB_shadow);
    _delay_us(1);
    
    // E Low
    portB_shadow &= ~LCD_E_BIT;
    MCP_Write(MCP_GPIOB, portB_shadow);
    _delay_us(50);
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

void LCD_SEND_COMMAND (uint8_t data_to_send)
{
  LCD_Send8Bit(data_to_send, 1);
}

void LCD_SEND_DATA (uint8_t data_to_send)
{
  LCD_Send8Bit(data_to_send, 0);
}

void LCD_INIT()
{
  _delay_ms(40);

  LCD_SEND_COMMAND(0b00110000);
  _delay_ms(3);
  LCD_SEND_COMMAND(0b00110000);
  LCD_SEND_COMMAND(0b00110000);//3x powtórzenie komendy inicjalizacyjnej

  LCD_SEND_COMMAND(0b00111000);//8-bitowy tryb, 2 linie, 5x8 pikseli
  LCD_SEND_COMMAND(0b00001100);// Wlacz wyswietlacz, wylacz kursor
  LCD_SEND_COMMAND(0b00000001);// Wyczysc wyswietlacz
  _delay_ms(2);
  LCD_SEND_COMMAND(0b00000110); // Przesuwanie kursora w prawo
}

void LCD_SET_CURSOR(uint8_t row, uint8_t column)
{
  uint8_t address;
  if (row == 0)
    address = 0x00 + column;
  else
    address = 0x40 + column;
  LCD_SEND_COMMAND(0b10000000 | address);
}

void LCD_PRINT_CHAR(char character)
{
  LCD_SEND_DATA(static_cast<uint8_t>(character));
}

void LCD_PRINT_STRING(const char* str)
{
  while (*str)
  {
    LCD_PRINT_CHAR(*str++);
  }
}

int main(void)
{
  SPI_INIT();
  MCP_Init();
  LCD_INIT();
  timer1_init();
  
  LCD_SET_CURSOR(0, 0);
  LCD_PRINT_STRING("Hello, World!");

  char buffer[16];
    while (1) {
      LCD_SET_CURSOR(1,0);
      sprintf(buffer, "%u", seconds);
      LCD_PRINT_STRING(buffer);
      _delay_ms(200);
  }
  return 0;
}

ISR(TIMER1_COMPA_vect){
  seconds++;
}