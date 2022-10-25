#include <SoftwareSerial.h>
// requires an Arduino Mega2560, possibly rev3, possibly only the authentic one.

const unsigned long SERIALSPEED = 115200;
const unsigned long UARTSPEED = 9600;

volatile unsigned Pulses; // counter for input events, reset each second

// tachometer needs an interrupt-capable digital pin. on Mega,
// this is 2, 3, 18, 19, 20, 21 (last two conflict with i2c).
const int RPMPIN = 2; // pin connected to tachometer

// we'll use two pins for UART communication with the ESP32, based
// atop a serial pair. Serial2 is RX on 17, TX on 16.
#define UART Serial2

// for control of 12V RGB LEDs. we go through 10Kohm resistors on our way
// to 3 N-MOSFETs (IRLB8721s), and from there emerge 3x 12V signals.
const int RRGBPIN = 11;
const int GRGBPIN = 10;
const int BRGBPIN = 9;

// fixed 12V LEDs in the reservoir atop the MO-RA3
const int RRESPIN = 7;
const int GRESPIN = 6;
const int BRESPIN = 5;

// we need a digital output pin for PWM.
const int PWMPIN = 8;

const int TEMPPIN = A0;

// Intel spec for PWM fans demands a 25K frequency.
const word PWM_FREQ_HZ = 25000;

byte Red = 32;
byte Green = 64;
byte Blue = 128;

unsigned Pwm;
// on mega:
//  pin 13, 4 == timer 0 (used for micros())
//  pin 12, 11 == timer 1
//  pin 10, 9 == timer 2
//  pin 5, 3, 2 == timer 3
//  pin 8, 7, 6 == timer 4

static void rpm(void){
  if(Pulses < 65535){
    ++Pulses;
  }
}

static void setup_timers(void){
  TCCR4A = 0;
  TCCR4B = 0;
  TCNT4  = 0;
  // Mode 10: phase correct PWM with ICR4 as Top
  // OC4C as Non-Inverted PWM output
  ICR4 = (F_CPU / PWM_FREQ_HZ) / 2;
  TCCR4A = _BV(COM4C1) | _BV(WGM41);
  TCCR4B = _BV(WGM43) | _BV(CS40);
}

static void apply_rgb(void){
  Serial.println("committing RGB values");
  analogWrite(RRGBPIN, Red);
  analogWrite(GRGBPIN, Green);
  analogWrite(BRGBPIN, Blue);
}

void setup(){
  const byte INITIAL_PWM = 128;
  Serial.begin(SERIALSPEED);
  while(!Serial); // only necessary/meaningful for boards with native USB
  UART.begin(UARTSPEED);

  setup_timers();
  pinMode(PWMPIN, OUTPUT);
  Serial.print("pwm write on ");
  Serial.println(PWMPIN);
  setPWM(INITIAL_PWM);

  Pulses = 0;
  pinMode(RPMPIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RPMPIN), rpm, RISING);
  Serial.print("tachometer read on ");
  Serial.println(RPMPIN);

  pinMode(TEMPPIN, INPUT);
  // we'll get better thermistor readings if we use the cleaner
  // 3.3V line. connect 3.3V to AREF.
  analogReference(EXTERNAL);

  pinMode(RRGBPIN, OUTPUT);
  pinMode(GRGBPIN, OUTPUT);
  pinMode(BRGBPIN, OUTPUT);
  apply_rgb();

  // the reservoir color is fixed; just set it once here
  pinMode(RRESPIN, OUTPUT);
  pinMode(GRESPIN, OUTPUT);
  pinMode(BRESPIN, OUTPUT);
  analogWrite(RRESPIN, 0);
  analogWrite(GRESPIN, 0);
  analogWrite(BRESPIN, 255);
}

void setPWM(byte pwm){
  Pwm = pwm;
  OCR4C = pwm;
  Serial.print("PWM to ");
  Serial.print(pwm);
  Serial.print(" OCR4C to ");
  Serial.println(OCR4C);
}

// apply a PWM value between 0 and 255, inclusive.
static int apply_pwm(int in){
  if(in >= 0){
    if(in > 255){
      Serial.print("invalid PWM level: ");
      Serial.println(in);
    }else{
      setPWM(in);
    }
  }
}

