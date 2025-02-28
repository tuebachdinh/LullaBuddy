#include <Arduino.h>
#define MIC_PIN 34  // GPIO pin connected to the OUT of MAX4466
#define THRESHOLD 2000  // Adjust based on noise level
#define DURATION 5000  // 5 seconds in milliseconds

unsigned long startTime = 0;
bool isLoud = false;

void setup() {
    Serial.begin(115200);
}

void loop() {
    int soundLevel = analogRead(MIC_PIN);  // Read sound level
    Serial.println(soundLevel);  // Print for debugging
    
    if (soundLevel > THRESHOLD) {
        Serial.println("Loud sound detected!");
        if (!isLoud) {
            startTime = millis();  // Start the timer
            isLoud = true;
        }

        if (millis() - startTime >= DURATION) {
            Serial.println("Success: Loud sound detected for 5 seconds!");
            isLoud = false;  // Reset state
        }
    } else {
        isLoud = false;  // Reset if sound drops below threshold
    }

    delay(100);  // Adjust sample rate
}
