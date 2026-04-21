/*
  A1: Lo-fi Interactive Night Light
  by Isaac Wu
*/

#include "src/RGBConverter/RGBConverter.h"

// Digital Pins
#define BUTTON 2
#define MOTOR 3
#define COLOR_BUTTON 4
#define RED 9
#define GRN 10
#define BLU 11
// Analog Pins
#define PHOTOCELL A1
#define LOFI_POT A2

#define LOFI_HI 320
#define LOFI_LO 305

enum mode {
  RGB,
  LIGHT,
  LOFI,
  SPIN,
  NUM_MODES
};

double _hue = 0;
double _step = 0.01;
byte rgb[3];

mode currMode;
int buttonValue, prevButtonValue;
int colorButtonValue, prevColorButtonValue;
bool freezeColor = false;
int lightValue;
int lofiValue;
int lofiScaled;

RGBConverter _rgbConverter;

// the setup function runs once when you press reset or power the board
void setup() {
  pinMode(BUTTON, INPUT_PULLUP);
  buttonValue = 0;
  prevButtonValue = 0;
  pinMode(COLOR_BUTTON, INPUT_PULLUP);
  colorButtonValue = 0;
  prevColorButtonValue = 0;
  // Set motor pin to output
  pinMode(MOTOR, OUTPUT);
  // Set the red, green, and blue RGB LED pins to output
  pinMode(RED, OUTPUT);
  pinMode(GRN, OUTPUT);
  pinMode(BLU, OUTPUT);

  // Set analog pins to input
  pinMode(PHOTOCELL, INPUT);
  pinMode(LOFI_POT, INPUT);

  // Set up modes
  setMode(RGB);

  // Start serial communication
  Serial.begin(9600);
}

// the loop function runs over and over again forever
void loop() {
  prevButtonValue = buttonValue;
  buttonValue = digitalRead(BUTTON);
  prevColorButtonValue = colorButtonValue;
  colorButtonValue = digitalRead(COLOR_BUTTON);
  lightValue = analogRead(PHOTOCELL);
  lofiValue = analogRead(LOFI_POT);
  lofiScaled = constrain((1.0 * lofiValue - LOFI_LO) / (LOFI_HI - LOFI_LO) * 1023.0, 0, 1023);

  if (prevButtonValue == 1 && buttonValue == 0) {
    mode inputMode = (currMode + 1) % NUM_MODES;
    setMode(inputMode);
  }

  switch (currMode) {
    case RGB:
      _rgbConverter.hslToRgb(_hue, 1, 0.5, rgb);
      setColor(rgb[0], rgb[1], rgb[2]);
      _hue += _step;
      if (_hue > 1) { _hue = 0; }
      break;
    case LIGHT:
      _hue = constrain((lightValue - 50) / 900.0, 0.0, 1.0);
      _rgbConverter.hslToRgb(_hue, 1, 0.5, rgb);
      setColor(rgb[0], rgb[1], rgb[2]);
      break;
    case LOFI:
      if (prevColorButtonValue == 1 && colorButtonValue == 0) {
        freezeColor = !freezeColor;
      }

      if (!freezeColor) {
        _hue = (lofiScaled / 1023.0);
        _rgbConverter.hslToRgb(_hue, 1, 0.5, rgb);
        setColor(rgb[0], rgb[1], rgb[2]);
      }
      break; 
    case SPIN:
      digitalWrite(MOTOR, HIGH);
      break;
    default:
      break;
  }

  delay(50);

  // Serial.print("0, 1023, "); // stops autoscaling
  // Serial.println(_hue);
  Serial.println(lightValue);
  // Serial.println(lofiScaled);
  // Serial.println(currMode);
}

void setMode(mode newMode) {
  // buzzer
  currMode = newMode;
  if (newMode != SPIN) {
    // Turn off motor
    digitalWrite(MOTOR, LOW);
  }
}

void setColor(int red, int grn, int blu) {
  analogWrite(RED, red);
  analogWrite(GRN, grn);
  analogWrite(BLU, blu);
}