// handle a byte read from the UART
static void handle_uart(int in){
  static enum {
    STATE_BEGIN,
    STATE_PWM, // got a 'P'
    STATE_RRGB, // got a 'C'
    STATE_GRGB, // got a Red
    STATE_BRGB, // got a Green
  } state = STATE_BEGIN;
  static byte uart_red;
  static byte uart_green;
  switch(state){
    case STATE_BEGIN:
      if(in == 'P'){
        state = STATE_PWM;
      }else if(in == 'C'){
        state = STATE_RRGB;
      }else{
        Serial.println("invalid uart input");
      }
      break;
    case STATE_PWM:
      apply_pwm(in);
      state = STATE_BEGIN;
      break;
    case STATE_RRGB:
      uart_red = in;
      state = STATE_GRGB;
      break;
    case STATE_GRGB:
      uart_green = in;
      state = STATE_BRGB;
      break;
    case STATE_BRGB:
      state = STATE_BEGIN;
      Red = uart_red;
      Green = uart_green;
      Blue = in;
      apply_rgb();
      break;
    default:
      Serial.println("invalid uart state");
      break;
  }
}

// read bytes from UART/USB, using the global references.
// each byte is interpreted as a PWM level, and ought be between [0..255].
// we act on the last byte available. USB has priority over UART.
static void check_pwm_update(void){
  int last = -1;
  int in;
  // only apply the last in a sequence
  while((in = UART.read()) != -1){
    Serial.print("read byte from uart: ");
    Serial.println(in);
    handle_uart(in);
  }
  while((in = Serial.read()) != -1){
    Serial.print("read byte from usb: ");
    Serial.println(in);
    last = in;
  }
  apply_pwm(last);
}

float readThermistor(void){
  const int BETA = 3435; // https://www.alphacool.com/download/kOhm_Sensor_Table_Alphacool.pdf
  const float NOMINAL = 25;
  const float R1 = 10000;
  const float VREF = 3.3;
  float v0 = analogRead(TEMPPIN);
  Serial.print("read raw voltage: ");
  Serial.print(v0);
  float scaled = v0 * (VREF / 1023.0);
  Serial.print(" scaled: ");
  Serial.print(scaled);
  float R = ((scaled * R1) / (VREF - scaled)) / R1;
  Serial.print(" R: ");
  Serial.println(R);
  float t = 1.0 / ((1.0 / NOMINAL) - ((log(R)) / BETA));
  Serial.print("read raw temp: ");
  Serial.println(t);
  return t;
}

const unsigned long LOOPUS = 1000000;

void loop (){
  static unsigned long m = 0;
  unsigned long cur;

  if(m == 0){
    m = micros();
  }
  do{
    cur = micros();
    // handle micros() overflow...
    if(cur < m){
      if(m + LOOPUS > m){
        break;
      }else if(cur > m + LOOPUS){
        break;
      }
    }
  }while(cur - m < LOOPUS);
  noInterrupts();
  unsigned p = Pulses;
  Pulses = 0;
  interrupts();
  if((unsigned long)p * 30 > 65535){
    Serial.print("invalid RPM read: ");
    Serial.print(p);
  }else{
    unsigned c = p * 30;
    UART.print("R");
    UART.print(c);
    Serial.print(RPMPIN, DEC);
    Serial.print(" ");
    Serial.print(p, DEC);
    Serial.print(" ");
    Serial.print(c, DEC);
    Serial.print(" rpm");
  }
  Serial.print(' ');
  Serial.print(cur - m, DEC);
  Serial.println("µs");
  Serial.println("");
  float therm = readThermistor();
  UART.print("T");
  UART.print(therm);
  Serial.print("Thermistor: ");
  Serial.print(therm);
  UART.print("P");
  UART.print(Pwm);
  Serial.print(" PWM output: ");
  Serial.print(Pwm);
  Serial.println();
  Serial.print("R");
  Serial.print(p * 30);
  Serial.print("T");
  Serial.print(therm);
  Serial.print("P");
  Serial.println(Pwm);
  check_pwm_update();
  m = cur;
}
