#include <avr/io.h>
#include <stdio.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define F_CPU 16000000UL //czestotliwość zegara 16 MHz
//Definicje pinów SPI
#define MISO PB4
#define MOSI PB3
#define SCK PB5
#define SS PB2

#define MCP0_RESET PB1
#define MCPS_RESET PB0

//Definicje Rejestrów MCP23017 (BANK=0)
#define MCP0_IODIRA 0x00
#define MCP0_IODIRB 0x01
#define MCP0_GPIOA  0x12 //Można też użyć OLATA (0x14)
#define MCP0_GPIOB  0x13 //Można też użyć OLATB (0x15)
#define MCP0_OLATA  0x14
#define MCP0_OLATB  0x15

//Definicje Rejestrów MCP23S17 (BANK=0)
#define MCPS_IODIRA 0x00
#define MCPS_IODIRB 0x01
#define MCPS_GPIOA  0x12 //Można też użyć OLATA (0x14)
#define MCPS_GPIOB  0x13 //Można też użyć OLATB (0x15)
#define MCPS_OLATA  0x14
#define MCPS_OLATB  0x15

#define MCP0_OPCODE 0x40 // A0,A1,A2 = GND
#define MCPS_OPCODE 0x40 // A0,A1,A2 = GND

//Piny sterujące na PORCIE A (MCP23017)
#define LCD_RS_BIT (1 << 2)
#define LCD_E_BIT  (1 << 0)
#define LCD_BL_BIT (1 << 3)

uint8_t portA_shadow = 0;//zmienna globalna dla stanu portu A układu MCP 

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
    TWBR = 0x19; //Prędkość transmisji
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
void MCP0_Write(uint8_t reg, uint8_t data) {
    I2C_Start();
    I2C_Write(MCP0_OPCODE); //Wysłanie adresu urządzenia (z bitem zapisu)
    I2C_Write(reg);        //Wysłanie adresu rejestru
    I2C_Write(data);       //Wysłanie danych
    I2C_Stop();
}

void MCP0_Init(void) {
    // 1. Obsługa sprzętowa pinu RESET
    DDRB |= (1 << MCP0_RESET);   // Ustaw pin jako wyjście
    PORTB &= ~(1 << MCP0_RESET); // TWARDE ZEROWANIE (Reset układu)
    _delay_ms(20);              
    PORTB |= (1 << MCP0_RESET);  // Wznowienie pracy (Stan wysoki)
    _delay_ms(50);              // Czekamy na stabilizację napięć

    // 2. Bezpośrednie (niesekwencyjne) ustawienie wszystkich pinów jako WYJŚCIA
    MCP0_Write(MCP0_IODIRA, 0x00); // Port A w całości jako wyjścia
    MCP0_Write(MCP0_IODIRB, 0x00); // Port B w całości jako wyjścia
    
    // 3. Stan niski na start
    MCP0_Write(MCP0_GPIOA, 0x00);
    MCP0_Write(MCP0_GPIOB, 0x00);
}

void SPI_INIT()
{
  DDRB |= (1 << MOSI) | (1 << SCK) | (1 << SS) | (1 << MCPS_RESET);
  DDRB &= ~(1 << MISO);
  PORTB |= (1 << SS) | (1 << MCP0_RESET);

  // F_CPU / 2 = 8 MHz 
  SPCR = (1 << SPE) | (1 << MSTR); // Brak bitów SPR1 i SPR0
  SPSR |= (1 << SPI2X); // Włączenie bitu podwójnej prędkości!
}

uint8_t SPI_Transfer(uint8_t data)
{
  SPDR = data; //wysłanie bajtu
  while (!(SPSR & (1 << SPIF))); //czekanie na zakończenie transmisji
  return SPDR;
}

// --- Obsługa MCP23S17 ---
void MCPS_Write(uint8_t reg, uint8_t data) {
    PORTB &= ~(1 << SS); // CS Low (Start)
    _delay_ms(1);
    SPI_Transfer(MCPS_OPCODE);
    SPI_Transfer(reg);
    SPI_Transfer(data);
    _delay_ms(1);
    PORTB |= (1 << SS);  // CS High (Stop)
}

void MCPS_Init(void) {
    // 1. Ustawienie IODIRA i IODIRB na 0x00 (Wszystkie piny jako WYJŚCIA)
    // Dzięki trybowi sekwencyjnemu (domyślnemu), można wysłać to jednym ciągiem!
    
    PORTB &= ~(1 << SS); // CS Low
    SPI_Transfer(MCPS_OPCODE);
    SPI_Transfer(MCPS_IODIRA); // Startujemy od adresu 0x00
    SPI_Transfer(0x00);       // Wpis do IODIRA (0x00) -> wskaźnik sam skoczy na 0x01
    SPI_Transfer(0x00);       // Wpis do IODIRB (0x01)
    PORTB |= (1 << SS);  // CS High
    _delay_ms(10);
    MCPS_Write(MCPS_GPIOB, 0x00);
    MCPS_Write(MCPS_GPIOA, 0x00);
}


void LCD_ENABLE()
{
    // E High
    portA_shadow |= LCD_E_BIT;
    MCP0_Write(MCP0_GPIOA, portA_shadow);
    MCPS_Write(MCPS_GPIOA, portA_shadow);
    _delay_us(1);
    
    // E Low
    portA_shadow &= ~LCD_E_BIT;
    MCP0_Write(MCP0_GPIOA, portA_shadow);
    MCPS_Write(MCPS_GPIOA, portA_shadow);
    _delay_us(50);
}

void LCD_Send8Bit(uint8_t val, uint8_t is_cmd) {
    // 1. Ustawienie RS (Port A)
    if (is_cmd) portA_shadow &= ~LCD_RS_BIT; // RS = 0 (Command)
    else        portA_shadow |=  LCD_RS_BIT; // RS = 1 (Data)
    
    MCP0_Write(MCP0_GPIOA, portA_shadow); // Aktualizuj linie sterujące
    MCPS_Write(MCPS_GPIOA, portA_shadow); // Aktualizuj linie sterujące
    
    // 2. Wystawienie Danych (Port B)
    MCP0_Write(MCP0_GPIOB, val);
    MCPS_Write(MCPS_GPIOB, val);
    
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
    SPI_INIT();
    MCP0_Init();
    MCPS_Init();
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
