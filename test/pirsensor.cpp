#include <Arduino.h>
#define PIR_PIN 2      // Digital pin connected to sensor's OUT pin

void setup() {
  Serial.begin(115200);         // Start serial communication
  pinMode(PIR_PIN, INPUT);      // Set the PIR sensor pin as input
  
  // Inform the user and allow sensor calibration time
  Serial.println("Initializing AMN-Series PIR sensor...");
  Serial.println("Please wait 30 seconds for calibration...");
  delay(30000);               // Typical warm-up/calibration period
  Serial.println("Sensor is now ready!");
}

void loop() {
  int sensorState = digitalRead(PIR_PIN); // Read sensor output

  if (sensorState == HIGH) {
    Serial.println("Motion detected!");
    
  } else {
    Serial.println("No motion.");

  }
  
  delay(500); // Short delay before next reading
}
