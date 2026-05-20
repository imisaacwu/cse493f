#include <Adafruit_ADXL343.h>

#define VIBRO A1
#define TRIGGER A4

#define VIBRO_LENGTH 100

Adafruit_ADXL343 accel = Adafruit_ADXL343(12345);

int prev_trigger = 1;
uint32_t t_vibro = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(VIBRO, OUTPUT);
  pinMode(TRIGGER, INPUT_PULLUP);

  if (!accel.begin()) { while(1); }

  Serial.begin(115200);
}

void loop() {
  uint32_t now = millis();
  bool trigger = !digitalRead(TRIGGER);
  sensors_event_t event;
  accel.getEvent(&event);

  Serial.print(event.acceleration.x);
  Serial.print(",");
  Serial.print(event.acceleration.y);
  Serial.print(",");
  Serial.print(event.acceleration.z);

  if (trigger != prev_trigger) {
    Serial.print(",");
    Serial.print(trigger);

    if (trigger) {
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      digitalWrite(LED_BUILTIN, LOW);
    }
  }

  Serial.println();

  if (Serial.available() > 0) {
    Serial.readStringUntil('\n');
    // Only thing being sent is if we got hit, so that's all we need to know.
    digitalWrite(VIBRO, HIGH);
    t_vibro = now;
  }

  if ((uint32_t)(now - t_vibro) >= VIBRO_LENGTH) {
    digitalWrite(VIBRO, LOW);
  }

  prev_trigger = trigger;

  delay(50);
}
