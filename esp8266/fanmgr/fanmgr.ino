// intended for use on a Hi-LetGo ESP8266, this manages a PWM fan and two
// PWM pumps, and two thermistors. it receives PWM control messages, and sends
// RPM and temperature reports, over MQTT. unlike the ESP32 version, this
// doesn't run LEDs, nor does it support a pressure sensor.
#define DEVNAME "esp8266"
#include "ESP8266WiFi.h"
#include <float.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <pwm.h>
#include "common.h"

#define PWM_CHANNELS 2

// only one ADC on the ESP8266
const int TEMPPIN = A0; // coolant thermistor (2-wire)

const int FANTACHPIN = D1;
const int AMBIENTPIN = D2; // ambient temperature (digital thermometer, Dallas 1-wire)
const int XTOPATACHPIN = D3;
const int XTOPBTACHPIN = D4; // connected to LED
const int PUMPPWMPIN = D5;
const int FANPWMPIN = D6;

// RPMs as determined by our interrupt handlers.
// we only get the RPM count from one of our fans; it stands for all.
static unsigned RPM;
static unsigned XTopRPMA;
static unsigned XTopRPMB;

static unsigned Pwm;
static unsigned PumpPwm;
static unsigned XTopPWMA;
static unsigned XTopPWMB;

EspMQTTClient client(
  #include "EspMQTTConfig.h"
);

OneWire twire(AMBIENTPIN);
DallasTemperature digtemp(&twire);

// esp8266_pwm_new uses 200ns units. we want 25KHz, which is 40us,
// which is 40k ns, which is 200x 200ns units.
#define PWMPERIOD 200

void setup(){
  Serial.begin(115200);
  Serial.print("booting esp8266 ");
  Serial.println(VERSION);
  client.enableDebuggingMessages();
  client.enableMQTTPersistence();
  pinMode(PUMPPWMPIN, OUTPUT);
  pinMode(FANPWMPIN, OUTPUT);
  uint32_t cycle = PWMPERIOD / 2; // half duty to start
  uint32_t cycles[PWM_CHANNELS] = { cycle, cycle, };
  uint32_t pwms[PWM_CHANNELS][3] = {
    {PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO12, 12},
	  {PERIPHS_IO_MUX_MTMS_U,  FUNC_GPIO14, 14},
  };
  pwm_init(PWMPERIOD, cycles, sizeof(pwms) / sizeof(*pwms), pwms);
  pwm_start();
  /*
  analogWriteFreq(25000);
  */
  set_pwm(INITIAL_FAN_PWM);
  set_pump_pwm(INITIAL_PUMP_PWM);
  pinMode(TEMPPIN, INPUT);
  setup_interrupts(FANTACHPIN, XTOPATACHPIN, XTOPBTACHPIN);
}

// set up the desired PWM value
static int set_pwm(unsigned p){
  //analogWrite(FANPWMPIN, p);
  pwm_set_duty(PWMPERIOD * p / 255, 1);
  Serial.print("configured fan PWM: ");
  Serial.println(p);
  Pwm = p;
  return 0;
}

// set up the desired pump PWM value
static int set_pump_pwm(unsigned p){
  //analogWrite(PUMPPWMPIN, p);
  pwm_set_duty(PWMPERIOD * p / 255, 2);
  Serial.print("configured pump PWM: ");
  Serial.println(p);
  PumpPwm = p;
  return 0;
}

void onConnectionEstablished() {
  Serial.println("got an MQTT connection");
  client.subscribe("control/" DEVNAME "/pwm", [](const String &payload){
      Serial.print("received PWM via mqtt: ");
      Serial.println(payload);
      unsigned long p = 0;
      if(payload.length() == 0){
        Serial.println("empty PWM input");
        return;
      }
      for(int i = 0 ; i < payload.length() ; ++i){
        char h = payload.charAt(i);
        if(!isdigit(h)){
          Serial.println("invalid PWM character");
          return;
        }
        p *= 10; // FIXME check for overflow
        p += h - '0';
      }
      if(valid_pwm_p(p)){
        set_pwm(p);
      }
    }
  );
  client.subscribe("control/" DEVNAME "/pumppwm", [](const String &payload){
      Serial.print("received pump PWM via mqtt: ");
      Serial.println(payload);
      unsigned long p = 0;
      if(payload.length() == 0){
        Serial.println("empty PWM input");
        return;
      }
      for(int i = 0 ; i < payload.length() ; ++i){
        char h = payload.charAt(i);
        if(!isdigit(h)){
          Serial.println("invalid PWM character");
          return;
        }
        p *= 10; // FIXME check for overflow
        p += h - '0';
      }
      if(valid_pwm_p(p)){
        set_pump_pwm(p);
      }
    }
  );
}

// we transmit approximately every 15 seconds, sampling RPMs at this time.
// we continuously sample the temperature, and use the most recent valid read
// for transmit/display. there are several blocking calls (1-wire and MQTT)
// that can lengthen a given cycle.
void loop(){
  static bool onewire_connected;
  static unsigned long last_tx; // micros() when we last transmitted to MQTT
  // these are the most recent valid reads (i.e. we don't reset to FLT_MAX
  // on error, but instead only on transmission).
  static float coolant_temp = FLT_MAX;
  static float ambient_temp = FLT_MAX;
  client.loop(); // handle any necessary wifi/mqtt
  readThermistor(&coolant_temp, TEMPPIN, 1024);
  if(!onewire_connected){
    if(connect_onewire(&digtemp) == 0){
      onewire_connected = true;
    }
  }
  if(onewire_connected){
    if(readAmbient(&ambient_temp, &digtemp)){
      onewire_connected = false;
    }
  }
  unsigned long m = micros();
  unsigned long diff = m - last_tx;
  if(diff < 15000000){
    return;
  }
  // sample RPM and transmit
  // FIXME replace with MEGA's rolling 5s logic
  last_tx = m;
  noInterrupts();
  unsigned long p = Pulses;
  Pulses = 0;
  unsigned long x1p = XTAPulses;
  XTAPulses = 0;
  unsigned long x2p = XTBPulses;
  XTBPulses = 0;
  interrupts();
  Serial.print(diff);
  Serial.println(" µsec expired for cycle");
  Serial.print(p);
  Serial.println(" pulses measured at fan");
  RPM = rpm(p, diff);
  XTopRPMA = rpm(x1p, diff);
  XTopRPMB = rpm(x2p, diff);
  rpmPublish(client, "moraxtop0rpm", XTopRPMA);
  rpmPublish(client, "moraxtop1rpm", XTopRPMB);
  rpmPublish(client, "morarpm", RPM);
  publish_temps(client, ambient_temp, coolant_temp);
  publish_pwm(client, Pwm, PumpPwm);
  publish_uptime(client, millis() / 1000); // FIXME handle overflow
  coolant_temp = FLT_MAX;
  ambient_temp = FLT_MAX;
}
