/*
 AVR (np. UNO)           MCP23017 (DIP28)                 LCD 16x2 (HD44780)
       +-------------+        +-------U--------+               +------------------+
       |             |        |                |               |                  |
       |         +5V +--------+ 9 (VDD)        |       GND ----+ 1  VSS           |
       |         GND +---+----+ 10 (VSS)       |       +5V ----+ 2  VDD           |
       |             |   |    |                |       |       | 3  V0 (Kontrast) |
       |             |   |    | [PORT B - Ster]|       |       |                  |
(SCL)  | A5 (PC5)    +---+----+ 12 (SCL)   GPB0+---------------+ 4  RS            |
(SDA)  | A4 (PC4)    +---+----+ 13 (SDA)   GPB1+---------------+ 6  E             |
       |             |   |    |                |       GND ----+ 5  RW            |
       |             |   |    |            GPB3+--[220R]-------+ 15 LED+ (Anoda)  |
       |             |   |    |                |       GND ----+ 16 LED- (Katoda) |
       |             |   +----+ 15 (A0) -> GND |               |                  |
       |             |   +----+ 16 (A1) -> GND |               | [PORT A - Dane]  |
       |             |   +----+ 17 (A2) -> GND |           +---+                  |
       |             |   |    |                |           |   |                  |
       |             |   +----+ 18 (RESET) +5V |       GPA0+---+ 7  D0            |
       +-------------+        |                |       GPA1+---+ 8  D1            |
                              |                |       GPA2+---+ 9  D2            |
 POTENCJOMETR 10k             |                |       GPA3+---+ 10 D3            |
  +5V ---+                    |                |       GPA4+---+ 11 D4            |
         |                    |                |       GPA5+---+ 12 D5            |
         <-------do V0 (Pin3) |                |       GPA6+---+ 13 D6            |
  GND ---+        LCD         |                |       GPA7+---+ 14 D7            |
                              +----------------+               +------------------+
*/

#include <avr/io.h>
#include <stdio.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define F_CPU 16000000UL //czestotliwość zegara 16 MHz

#define MCP_RESET PB1

//Definicje Rejestrów MCP23S17 (BANK=0)
#define MCP_IODIRA 0x00
#define MCP_IODIRB 0x01
#define MCP_GPIOA  0x12 //Można też użyć OLATA (0x14)
#define MCP_GPIOB  0x13 //Można też użyć OLATB (0x15)
#define MCP_OLATA  0x14
#define MCP_OLATB  0x15

#define MCP_OPCODE 0x40 // A0,A1,A2 = GND

//Piny sterujące na PORCIE B (MCP23017)
#define LCD_RS_BIT (1 << 0)
#define LCD_E_BIT  (1 << 1)
#define LCD_BL_BIT (1 << 3)

uint8_t portB_shadow = 0;//zmienna globalna dla stanu portu B układu MCP 

volatile uint16_t seconds = 0;

void timer1_init(){
  TCCR1B |= (1<<WGM12)|(1<<CS12)|(1<<CS10);
  OCR1A = 15624; 
  TIMSK1 |= (1<<OCIE1A);
  sei();
}

//Obsługa sprzętowa I2C (w AVR TWI)

void I2C_Init() {
    TWSR = 0x00;
    TWBR = 0x2; //Prędkość transmisji
    TWCR = (1 << TWEN);
}

void I2C_Start() {
    TWCR = (1 << TWSTA) | (1 << TWEN) | (1 << TWINT);
    while (!(TWCR & (1 << TWINT)));
}

void I2C_Stop() {
    TWCR = (1 << TWSTO) | (1 << TWEN) | (1 << TWINT);

    //ta poprawka naprawiła problem komunikacji z MCP, przy większych prędkosciach znak start nadpisywal STOP i uklad sie gubil
    while(TWCR & (1 << TWSTO));
    _delay_us(10);
}

void I2C_Write(uint8_t data) {
    TWDR = data; 
    TWCR = (1 << TWEN) | (1 << TWINT); 
    while (!(TWCR & (1 << TWINT)));
}

// --- Obsługa MCP23017 ---
void MCP_Write(uint8_t reg, uint8_t data) {
    I2C_Start();
    I2C_Write(MCP_OPCODE); //Wysłanie adresu urządzenia (z bitem zapisu)
    I2C_Write(reg);        //Wysłanie adresu rejestru
    I2C_Write(data);       //Wysłanie danych
    I2C_Stop();
}

void MCP_Init(void) {
    // 1. Obsługa sprzętowa pinu RESET
    DDRB |= (1 << MCP_RESET);   // Ustaw pin jako wyjście
    PORTB &= ~(1 << MCP_RESET); // TWARDE ZEROWANIE (Reset układu)
    _delay_ms(20);              
    PORTB |= (1 << MCP_RESET);  // Wznowienie pracy (Stan wysoki)
    _delay_ms(50);              // Czekamy na stabilizację napięć

    // 2. Bezpośrednie (niesekwencyjne) ustawienie wszystkich pinów jako WYJŚCIA
    MCP_Write(MCP_IODIRA, 0x00); // Port A w całości jako wyjścia
    MCP_Write(MCP_IODIRB, 0x00); // Port B w całości jako wyjścia
    
    // 3. Stan niski na start
    MCP_Write(MCP_GPIOA, 0x00);
    MCP_Write(MCP_GPIOB, 0x00);
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
    I2C_Init();
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
