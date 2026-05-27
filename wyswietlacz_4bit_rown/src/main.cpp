#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

#define RS PB5 
#define EN PB4 

#define D4 PD4 
#define D5 PD5
#define D6 PD6
#define D7 PD7 

uint16_t seconds = 0; 

void lcd_enable(){
  PORTB |= (1<<EN);
  _delay_us(1);
  PORTB &= ~(1<<EN);
  _delay_us(1);
}

void lcd_send(uint8_t data, uint8_t is_data){
  if(is_data) PORTB |= (1<<RS);
  else PORTB &= ~(1<<RS);

  PORTD = (PORTD&0b00001111)|(data&0b11110000);
  lcd_enable();

  PORTD = (PORTD&0b00001111)|((data&0b00001111) << 4);
  lcd_enable();

  _delay_us(50);
}

void lcd_command(uint8_t cmd){
  lcd_send(cmd, 0);
}

void lcd_data(uint8_t data){
  lcd_send(data, 1);
}

void lcd_init(){
  DDRB |= (1<<RS)|(1<<EN);
  DDRD |=(1<<D4)|(1<<D5)|(1<<D6)|(1<<D7);
  _delay_ms(40);

  PORTD = 0b00000011 << 4;
  lcd_enable();
  _delay_ms(5);

  lcd_enable();
  _delay_us(150);

  lcd_enable();
  _delay_ms(1);

  PORTD = 0b00000010 << 4;
  lcd_enable();
  _delay_ms(1);
    lcd_command(0b00101000); 
    lcd_command(0b00001100); 
    lcd_command(0b00000001); 
    _delay_ms(2);
    lcd_command(0b00000110); 
}

/*void lcd_set_cursor(uint8_t row, uint8_t col){ //dla 16x2, row 0-1, col 0-15
  uint8_t pos = 0;
  if(row==0) pos = col;
  else pos = 0b01000000 + col;
  lcd_command(0b10000000 | pos);
}*/

//zmodyfikowana funkcja dla wyswietlacza 20x4
void lcd_set_cursor(uint8_t row, uint8_t col){
    uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    lcd_command(0x80 | (row_offsets[row] + col));
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

int main(){
  lcd_init();
  timer1_init();
  
  lcd_set_cursor(0, 0);
  lcd_print("Hello World");
  lcd_set_cursor(1, 0);
  lcd_print("Wyswietlacz 20x4");
  lcd_set_cursor(2, 0);
  lcd_print("Czas dzialania:");

  char buffer[16];
  
  while (1){
    lcd_set_cursor(3,0);
    sprintf(buffer, "%u", seconds);
    lcd_print(buffer);
    _delay_ms(200);
  }
  return 0;
}
ISR(TIMER1_COMPA_vect){
  seconds++;
}
