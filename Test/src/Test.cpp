/* 
 * Project myProject
 * Author: Your Name
 * Date: 
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"
#include "credentials.h"
#include "Adafruit_BME280.h"
#include "Adafruit_SSD1306.h"
#include "Adafruit_GFX.h"
#include "IotClassroom_CNM.h"
#include "Grove_Air_quality_Sensor.h"
#include <Adafruit_MQTT.h>
#include "Adafruit_MQTT/Adafruit_MQTT_SPARK.h"
#include "Adafruit_MQTT/Adafruit_MQTT.h"
#include "HX711.h"

TCPClient TheClient; 
Adafruit_MQTT_SPARK mqtt(&TheClient,AIO_SERVER,AIO_SERVERPORT,AIO_USERNAME,AIO_KEY); 

Adafruit_MQTT_Subscribe pumpFeed = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/Pump"); 
Adafruit_MQTT_Publish pubAQ = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/Air_Quality");  
Adafruit_MQTT_Publish pubTemp = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/Temperature"); 
Adafruit_MQTT_Publish pubMoist = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/Moisture"); 
Adafruit_MQTT_Publish pubHumid = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/Humidity");

SYSTEM_MODE(AUTOMATIC);

const int OLED_RESET=-1;

Adafruit_SSD1306 display(OLED_RESET);
Adafruit_BME280 bme;
AirQualitySensor airSensor(A5);
AirQualitySensor airSensor(A0);
IoTTimer pumpTimer;
HX711 myScale (D2, D6);  //pins connected to hx711

const int soilSensor = A0;
const int OLED_RESET=-1;
const int soilSensor = A5;
const int pump = D9;
const int CALFACTOR = 700; 
const int SAMPLES = 10;
const int DRY_THRESHOLD = 1100;
float weight, rawData, calibration;
int offset;
bool status;
bool bmeStatus = false;
bool pumpRunning = false;
bool systemReady = false;
bool wateringEnabled = true;
bool lowWater = false;
float tempC = 0;
float tempF = 0;
float pressurePa = 0;
float pressureInHg = 0;
float humid = 0;
float reservoirWeight = 0;
int soilRead;
int readSoilAverage();
int airQuality = 0;
int pumpCommand = 0;
int lastPumpCommand = 0;
int dryCount = 0;
void startPump(); 
void updateReservoirStatus();
float tempToFah(float measurement);
float pressureToInHg(float measurementPa);
void MQTT_connect();
bool MQTT_ping();
unsigned int lastDisplayMs = 0;
unsigned int lastReadMs = 0;
unsigned int lastPublishMs = 0;
unsigned int lastWaterTime = 0;


void setup() {
   pinMode(pump, OUTPUT);
    digitalWrite(pump, LOW);

  Serial.begin(9600);
    waitFor(Serial.isConnected, 10000);

    myScale.set_scale();
    delay (5000);
    myScale.tare(); 
    myScale.set_scale (CALFACTOR);
    delay(3000);

    pinMode(soilSensor, INPUT);

    mqtt.subscribe(&pumpFeed);

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.display();

    bmeStatus = bme.begin(0x76);
    if (bmeStatus) {
        Serial.printf("BME280 ready.");
    } else {
        Serial.printf("BME280 not found.");
    }

    Serial.printf("Initializing air quality sensor...");
    delay(2000);
    if (airSensor.init()) {
        Serial.printf("Air quality sensor ready.");
    } else {
        Serial.printf("Air quality sensor ERROR.");
    }
 }


void loop() {


    MQTT_connect();
    MQTT_ping();
    weight = myScale.get_units() *-1;
        delay(5000);
        Serial.printf("Raw data = %f\nWeight = %f\n", rawData,weight);
    reservoirWeight = myScale.get_units() *-1;
    updateReservoirStatus();

    Adafruit_MQTT_Subscribe *subscription;
    while ((subscription = mqtt.readSubscription(100))) {
        if (subscription == &pumpFeed) {
            pumpCommand = atoi((char *)pumpFeed.lastread);
            Serial.printf("Pump feed value: %i\n", pumpCommand);
            delay(500);
            if (pumpCommand = 1 && lastPumpCommand == 0 && !pumpRunning) {
                 digitalWrite(pump, HIGH);
                 pumpRunning = true;
                 pumpTimer.startTimer(2000);
                 Serial.printf("Pump started from MQTT");
            }
            lastPumpCommand = pumpCommand;     
         //if(pumpTimer.isTimerReady()){
           // digitalWrite(pump,LOW);

        }

    }
        if (pumpRunning && pumpTimer.isTimerReady()) {
            digitalWrite(pump, LOW);
            pumpRunning = false;
        }       

        if (soilRead > DRY_THRESHOLD && !pumpRunning && millis() - lastWaterTime > 30000 && wateringEnabled) {
            digitalWrite(pump, HIGH);
            pumpRunning = true;
            pumpTimer.startTimer(2000);
            lastWaterTime = millis();
            Serial.printf("Auto watering triggered.");

    }

        if (!wateringEnabled) {
            digitalWrite(pump, LOW);
            Serial.println("Auto watering blocked: low reservoir.");
        }

    if (millis() - lastReadMs >= 8000) {
        lastReadMs = millis();

        if (bmeStatus) {
            tempC = bme.readTemperature();
            pressurePa = bme.readPressure();
            humid = bme.readHumidity();

            tempF = tempToFah(tempC);
            pressureInHg = pressureToInHg(pressurePa);
        }

        soilRead = readSoilAverage();
        airQuality = airSensor.slope();

        if(!systemReady){
            systemReady = true;
            return;
        }

        Serial.printf("TempF: %.1f  Press: %.2f inHg  Humid: %.1f  Soil: %i  AQ raw: %i\n",
                      tempF, pressureInHg, humid, soilRead, airSensor.getValue());

        if (airQuality == AirQualitySensor::FORCE_SIGNAL) {
            Serial.printf("Air quality: High pollution! Force signal active.");
        } else if (airQuality == AirQualitySensor::HIGH_POLLUTION) {
            Serial.printf("Air quality: High pollution.");
        } else if (airQuality == AirQualitySensor::LOW_POLLUTION) {
            Serial.printf("Air quality: Low pollution.");
        } else if (airQuality == AirQualitySensor::FRESH_AIR) {
            Serial.printf("Air quality: Fresh air.");
        }
    }

    if (millis() - lastDisplayMs >= 10000) {
        lastDisplayMs = millis();

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.printf("Temp: %.1f F\n", tempF);
        display.printf("Pres: %.2f inHg\n", pressureInHg);
        display.printf("Hum : %.1f\n", humid);
        display.printf("Soil: %i\n", soilRead);
        display.printf("Air : %i\n", airQuality);
        display.display();
    }

    if (millis() - lastPublishMs >= 15000) {
        lastPublishMs = millis();

        if (mqtt.connected()) {
            pubTemp.publish(tempF);
            pubMoist.publish(soilRead);
            pubHumid.publish(humid);
            pubAQ.publish(airQuality);

            Serial.printf("Published sensor data to Adafruit IO.");
            }
        }
}
    void startPump() {
        if (!pumpRunning && wateringEnabled) {
        digitalWrite(pump, HIGH);   // active-high relay: HIGH = ON
        pumpRunning = true;
        pumpTimer.startTimer(2000);
        lastWaterTime = millis();
    }
}
    void updateReservoirStatus() {
    if (reservoirWeight <= 900) {
        lowWater = true;
        wateringEnabled = false;
    } else {
        lowWater = false;
        wateringEnabled = true;
    }
}


    float tempToFah(float measurement) {
    return (9.0 / 5.0) * measurement + 32.0;
}

    float pressureToInHg(float measurementPa) {
    return measurementPa * 0.0002953;
}
        int readSoilAverage() {
            int total = 0;
            const int samples = 10;

            for (int i = 0; i < samples; i++){
                total += analogRead(soilSensor);
                delay(10);
            }
            return total / samples;
        }

    void MQTT_connect() {
    if (mqtt.connected()) {
        return;
    }

    Serial.printf("Connecting to MQTT... ");

    int8_t ret;
    while ((ret = mqtt.connect()) != 0) {
        Serial.printf("MQTT connect failed: %s\n", mqtt.connectErrorString(ret));
        Serial.printf("Retrying MQTT connection in 5 seconds...");
        mqtt.disconnect();
        delay(5000);
    }

    Serial.printf("MQTT connected.");
}

    bool MQTT_ping() {
    static unsigned long lastPing = 0;

    if (millis() - lastPing > 120000) {
        lastPing = millis();
        if (!mqtt.ping()) {
            mqtt.disconnect();
            return false;
        }
    }
    return true;
    }