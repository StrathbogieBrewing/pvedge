/******************************************************************************/
/* Files to Include                                                           */
/******************************************************************************/

#include <stdint.h>        /* For uint8_t definition */
#include <stdbool.h>       /* For true/false definition */

#include <stdint.h>       /* For true/false definition */

// CONFIG
#pragma config FOSC = HS        // Oscillator Selection bits (HS oscillator)
#pragma config WDTE = ON        // Watchdog Timer Enable bit (WDT enabled)
#pragma config PWRTE = ON       // Power-up Timer Enable bit (PWRT enabled)
#pragma config CP = OFF         // FLASH Program Memory Code Protection bit (Code protection off)
#pragma config BOREN = ON       // Brown-out Reset Enable bit (BOR enabled)

#define _XTAL_FREQ 20000000

#include <xc.h>         /* XC8 General Include File */

#define TICK_PERIOD_US (100)
#define LINE_FREQUENCY_HZ (50)
#define TICKS_FULL_CYCLE (1000000 / LINE_FREQUENCY_HZ / TICK_PERIOD_US)
#define TICKS_HALF_CYCLE (TICKS_FULL_CYCLE / 2)
#define TICKS_ON_MAX ((TICKS_HALF_CYCLE * 67) / 100)

#define SOLAR_ADC_BIT_MV (346)

#define SOLAR_START (56000 / SOLAR_ADC_BIT_MV)
//#define SOLAR_START (28000 / SOLAR_ADC_BIT_MV)
#define SOLAR_STOP (24000 / SOLAR_ADC_BIT_MV)

#define THERMAL_FAN_TEMP (170)  // approx 40 celcius
#define THERMAL_STOP_TEMP (175)  // approx 50 celcius

#define ENERGY_LIMIT (100000000UL) // approx 10 kwh per day

#define SOLAR_VOLTAGE_ADC_CHANNEL 0
#define HEATSINK_TEMP_ADC_CHANNEL 2

#define LEFT_LOW RC3
#define LEFT_HIGH RC2
#define RIGHT_LOW RC0
#define RIGHT_HIGH RC1

void tick_delay(unsigned char ticks){
    while(ticks--){
        __delay_us(TICK_PERIOD_US);
    }
}

void main(void)
{
    TRISB = 0xf0;
    PORTB |= 0x0f;
    
    TRISC = 0b00010000;
    RC0 = 0;    // enable right arm low side fet
    RC1 = 0;    // disable right arm high side fet
    RC2 = 0;    // disable left arm high side fet
    RC3 = 0;    // enable left arm low side fet
    RC5 = 0;    // fan off
    RC6 = 0;    // ac aux relay off
    RC7 = 0;    // ac main relay off

    ADCON0 = 0x81;               // Turn ON ADC and Clock Selection
    ADCON1 = 0x00;               // All pins as Analog Input and setting Reference Voltages
    ADCON0 &= 0xC5;              // Select channel 0 - solar voltage
    ADCON0 |= (SOLAR_VOLTAGE_ADC_CHANNEL << 3);
    
    unsigned char on_ticks = 0;  // start with energy at zero
    unsigned char cycle_counter = 0;  // start with energy at zero
    unsigned char solar_voltage = 0;
    unsigned char heatsink_temp = 0;
    uint32_t energy = 0;
    
    while(1)
    {
        CLRWDT();
        
        // Read adc solar voltage
        GO_nDONE = 1;                // Initializes A/D conversion
        while(GO_nDONE);             // Waiting for conversion to complete
        solar_voltage = ADRES;
        ADCON0 &= 0xC5;              // Select channel 2 - heat sink temperature
        ADCON0 |= (HEATSINK_TEMP_ADC_CHANNEL << 3);         

        // update duty cycle every 16 ac line cycles
        cycle_counter++;
        if((cycle_counter & 0x0f) == 0){
            if(solar_voltage < SOLAR_START){
                if(on_ticks){
                    on_ticks--;
                }                
            } else {
                if(on_ticks < TICKS_ON_MAX){
                    on_ticks++;
                }
            }
        }
        
        // shutdown tests
        if((solar_voltage < SOLAR_STOP) 
                || (heatsink_temp > THERMAL_STOP_TEMP) 
                || (energy > ENERGY_LIMIT)){
            on_ticks = 0;
        }
        
        // update fan state
        if((solar_voltage > SOLAR_STOP) 
                && (on_ticks > (TICKS_ON_MAX / 10)) 
                && (heatsink_temp > THERMAL_FAN_TEMP)){
            RC5 = 1;    // fan on
        } else {
            RC5 = 0;    // fan off
        }
        
        // duty cycle range sanity check
        if(on_ticks > TICKS_ON_MAX) on_ticks = TICKS_ON_MAX;
        unsigned char off_ticks = TICKS_HALF_CYCLE - on_ticks;
 
        // positive half cycle
        if(on_ticks){ 
            LEFT_LOW = 0;    // disable left arm low side fet
            NOP();             // shoot through guard
            NOP();
            LEFT_HIGH = 1;    // enable left arm high side fet
            tick_delay(on_ticks);
            RIGHT_LOW = 0;    // disable right arm low side fet
            NOP();
            NOP();
            RIGHT_HIGH = 1;    // enable right arm high side fet  
        } 
        tick_delay(off_ticks);
        
        // Read adc heatsink temperature
        GO_nDONE = 1;                // Initializes A/D conversion
        while(GO_nDONE);             // Waiting for conversion to complete
        heatsink_temp = ADRES;
        ADCON0 &= 0xC5;              // // Select solar voltage channel 0 
        ADCON0 |= (SOLAR_VOLTAGE_ADC_CHANNEL << 3); 
        
        // update leds
        PORTB |= 0x0f;
        if(on_ticks == TICKS_ON_MAX){
            PORTB &= 0xf0;
        } else if (on_ticks > (TICKS_ON_MAX * 3) / 4){
            PORTB &= 0xf1;
        } else if (on_ticks > (TICKS_ON_MAX * 2) / 4){
            PORTB &= 0xf3;
        } else if (on_ticks > (TICKS_ON_MAX * 1) / 4){
            PORTB &= 0xf7;
        }
        
        // accumulate energy 
        energy += on_ticks;
            
        // negative half cycle
        if(on_ticks){ 
            LEFT_HIGH = 0;    // disable left arm high side fet
            NOP();             // shoot through guard
            NOP();
            LEFT_LOW = 1;    // enable left arm low side fet
            tick_delay(on_ticks);
            RIGHT_HIGH = 0;    // disable right arm high side fet
            NOP();             // shoot through guard
            NOP();
            RIGHT_LOW = 1;    // enable right arm low side fet  
        }
        tick_delay(off_ticks);
    }
}

