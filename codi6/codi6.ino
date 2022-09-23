#include <FastLED.h>
#define NUM_STRIPS 6
#define NUM_LEDS_PER_STRIP (37 * 2)
#define NUM_LEDS NUM_LEDS_PER_STRIP * NUM_STRIPS
 
CRGB leds[NUM_LEDS];
 
void setup() {
  // tell FastLED there's 15  NEOPIXEL leds on pin 3, starting at index 0 in the led array
  FastLED.addLeds<NEOPIXEL, 3>(leds, 0, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<NEOPIXEL, 6>(leds, NUM_LEDS_PER_STRIP, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<NEOPIXEL, 9>(leds, NUM_LEDS_PER_STRIP * 2, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<NEOPIXEL, 12>(leds, 0, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<NEOPIXEL, 15>(leds, NUM_LEDS_PER_STRIP, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<NEOPIXEL, 18>(leds, NUM_LEDS_PER_STRIP * 2, NUM_LEDS_PER_STRIP);
}

void loop() {
  for(int i = 0 ; i < NUM_LEDS ; ++i){
    leds[i] = CRGB::Cyan;
  }
  FastLED.show();
}
