#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>

//Adres I2C ekspandera PCF8574 (w zależności od wersji układu i podłączenia pinów A0-A2, 0x20 to domyślny adres)
#define PCF8574_ADDRESS 0x20

//Mapowanie pinów ekspandera PCF8574 na piny wyświetlacza LCD
#define RS 0 //Register Select: 0 = komenda, 1 = dane
#define EN 1 //Enable: pin zezwalający na zapis/odczyt
#define RW 2 //Read/Write: 0 = zapis, 1 = odczyt

//Piny danych dla trybu 4-bitowego
#define D4 4 
#define D5 5
#define D6 6
#define D7 7 

//Zmienna przechowująca aktualny stan pinów ekspandera (tzw. shadow register)
uint8_t to_send = 0;

//Licznik sekund aktualizowany w przerwaniu
volatile uint16_t seconds = 0; //Dodano 'volatile', ponieważ zmienna jest modyfikowana w przerwaniu

//Inicjalizacja magistrali I2C (w AVR nazywanej TWI)
void I2C_Init() {
    TWSR = 0x00; //Brak preskalera dla I2C
    TWBR = 0x48; //Ustawienie prędkości transmisji (SCL). Dla zegara 16MHz daje to ok. 100 kHz
    TWCR = (1 << TWEN); //Włączenie modułu TWI
}

//Generowanie warunku START na magistrali I2C
void I2C_Start() {
    TWCR = (1 << TWSTA) | (1 << TWEN) | (1 << TWINT);
    while (!(TWCR & (1 << TWINT))); // Czekaj na zakończenie operacji (ustawienie flagi TWINT)
}

//Generowanie warunku STOP na magistrali I2C
void I2C_Stop() {
    TWCR = (1 << TWSTO) | (1 << TWEN) | (1 << TWINT);
}

//Wysłanie pojedynczego bajtu przez I2C
void I2C_Write(uint8_t data) {
    TWDR = data; //Załadowanie danych do rejestru danych TWI
    TWCR = (1 << TWEN) | (1 << TWINT); //Rozpoczęcie transmisji
    while (!(TWCR & (1 << TWINT))); //Czekaj na zakończenie wysyłania
}

//Wysłanie bajtu danych do ekspandera PCF8574
void PCF8574_Write(uint8_t data) {
    I2C_Start();
    I2C_Write(PCF8574_ADDRESS << 1); //Wysłanie adresu z bitem R/W ustawionym na 0 (zapis)
    I2C_Write(data);                 //Wysłanie właściwych danych
    I2C_Stop();
}

//Odczyt bajtu danych z ekspandera PCF8574
uint8_t PCF8574_Read() { 
    uint8_t data;
    I2C_Start();
    I2C_Write((PCF8574_ADDRESS << 1) | 1); //Wysłanie adresu z bitem R/W ustawionym na 1 (odczyt)
    TWCR = (1 << TWEN) | (1 << TWINT);     //Odbiór danych (bez potwierdzenia ACK - NACK)
    while (!(TWCR & (1 << TWINT)));        //Czekaj na zakończenie odbioru
    data = TWDR;                           //Zapisz odebrane dane
    I2C_Stop();
    return data;
}

//Pulsowanie pinu EN (Enable), które zatwierdza dane na pinach D4-D7 wyświetlacza
void lcd_enable(){
    to_send |= (1<<EN);      //Ustawienie EN na stan wysoki
    PCF8574_Write(to_send);
    _delay_us(1);            //Krótkie opóźnienie dla stabilizacji
    to_send &= ~(1<<EN);     //Ustawienie EN na stan niski (opadające zbocze zatwierdza dane)
    PCF8574_Write(to_send);
    _delay_us(50);           //Czas na przetworzenie przez LCD
}

//Sprawdzenie flagi zajętości (Busy Flag) wyświetlacza LCD
bool checkBS(){
    to_send &= ~(1 << RS); //RS = 0 (tryb instrukcji)
    to_send |= (1 << RW);  //RW = 1 (tryb odczytu)
    PCF8574_Write(to_send);

    to_send |= (1<<EN);    //EN = 1 (rozpoczęcie odczytu)
    PCF8574_Write(to_send);
    _delay_us(1);

    //Odczyt stanu pinów, flaga zajętości znajduje się na pinie D7
    bool busy = (PCF8574_Read() & (1 << D7)); 
    
    to_send &= ~(1<<EN);   //EN = 0 (zakończenie odczytu pierwszej połowy bajtu)
    PCF8574_Write(to_send);

    //Wyświetlacz w trybie 4-bitowym wymaga drugiego taktu EN dla młodszych 4 bitów, 
    //nawet jeśli nas interesuje tylko starsza część (z flagą D7)
    to_send |= (1<<EN);
    PCF8574_Write(to_send);
    _delay_us(1);
    to_send &= ~(1<<EN);
    PCF8574_Write(to_send);

    to_send &= ~(1 << RW); //Powrót do trybu zapisu (RW = 0)
    PCF8574_Write(to_send);

    return busy; //Zwraca 1 jeśli LCD jest zajęty, 0 jeśli gotowy
}

