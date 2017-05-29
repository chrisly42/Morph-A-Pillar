/*
MIT License

Copyright (c) 2017 Chris Hodges <chrisly@platon42.de>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
**/

#include <avr/sleep.h>

// Datalines ordering:
// +5V, O-WR, ENN, GND
// USB:
// +5V, DM, DP, GND

// DM (1.5 KOhm pullup!) at PB2
#define EN_PINNO   4
#define EN_PIN     PINB
#define EN_PORT    PORTB
#define EN_DDR     DDRB
#define EN_PCMSK   PCMSK
#define EN_PCINT   PCINT4

// DP (Floating) at PB3/PCINT3/CLKI/_OC1B
#define DATA_PINNO 3
#define DATA_PIN   PINB
#define DATA_PORT  PORTB
#define DATA_DDR   DDRB
#define DATA_PCMSK PCMSK
#define DATA_PCINT PCINT3

#define LED_PINNO  1
#define LED_PIN    PINB
#define LED_PORT   PORTB
#define LED_DDR    DDRB

// optional en output for chaining
#define EN_OUT_PINNO 0
#define EN_OUT_PIN   PINB
#define EN_OUT_PORT  PORTB
#define EN_OUT_DDR   DDRB

#define CMD_DISCOVER     0x01 // send ID/param
#define CMD_PING         0x02 // send ack
#define CMD_TURN_LED_ON  0x03 // send ack
#define CMD_TURN_LED_OFF 0x04 // send ack
#define CMD_ALL_OFF      0x05 // or whatever. broadcast
#define CMD_LED_BLINK    0x0C // send ack

// Known part IDs for each segment
#define PART_FORWARD      0x01
#define PART_RIGHT_90     0x02
#define PART_LEFT_180     0x03
#define PART_LEFT_90      0x04
#define PART_RIGHT_360    0x05
#define PART_ZIG_ZAG      0x06
#define PART_FACTORY_TST1 0x07 // motors, lights, sound effects
#define PART_MUSIC        0x08
#define PART_SLEEPY_MUSIC 0x09
#define PART_LEFT_45      0x0a
#define PART_FACTORY_TST2 0x0b // motors (movements: forward, left, right, zigzag, off, reset)
#define PART_HALF_FORWARD 0x0c
#define PART_SOUND_TEST   0x0d // head switch toggles to next sound/music, button repeats (power off to exit)
#define PART_HAPPY_MUSIC  0x10
#define PART_WACKY_MUSIC  0x11
#define PART_RIGHT_180    0x12
#define PART_LEAP_FORWARD 0x14
#define PART_REPEAT_ONCE  0x18 // repeat last segment once, can be used cascaded multiple times
#define PART_RIGHT_45     0x20
#define PART_SHORT_SOUND  0x21
#define PART_LEFT_360     0x22
#define PART_FROG_FORWARD 0x24
#define PART_SONAR_SOUND  0x28
#define PART_REPEAT_1A    0x40 // %00000 repeat last segment once
#define PART_REPEAT_1B    0x41 // %00001 repeat last segment once
#define PART_REPEAT_2A    0x42 // %00010 repeat last segment twice
#define PART_REPEAT_2B    0x43 // %00011 repeat last segment twice
#define PART_REPEAT_3A    0x44 // %00100 repeat last segment 3 times
#define PART_REPEAT_3B    0x46 // %00110 repeat last segment 3 times
#define PART_REPEAT_4A    0x48 // %01000 repeat last segment 4 times
#define PART_REPEAT_4B    0x4c // %01100 repeat last segment 4 times
#define PART_REPEAT_5A    0x50 // %10000 repeat last segment 5 times
#define PART_REPEAT_5B    0x58 // %11000 repeat last segment 5 times
// sonar sounds from 0x60 - 0x7f
// all above 0x7f (probably) cause errors

// number of parts this piece simulates
#define NUM_PARTS 5

uint8_t addresses[NUM_PARTS] = { 0 };

