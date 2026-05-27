#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include <stdio.h>

#define PCF8574_ADDRESS 0x21

#define RS PB5 //pin13
#define EN PB4 //pin12

#define D0 0 //P0
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7 //P7

uint8_t to_send_B = 0; //rejestr danych

volatile uint16_t seconds = 0;

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
    I2C_Write(PCF8574_ADDRESS << 1); 
    I2C_Write(data);              
    I2C_Stop();
    
}

uint8_t PCF8574_Read() { 
    uint8_t data;
    I2C_Start();
    I2C_Write((PCF8574_ADDRESS << 1) | 1);
    TWCR = (1 << TWEN) | (1 << TWINT);    
    while (!(TWCR & (1 << TWINT)));
    data = TWDR;                      
    I2C_Stop();
    return data;
}

void lcd_enable(){
  PORTB |= (1<<EN);
  _delay_us(1);
  PORTB &= ~(1<<EN);
  _delay_us(1);
}

void lcd_send(uint8_t data, uint8_t is_data){ 
  if (is_data) PORTB |= (1<<RS);
  else PORTB &= ~(1<<RS);
  
  PCF8574_Write(data);
  lcd_enable();

  _delay_ms(2);
}

void lcd_command(uint8_t cmd){
  lcd_send(cmd, 0);
}

void lcd_data(uint8_t data){
  lcd_send(data, 1);
}

void lcd_init(){
  DDRB |=(1<<RS)|(1<<EN);
  _delay_ms(40);

  lcd_command(0b00110000);
  _delay_ms(3);
  lcd_command(0b00110000);
  lcd_command(0b00110000);

  lcd_command(0b00111000);
  lcd_command(0b00001100);
  lcd_command(0b00000001);
  _delay_ms(2);
  lcd_command(0b00000110); 
}

void lcd_set_cursor(uint8_t row, uint8_t col){ //16x2
  uint8_t pos = 0;
  if(row==0) pos = col;
  else pos = 0b01000000 + col;
  lcd_command(0b10000000 | pos);
}

//zmodyfikowana funkcja dla wyswietlacza 20x4
/*void lcd_set_cursor(uint8_t row, uint8_t col){
    uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    if (row >= 4) {
        row = 0; 
    }
    lcd_command(0x80 | (row_offsets[row] + col));
}*/

void lcd_print(const char *str) {
    while (*str) {
        lcd_data(*str++);
    }
}

void timer1_init(){
  TCCR1B |= (1<<WGM12)|(1<<CS12)|(1<<CS10);
  OCR1A = 15624; 
  TIMSK1 |= (1<<OCIE1A);
  sei();
}

int main() {
    I2C_Init();
    lcd_init();
    timer1_init();
    
    /*lcd_set_cursor(0, 0);
    lcd_print("Hello World");
    lcd_set_cursor(1, 0);
    lcd_print("Wyswietlacz 20x4");
    lcd_set_cursor(2, 0);
    lcd_print("Czas dzialania:");*/

    lcd_set_cursor(0, 0);
    lcd_print("Hello World");

    char buffer[16];
    while (1){
      lcd_set_cursor(1,0);
      sprintf(buffer, "%u", seconds);
      lcd_print(buffer);
      _delay_ms(200);
    }
    
    return 0;
}
ISR(TIMER1_COMPA_vect){
  seconds++;
}
