#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

//Adres I2C ekspandera PCF8574 (0x21 oznacza, że piny adresowe A0-A2 są odpowiednio ustawione)
#define PCF8574_ADDRESS 0x21

//Piny sterujące LCD podłączone bezpośrednio do mikrokontrolera (PORTB)
#define RS PB5 //Pin 13 w Arduino Uno (Register Select: 0 = komenda, 1 = dane)
#define EN PB4 //Pin 12 w Arduino Uno (Enable: aktywacja zapisu danych)

//Mapowanie wyjść ekspandera PCF8574 na piny danych wyświetlacza LCD (tryb 8-bitowy)
#define D0 0 //P0 ekspandera -> D0 wyświetlacza
#define D1 1
#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7 //P7 ekspandera -> D7 wyświetlacza

uint8_t to_send_B = 0; //Zmienna pomocnicza do przechowywania stanu danych

//Licznik sekund – dopisane 'volatile', ponieważ zmienna zmienia się w przerwaniu
volatile uint16_t seconds = 0;

//Inicjalizacja sprzętowego modułu I2C (TWI) w AVR
void I2C_Init() {
    TWSR = 0x00; //Brak preskalera (częstotliwość dzielona przez 1)
    TWBR = 0x48; //Ustawienie prędkości SCL na ok. 100 kHz przy zegarze 16 MHz
    TWCR = (1 << TWEN); //Włączenie modułu TWI
}

//Generowanie sygnału START na magistrali I2C
void I2C_Start() {
    TWCR = (1 << TWSTA) | (1 << TWEN) | (1 << TWINT);
    while (!(TWCR & (1 << TWINT))); //Oczekiwanie na ustawienie flagi TWINT (koniec operacji)
}

//Generowanie sygnału STOP na magistrali I2C
void I2C_Stop() {
    TWCR = (1 << TWSTO) | (1 << TWEN) | (1 << TWINT);
}

//Transmisja jednego bajtu danych przez I2C
void I2C_Write(uint8_t data) {
    TWDR = data; //Wpisanie danych do rejestru przesuwnego TWI
    TWCR = (1 << TWEN) | (1 << TWINT); //Rozpoczęcie wysyłania
    while (!(TWCR & (1 << TWINT))); //Oczekiwanie na zakończenie nadawania
}

//Wysłanie pełnego bajtu danych bezpośrednio do portów wyjściowych ekspandera PCF8574
void PCF8574_Write(uint8_t data) {
    I2C_Start();
    I2C_Write(PCF8574_ADDRESS << 1); //Adresowanie układu w trybie zapisu (bit R/W = 0)
    I2C_Write(data);                 //Wystawienie danych na piny P0-P7 ekspandera
    I2C_Stop();
}

//Odczyt danych z ekspandera PCF8574
uint8_t PCF8574_Read() { 
    uint8_t data;
    I2C_Start();
    I2C_Write((PCF8574_ADDRESS << 1) | 1); //Adresowanie układu w trybie odczytu (bit R/W = 1)
    TWCR = (1 << TWEN) | (1 << TWINT);     //Odbiór bajtu z odpowiedzią NACK
    while (!(TWCR & (1 << TWINT)));
    data = TWDR;                           //Pobranie odczytanej wartości
    I2C_Stop();
    return data;
}

//Wygenerowanie impulsu na pinie EN (mikrokontrolera), który zatwierdza dane w LCD
void lcd_enable(){
    PORTB |= (1<<EN);   //Stan wysoki na EN
    _delay_us(1);       //Krótkie podtrzymanie stanu high
    PORTB &= ~(1<<EN);  //Opadające zbocze na EN – LCD w tym momencie zatrzaskuje bity danych
    _delay_us(1);       //Czas na ustabilizowanie
}

//Główna funkcja wysyłająca dane/komendy w trybie 8-bitowym
void lcd_send(uint8_t data, uint8_t is_data){ 
    //Ustawienie pinu RS na mikrokontrolerze w zależności od rodzaju przesyłu
    if (is_data) PORTB |= (1<<RS); //Dane (np. znak do wyświetlenia)
    else PORTB &= ~(1<<RS);        //Instrukcja/Komenda
  
    PCF8574_Write(data); //Wysłanie całego bajtu danych na piny D0-D7 przez ekspander I2C
    lcd_enable();        //Wygenerowanie impulsu zegarowego dla LCD

    _delay_ms(2); //Bezpieczny czas na wykonanie operacji przez kontroler HD44780
}

//Wysłanie instrukcji sterującej do LCD
void lcd_command(uint8_t cmd){
    lcd_send(cmd, 0); //is_data = 0
}

