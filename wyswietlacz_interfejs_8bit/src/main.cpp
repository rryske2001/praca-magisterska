#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

//definicje pinow
#define RS PB5 //pin13
#define EN PB4 //pin12

#define D0 PD0 //pin0
#define D1 PD1
#define D2 PD2
#define D3 PD3
#define D4 PD4
#define D5 PD5
#define D6 PD6
#define D7 PD7 //pin7

uint16_t seconds = 0; //Zmienna globalna, licznik sekund

//Funkcja do wysylania impulsu na pin EN
void lcd_enable(){
  PORTB |= (1<<EN);
  _delay_us(1);
  PORTB &= ~(1<<EN);
  _delay_us(1);
}

//Funkcja do wysylania danych lub komend
void lcd_send(uint8_t data, uint8_t is_data){
  if (is_data) PORTB |= (1<<RS);
  else PORTB &= ~(1<<RS);

  //wysylanie 8 bitow
  PORTD = data;
  lcd_enable();
  _delay_us(50);
}

//Funkcja do wysylania komend
void lcd_command(uint8_t cmd){ //wysyłanie komendy
  lcd_send(cmd, 0);
}

//Funkcja do wysylania znaku
void lcd_data(uint8_t data){ //wysylanie znaku
  lcd_send(data, 1);
}

//Funkcja inicjalizujaca wyswietlacz
void lcd_init(){
  //ustawienie pinow jako wyjscie
  DDRB |=(1<<RS)|(1<<EN);
  DDRD |=(1<<D0)|(1<<D1)|(1<<D2)|(1<<D3)|(1<<D4)|(1<<D5)|(1<<D6)|(1<<D7);
  _delay_ms(40);
  //tryb 8-bitowy
  PORTD = 0b00000011 << 4;
  lcd_enable();
  _delay_ms(5);
  
  lcd_enable();
  _delay_us(100);

  lcd_enable();
  _delay_ms(1);

 //ustawienia wyswietlacza
  lcd_command(0b00111000); //8-bitowy tryb, 2 linie, 5x8 pikseli
  lcd_command(0b00001100); // Wlacz wyswietlacz, wylacz kursor
  lcd_command(0b00000001); // Wyczysc wyswietlacz
  _delay_ms(2);
  lcd_command(0b00000110); // Przesuwanie kursora w prawo
}

//Funkcja ustawiajaca kursor na zadanej pozcji
void lcd_set_cursor(uint8_t row, uint8_t col){
  uint8_t pos = 0;
  if(row==0) pos = col;
  else pos = 0b01000000 + col;
  lcd_command(0b10000000 | pos);
}

//Funkcja wysylajaca tekst na wyswietlacz
void lcd_print(const char *str) {
    while (*str) {
        lcd_data(*str++);
    }
}

//Funkcja inicjalizujaca Timer1
void timer1_init(){
  TCCR1B |= (1<<WGM12)|(1<<CS12)|(1<<CS10); //tryb CTC, preskaler 1024
  OCR1A = 15624; //Dla 16 MHz, 1 sekunda
  TIMSK1 |= (1<<OCIE1A);
  sei();
}

int main(void){
  lcd_init();
  timer1_init();
  
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

//przerwanie Timer1 do zwiększania liczby sekund
ISR(TIMER1_COMPA_vect){
  seconds++;
}