// IDs (see above) each part has
const uint8_t partIds[NUM_PARTS] = { 0x0c, 0x0a, 0x06, 0x20, 0x12 };

volatile uint8_t newPulseLength = 0;
volatile uint8_t commTimeout = true;
volatile uint8_t countPulse = false;
volatile uint8_t isResetPulse = false;
  
uint8_t nextPart = 0;

uint8_t buf[4];
uint8_t bitpos = 0;

ISR(PCINT0_vect) // pin change interrupt
{
    if(bitRead(DATA_PIN, DATA_PINNO)) // rising edge
    { 
        newPulseLength = TCNT1;
        TCCR1 = 0x07; // timer1 8MHz/64 ==> overflow after 2ms
        commTimeout = false;
        countPulse = false;
    }
    else // falling edge
    {
        TCNT1 = 0;    //restart timer 1
        TCCR1 = 0x04; //timer1 8MHz/8 == microseconds
        isResetPulse = false;  //clear
        countPulse = true;
    }
}

ISR(TIM1_OVF_vect) // overflow interrupt timer 1
{
    if(countPulse)
    {
        isResetPulse = true;  
    } else {
        commTimeout = true;
    }
    TCCR1 = 0x00; // stop timer
    TCNT1 = 0xfe;
}

void setup() {
    bitSet(LED_DDR, LED_PINNO);
    bitClear(LED_PORT, LED_PINNO);
  
    bitSet(TIMSK, TOIE1); // enable overflow interrupt for timer 1
  
    bitSet(GIMSK, PCIE); // enable pin change interrupt
  
    bitSet(DATA_PCMSK, DATA_PCINT); // PB3 pin change interrupt

#ifdef EN_OUT_PINNO
    bitSet(EN_OUT_DDR, EN_OUT_PINNO);
    bitSet(EN_OUT_PORT, EN_OUT_PINNO);
#endif

    // save some power
    ADCSRA = 0;
    bitSet(PRR, PRADC);
    bitSet(PRR, PRUSI);

    sei();
}

void goToSleep()
{
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
    sleep_cpu();
 
    // cancel sleep as a precaution
    sleep_disable();
}

// Command: 1 bit time = 96us/112u. Zero bit = 62us, One bit = 8.6us
// Request: 1 bit time = 100us/120.6us/156.6us, Clock = 8.6us, Resp after 23.8us, Zero bit = 32us, One bit = 

void sendZero()
{
    bitClear(GIMSK, PCIE); // disable pin change interrupts
    bitSet(DATA_DDR, DATA_PINNO);
    bitClear(DATA_PORT, DATA_PINNO);
    delayMicroseconds(60);
    bitClear(DATA_DDR, DATA_PINNO);
    bitSet(GIFR, PCIE); // clear any outstanding interrupts
    bitSet(GIMSK, PCIE); // enable pin change interrupts
    newPulseLength = 0;
}

uint32_t blinkTimer = 0;
uint16_t onTime = 0;
uint16_t offTime = 0;
uint8_t nextOff = false;
uint8_t blinks = 0;

void setBlink(uint16_t on, uint16_t off, uint8_t times)
{
   onTime = on;
   offTime = off;
   blinks = times;
   nextOff = false;
}

void handleBlinks()
{
    if(blinks)
    {
        if(((int32_t) (blinkTimer - millis())) <= 0)
        {
            if(nextOff)
            {
                blinkTimer = millis() + offTime;
                bitClear(LED_PORT, LED_PINNO);
                blinks--;
            } else {
                blinkTimer = millis() + onTime;
                bitSet(LED_PORT, LED_PINNO);
            }
            nextOff ^= 1;
        }
    }
}