//Wysłanie pojedynczego znaku na ekran
void lcd_data(uint8_t data){
    lcd_send(data, 1); //is_data = 1
}

//Inicjalizacja wyświetlacza LCD w trybie 8-bitowym
void lcd_init(){
    DDRB |= (1<<RS) | (1<<EN); //Ustawienie pinów RS i EN mikrokontrolera jako wyjścia
    _delay_ms(40);             //Oczekiwanie na ustabilizowanie się napięcia zasilania LCD

    //Sekwencja restartu kontrolera LCD (zgodna ze specyfikacją HD44780)
    lcd_command(0b00110000);
    _delay_ms(3);
    lcd_command(0b00110000);
    lcd_command(0b00110000);

    // Właściwa konfiguracja:
    lcd_command(0b00111000); //Tryb 8-bitowy, 2 linie wyświetlacza, czcionka 5x8 punktów
    lcd_command(0b00001100); //Włączenie ekranu, wyłączenie kursora oraz jego migania
    lcd_command(0b00000001); //Czyszczenie zawartości ekranu (Clear Display)
    _delay_ms(2);            //Czyszczenie wymaga dłuższego czasu wykonania
    lcd_command(0b00000110); //Tryb pracy kursora – przesuwanie w prawo po zapisie znaku
}

//Ustawienie pozycji kursora dla standardowego wyświetlacza 16x2
void lcd_set_cursor(uint8_t row, uint8_t col){ 
    uint8_t pos = 0;
    if(row == 0) pos = col;       // Pierwszy wiersz (adres DDRAM zaczyna się od 0x00)
    else pos = 0b01000000 + col;  // Drugi wiersz (adres DDRAM zaczyna się od 0x40)
    lcd_command(0b10000000 | pos); // Wysłanie komendy "Set DDRAM Address"
}

// Zmodyfikowana funkcja ustawiania kursora dla dużego wyświetlacza 20x4 (zakomentowana)
/*void lcd_set_cursor(uint8_t row, uint8_t col){
    // Mapowanie początkowych adresów pamięci DDRAM dla każdego z 4 wierszy
    uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    if (row >= 4) {
        row = 0; // Zabezpieczenie przed wyjściem poza zakres wierszy
    }
    lcd_command(0x80 | (row_offsets[row] + col));
}*/

//Wyświetlenie całego ciągu tekstowego na ekranie
void lcd_print(const char *str) {
    while (*str) {
        lcd_data(*str++); //Wysyłanie kolejnych znaków w pętli do momentu znaku końca '\0'
    }
}

//Konfiguracja Timera 1 do generowania przerwań co 1 sekundę
void timer1_init(){
    //Tryb CTC (Clear Timer on Compare) oraz ustawienie preskalera na 1024
    TCCR1B |= (1<<WGM12) | (1<<CS12) | (1<<CS10);
    
    //Wartość TOP dla częstotliwości 1 Hz przy zegarze 16 MHz: (16000000 / (1024 * 1)) - 1
    OCR1A = 15624; 
    
    TIMSK1 |= (1<<OCIE1A); //Włączenie przerwania Compare Match A dla Timera 1
    sei();                 //Globalne odblokowanie przerwań
}

int main() {
    I2C_Init();    //Start sprzętowego I2C
    lcd_init();    //Konfiguracja ekranu LCD
    timer1_init(); //Uruchomienie licznika sekund
    
    /* Sekcja testowa dla ekranu 20x4 (zakomentowana)
    lcd_set_cursor(0, 0);
    lcd_print("Hello World");
    lcd_set_cursor(1, 0);
    lcd_print("Wyswietlacz 20x4");
    lcd_set_cursor(2, 0);
    lcd_print("Czas dzialania:"); */

    lcd_set_cursor(0, 0);
    lcd_print("Hello World"); //Wyświetlenie tekstu w pierwszym wierszu

    char buffer[16]; //Bufor tekstowy do przechowywania sformatowanej liczby sekund
    
    while (1){
        lcd_set_cursor(1, 0);           //Ustawienie kursora na początku drugiego wiersza
        sprintf(buffer, "%u", seconds); //Konwersja zmiennej liczbowej 'seconds' na tekst
        lcd_print(buffer);              //Wypisanie aktualnego stanu licznika na LCD
        _delay_ms(200);                 //Krótkie opóźnienie pętli głównej dla stabilności odświeżania
    }
    
    return 0;
}

//Wektor obsługi przerwania od Timera 1 – wywoływany dokładnie co sekundę
ISR(TIMER1_COMPA_vect){
    seconds++; //Inkrementacja licznika sekund
}
