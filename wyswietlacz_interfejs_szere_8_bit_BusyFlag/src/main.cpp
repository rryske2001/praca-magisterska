#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include <stdio.h>

#define PCF8574_ADDRESS_A 0x20
#define PCF8574_ADDRESS_B 0x21

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

uint8_t to_send_A = 0; //rejestr sygnałów sterujących
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

void PCF8574_Write_A(uint8_t data) {
    I2C_Start();
    I2C_Write(PCF8574_ADDRESS_A << 1); 
    I2C_Write(data);              
    I2C_Stop();
    
}

void PCF8574_Write_B(uint8_t data) {
    I2C_Start();
    I2C_Write(PCF8574_ADDRESS_B << 1); 
    I2C_Write(data);              
    I2C_Stop();
    
}

uint8_t PCF8574_Read_A() { 
    uint8_t data;
    I2C_Start();
    I2C_Write((PCF8574_ADDRESS_A << 1) | 1);
    TWCR = (1 << TWEN) | (1 << TWINT);    
    while (!(TWCR & (1 << TWINT)));
    data = TWDR;                      
    I2C_Stop();
    return data;
}

uint8_t PCF8574_Read_B() { 
    uint8_t data;
    I2C_Start();
    I2C_Write((PCF8574_ADDRESS_B << 1) | 1);
    TWCR = (1 << TWEN) | (1 << TWINT);    
    while (!(TWCR & (1 << TWINT)));
    data = TWDR;                      
    I2C_Stop();
    return data;
}

bool checkBS(){
    to_send_A &= ~(1 << RS);
    to_send_A |= (1 << RW); 
    PCF8574_Write_A(to_send_A);

    to_send_A |= (1<<EN);
    PCF8574_Write_A(to_send_A);
    _delay_us(1);

    bool busy = (PCF8574_Read_B() & (1 << D7));
    to_send_A &= ~(1<<EN);
    PCF8574_Write_A(to_send_A);

     to_send_A |= (1<<EN);
     PCF8574_Write_A(to_send_A);
     _delay_us(1);
     to_send_A &= ~(1<<EN);
     PCF8574_Write_A(to_send_A);

    to_send_A &= ~(1 << RW);
    PCF8574_Write_A(to_send_A);

    return busy;
}

void wait_until_BS(){
  while(checkBS()==1){
    _delay_us(1);
  }
}

void lcd_enable(){
  to_send_A |= (1<<EN);
  PCF8574_Write_A(to_send_A);
  _delay_us(1); 
  to_send_A &= ~(1<<EN);
  PCF8574_Write_A(to_send_A);
  _delay_us(50);
}

void lcd_send(uint8_t data, uint8_t is_data){
  if(is_data) to_send_A |= (1<<RS);
  else to_send_A &= ~(1<<RS);
  PCF8574_Write_A(to_send_A);
  
  PCF8574_Write_B(data);
  lcd_enable();

  wait_until_BS();
}

void lcd_command(uint8_t cmd){
  lcd_send(cmd, 0);
}

void lcd_data(uint8_t data){
  lcd_send(data, 1);
}

void lcd_init(){
  _delay_ms(40);

  to_send_B = 0b00000011 << 4;
  lcd_enable();
  _delay_ms(5);
  
  lcd_enable();
  _delay_us(150);

  lcd_enable();
  wait_until_BS();
  

  lcd_command(0b00111000); 
  lcd_command(0b00001100); 
  lcd_command(0b00000001); 

  lcd_command(0b00000110); 
}

void lcd_set_cursor(uint8_t row, uint8_t col){
  uint8_t pos = 0;
  if(row==0) pos = col;
  else pos = 0b01000000 + col;
  lcd_command(0b10000000 | pos);
}

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
    
    lcd_set_cursor(0, 0);
    lcd_print("Hello World");

    char buffer[16];
    while (1) {
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