void loop()
{
    handleBlinks();
    goToSleep();

    uint8_t resetBus = false;
    if(commTimeout)
    {
        resetBus = true;
        setBlink(50, 10, bitpos);
    }

    uint16_t pulseLength = newPulseLength;
    while(pulseLength)
    {
        newPulseLength = 0;

        if(isResetPulse) // reset
        {
#ifdef EN_OUT_PINNO
            bitSet(EN_OUT_PORT, EN_OUT_PINNO);
#endif
            setBlink(1, 10, 255);
            for(uint8_t part = 0; part < NUM_PARTS; part++)
            {
                addresses[part] = 0;
            }
            resetBus = true;
            nextPart = 0;
            break;
        } 
        else if(pulseLength < 100)
        {
            if(bitpos < 28)
            {
                uint8_t bytepos = bitpos >> 3;
                if((bitpos & 7) == 0)
                {
                    buf[bytepos] = 0;
                }
                if(pulseLength < 20)
                {
                    bitSet(buf[bytepos], 7 - (bitpos & 7));
                }
            }
            if(++bitpos == 28)
            {
                uint8_t chksum = (buf[0] + buf[1] + buf[2]) << 4;
                if(chksum != buf[3])
                {
                    setBlink((chksum >> 4) + 1, (buf[3] >> 4) + 1, 10);
                    resetBus = true;
                } else {
                    uint8_t found = !buf[0];
                    uint8_t partNo = 0;
                    for(; !found && (partNo < NUM_PARTS); partNo++)
                    {
                        if(buf[0] == addresses[partNo])
                        {
                            found = true;
                            break;
                        }
                    }
                    if(found || ((buf[1] == CMD_DISCOVER) && (!bitRead(EN_PIN, EN_PINNO)) && (nextPart < NUM_PARTS)))
                    {
                        uint8_t sendAck = false;
                        switch(buf[1])
                        {
                            case CMD_DISCOVER:
                                addresses[nextPart] = buf[0];
                                // send response
                                buf[0] = partIds[nextPart];
                                buf[1] = 0x00;
                                buf[2] = (buf[0] + buf[1]) << 4;
  
                                for(bitpos = 0; bitpos < 20; bitpos++)
                                {
                                    uint8_t sendOne = bitRead(buf[bitpos >> 3], 7 - (bitpos & 7));
                                    while(!newPulseLength);
                                    if(!sendOne)
                                    {
                                        sendZero();
                                    }
                                    newPulseLength = 0;
                                }
                                setBlink(50, 50, 1 + nextPart);

                                nextPart++;
#ifdef EN_OUT_PINNO
                                if(nextPart >= NUM_PARTS)
                                {
                                    bitClear(EN_OUT_PORT, EN_OUT_PINNO);
                                }
#endif
                                resetBus = true;
                                break;
      
                            case CMD_PING:
                                sendAck = true;
                                break;
  
                            case CMD_TURN_LED_ON:
                                bitSet(LED_PORT, LED_PINNO);
                                blinks = 0;
                                sendAck = true;
                                break;
                            
                            case CMD_TURN_LED_OFF:
                                bitClear(LED_PORT, LED_PINNO);
                                blinks = 0;
                                sendAck = true;
                                break;
                            
                            case CMD_LED_BLINK:
                                sendAck = true;
                                setBlink(500 >> partNo, 500 >> partNo, 255);
                                break;
      
                            case CMD_ALL_OFF:
                                bitClear(LED_PORT, LED_PINNO);
                                blinks = 0;
                                resetBus = true;
                                break;
                                
                            default:
                                resetBus = true;
                        }
                        
                        if(sendAck) // sendAck
                        {
                            while(!newPulseLength);
                            sendZero();
                            resetBus = true;
                        }                   
                    } else {
                        while(!commTimeout);
                        commTimeout = false;
                        newPulseLength = 0;
                        resetBus = true;
                    }
                }
            }
        } else {
            //setBlink(10, 40, 3);
            resetBus = true;
        }
        pulseLength = newPulseLength;
    }
    
    if(resetBus)
    {
        if(!countPulse)
        {
            TCCR1 = 0x00; // stop timer
        }
        bitpos = 0;
        commTimeout = false;
    }
}