//Pętla oczekująca na zwolnienie flagi zajętości LCD
void wait_until_BS(){
    while(checkBS()==1){
        _delay_us(1);
    }
}

//Funkcja wysyłająca bajt do LCD w trybie 4-bitowym (najpierw starsze, potem młodsze 4 bity)
void lcd_send(uint8_t data, uint8_t is_data){
    // Ustawienie pinu RS w zależności od tego, czy wysyłamy komendę czy znak
    if(is_data) to_send |= (1<<RS);
    else to_send &= ~(1<<RS);
    
    //Wysłanie starszej połówki bajtu (bity 4-7)
    //Czyścimy starsze 4 bity w to_send i wstawiamy starsze 4 bity z 'data'
    to_send = (to_send & 0b00001111) | (data & 0b11110000);
    PCF8574_Write(to_send);
    lcd_enable(); //Zatwierdzenie starszej połówki

    //Wysłanie młodszej połówki bajtu (bity 0-3) przesuniętej na pozycje 4-7
    to_send = (to_send & 0b00001111) | ((data << 4) & 0b11110000);
    PCF8574_Write(to_send);
    lcd_enable(); //Zatwierdzenie młodszej połówki

    wait_until_BS(); //Oczekiwanie na przetworzenie przez LCD
}

//Wysłanie komendy do LCD
void lcd_command(uint8_t cmd){
    lcd_send(cmd, 0); // is_data = 0
}

//Wysłanie znaku (danych) do LCD
void lcd_data(uint8_t data){
    lcd_send(data, 1); // is_data = 1
}

//Procedura inicjalizacji kontrolera HD44780 w trybie 4-bitowym
void lcd_init(){
    _delay_ms(40); //Czekaj na ustabilizowanie napięcia zasilania LCD (>15ms)

    //Sekwencja inicjalizacyjna "wymuszająca" reset i tryb 8-bitowy (roboczo)
    to_send = 0b00000011 << 4;
    lcd_enable();
    _delay_ms(5); 
    
    lcd_enable();
    _delay_us(150); 

    lcd_enable();
    wait_until_BS();
    
    //Przełączenie w tryb 4-bitowy
    to_send = 0b00000010 << 4;
    lcd_enable();
    wait_until_BS();
    
    //Konfiguracja LCD (już w trybie 4-bitowym przy użyciu pełnej funkcji lcd_command)
    lcd_command(0b00101000); //Interfejs 4-bitowy, 2 linie, czcionka 5x8
    lcd_command(0b00001100); //Włącz wyświetlacz, wyłącz kursor i jego miganie
    lcd_command(0b00000001); //Wyczyść ekran (Clear Display)

    lcd_command(0b00000110); //Inkrementowanie adresu po każdym znaku (Entry Mode Set)
}

//Ustawienie kursora na wybranej pozycji (wiersz, kolumna)
void lcd_set_cursor(uint8_t row, uint8_t col){
    uint8_t pos = 0;
    if(row == 0) pos = col;                 //Wiersz 0 zaczyna się od adresu 0x00
    else pos = 0b01000000 + col;            //Wiersz 1 zaczyna się od adresu 0x40 (64)
    lcd_command(0b10000000 | pos);          //Komenda Set DDRAM Address (najstarszy bit = 1)
}

//Wyświetlenie ciągu znaków (string) na LCD
void lcd_print(const char *str) {
    while (*str) {
        lcd_data(*str++); //Wysyłaj znak po znaku, aż natrafisz na znak końca '\0'
    }
}

//Konfiguracja Timera 1 do odliczania sekund
void timer1_init(){
    //Tryb CTC (Clear Timer on Compare Match) i preskaler = 1024
    TCCR1B |= (1<<WGM12) | (1<<CS12) | (1<<CS10); 
    
    //Ustawienie wartości porównania (TOP). 
    //Wzór: F_CPU / (Preskaler * Częstotliwość) - 1
    //Dla 16 MHz: 16000000 / (1024 * 1Hz) - 1 = 15624
    OCR1A = 15624; 
    
    //Włączenie przerwania od zrównania z OCR1A
    TIMSK1 |= (1<<OCIE1A);
    
    //Globalne odblokowanie przerwań
    sei();
}

int main() {
    I2C_Init();       //Inicjalizacja I2C
    lcd_init();       //Inicjalizacja LCD
    timer1_init();    //Inicjalizacja Timera 1

    lcd_set_cursor(0, 0);    //Ustawienie na początku pierwszej linii
    lcd_print("Hello World");

    char buffer[16]; //Bufor tekstowy dla funkcji sprintf
    
    while (1) {
        lcd_set_cursor(1, 0);            //Ustawienie na początku drugiej linii
        sprintf(buffer, "%u", seconds);  //Konwersja liczby całkowitej na tekst
        lcd_print(buffer);               //Wyświetlenie aktualnej liczby sekund
    }

    return 0;
}

//Wektor przerwania Timera 1 (wywoływany co 1 sekundę)
ISR(TIMER1_COMPA_vect){
    seconds++; //Zwiększenie licznika
}