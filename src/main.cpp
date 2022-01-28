/***********************************************************************************
 * CanAirIO M5CoreInk
 * @author @hpsaturn
 * 
 * This project using CanAirIO Sensors Library. You can find the library here:
 * https://github.com/kike-canaries/canairio_sensorlib
 * 
 * The source code, documentation and the last version of this project here:
 * https://github.com/hpsaturn/co2_m5coreink
 * 
 * Tested with:
 * 
 * - One SCD30 (C02 sensor)
 * - One GCJA5 (Particulate Matter sensor)
 * - ENVII M5 Hat
 * 
 * But you can use it with any other i2c sensors, for example SPS30 or SCD41
 * UART sensors right nos is untested. 
 * 
 ***********************************************************************************/


#include <Arduino.h>
#include <M5CoreInk.h>
#include <Sensors.hpp>
#include <StreamString.h>

#define DEEP_SLEEP_MODE       1     // eInk and esp32 hibernate
#define DEEP_SLEEP_TIME     600     // *** !! Please change it !! *** to 600s (10m) or more 
#define MAX_SAMPLES_COUNT     8     // samples before suspend and show (for PM2.5 ~9, 18sec or more)
#define BEEP_ENABLE           1     // eneble high level alarm
#define PM25_ALARM_BEEP      50     // PM2.5 level to trigger alarm
#define CO2_ALARM_BEEP     2000     // CO2 ppm level to trigger alarm
#define DISABLE_LED                 // improve battery

#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_7C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include "bitmaps/Bitmaps200x200.h"  // 1.54" b/w

GxEPD2_154_M09 medp = GxEPD2_154_M09(/*CS=D8*/ 9, /*DC=D3*/ 15, /*RST=D4*/ 0, /*BUSY=D2*/ 4);
GxEPD2_BW<GxEPD2_154_M09, GxEPD2_154_M09::HEIGHT> display(medp);  // GDEH0154D67

UNIT mainUnit = UNIT::NUNIT;
UNIT minorUnit = UNIT::NUNIT;
UNIT tempUnit = UNIT::NUNIT;
UNIT humiUnit = UNIT::NUNIT;
UNIT otherUnit = UNIT::NUNIT; 

uint16_t samples_count = 0;
bool drawReady;
bool isCharging;
bool isCalibrating;
int calibration_counter = 60;
/************************************************
 *           eINK static GUI methods
 * **********************************************/

void displayHomeCallback(const void*) {
    uint16_t x = 15;
    uint16_t y = display.height() / 2 - 30;
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print("0000");
}

void displayHome() {
    display.setRotation(0);
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextSize(2);
    display.setTextColor(GxEPD_BLACK);
    display.setFullWindow();
    display.drawPaged(displayHomeCallback, 0);
}

/************************************************
 *       eINK sensor values GUI methods
 * **********************************************/

void displayMainValue(UNIT unit){
    uint16_t x = 15;
    uint16_t y = display.height() / 2 - 30;
    display.fillScreen(GxEPD_WHITE);
    display.setTextSize(2);
    display.setCursor(x, y);
    display.setFont(&FreeMonoBold18pt7b);
    display.printf("%04i", (uint16_t)sensors.getUnitValue(mainUnit));
    display.setFont(&FreeMonoBold9pt7b);
    String mainUnitSymbol = sensors.getUnitName(unit)+" / "+sensors.getUnitSymbol(unit);
    uint16_t lenght = mainUnitSymbol.length();
    x = (display.width() / 2) - ((lenght*11)/2);
    y = display.height() / 2 - 8;
    display.setTextSize(0);
    display.setCursor(x, y);
    display.print(mainUnitSymbol);
}

void displayValuesCallback(const void*) {
    displayMainValue(mainUnit);
    uint16_t x = 11;
    uint16_t y = display.height() / 2 + 25;
    String minorName = sensors.getUnitName(minorUnit);
    display.setCursor(x, y);
    display.setFont(&FreeMonoBold12pt7b);
    display.printf("%5s: %04d", minorName.c_str(), (uint16_t)sensors.getUnitValue(minorUnit));

    y = display.height() / 2 + 45;
    display.setCursor(x, y);
    display.printf(" Temp: %0.1fC",sensors.getUnitValue(tempUnit));

    y = display.height() / 2 + 65;
    display.setCursor(x, y);
    display.printf(" Humi: %0.1f%%",sensors.getUnitValue(humiUnit));

    y = display.height() / 2 + 85;
    display.setCursor(x, y);
    String oUnit = sensors.getUnitName(otherUnit);
    display.printf("%5s: %04.1f", oUnit.c_str(),sensors.getUnitValue(otherUnit));

    delay(100);

    drawReady = true;
    Serial.println("done");
}

void calibrationTitleCallback(const void*) {
    displayMainValue(UNIT::CO2);
    uint16_t x = 10;
    uint16_t y = display.height() / 2 + 25;
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextSize(1);
    display.setCursor(x, y);
    display.print("CO2 CALIBRATION:");
    
    display.setTextSize(1);
    display.setCursor(x, y+30);
    display.setTextSize(0);
    display.print("Send command?");

    display.setCursor(x, y + 50);
    display.setTextSize(0);
    display.print("CENTER:    YES");

    display.setCursor(x, y + 70);
    display.setTextSize(0);
    display.print("DOWN  :    NO");
}

void calibrationReadyCallBack(const void*) {
    uint16_t x = 10;
    uint16_t y = display.height() / 2 - 20;
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print("!!CALIBRATED!!");
}

void loadingCallBack(const void*) {
    display.setCursor(10, 190);
    display.setTextSize(0);
    display.print("Restarting..");
}

void displayPartialMode(void (*drawCallback)(const void*)) {
    static uint32_t timeStamp = 0;      
    if ((millis() - timeStamp > 1000)) {  // eInk refresh every 2 seconds
        timeStamp = millis();
        Serial.println("-->[eINK] displayValuesPartialMode");
        Serial.print("-->[eINK] drawing..");
        drawReady = false;
        display.setPartialWindow(0, 0, display.width(), display.height());
        display.setRotation(0);
        // display.setFont(&FreeMonoBold24pt7b);
        display.setTextSize(1);
        display.setTextColor(GxEPD_BLACK);
        display.drawPaged(drawCallback, 0);
    }
}

void displayMessageTitle(void (*drawCallback)(const void*)) {
    display.setRotation(0);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setFullWindow();
    display.drawPaged(drawCallback, 0);
}

/************************************************
 *           Sensors methods
 * **********************************************/

/// for connection and disconnection demo
void resetVariables() {  
    mainUnit = UNIT::NUNIT;
    minorUnit = UNIT::NUNIT;
    tempUnit = UNIT::NUNIT;
    humiUnit = UNIT::NUNIT;
    otherUnit = UNIT::NUNIT;
    sensors.resetUnitsRegister();
    sensors.resetSensorsRegister();
    sensors.resetAllVariables();
}

/// main method for sensors priority and rules
void getSensorsUnits() {
    Serial.println("-->[MAIN] Preview sensor values:");
    UNIT unit = sensors.getNextUnit();
    while(unit != UNIT::NUNIT) {
        if ((unit == UNIT::CO2 || unit == UNIT::PM25) && mainUnit == UNIT::NUNIT) {
            mainUnit = unit;
        } else if (unit == UNIT::CO2 && mainUnit == UNIT::PM25) {
            minorUnit = mainUnit;  // CO2 in indoors has more priority
            mainUnit = unit;       // and is shown in main unit field
        } else if (unit == UNIT::PM10 && minorUnit == UNIT::NUNIT) {
            minorUnit = unit;
        }
        if (unit == UNIT::TEMP || unit == UNIT::CO2TEMP) {
            tempUnit = unit;
            if (mainUnit == UNIT::NUNIT) mainUnit = unit;
        }
        if (unit == UNIT::HUM || unit == UNIT::CO2HUM) {
            humiUnit = unit;
        } 
        if (unit == UNIT::PRESS || mainUnit == UNIT::GAS || mainUnit == UNIT::ALT) {
            otherUnit = unit;
        }
        if (minorUnit == UNIT::NUNIT && unit == UNIT::ALT) minorUnit = unit;
        if (otherUnit == UNIT::NUNIT && unit == UNIT::CO2TEMP) otherUnit = unit;

        String uName = sensors.getUnitName(unit);
        float uValue = sensors.getUnitValue(unit);
        String uSymb = sensors.getUnitSymbol(unit);
        Serial.println("-->[MAIN] " + uName + " \t: " + String(uValue) + " " + uSymb);
        unit = sensors.getNextUnit();
    }
}

void simulateDeepSleep() {   // it's only used on USB mode
    Serial.println("-->[MAIN] Simulate deep sleep");
    sensors.init();
}

void beep() {
    M5.Speaker.tone(2000, 50);
    delay(30);
    M5.Speaker.mute();
}

void shutdown() {
    if (DEEP_SLEEP_MODE == 1) {
        Serial.println("-->[LOOP] Deep sleep..");
        display.display(isCharging);
        display.powerOff();
        M5.shutdown(DEEP_SLEEP_TIME);
        Serial.println("-->[LOOP] USB is connected..");
        isCharging = true;  // it only is reached when the USB is connected
        simulateDeepSleep();
        Serial.println("-->[LOOP] Deep sleep done.");
    }
}

/// sensors callback, here we can get the values
void onSensorDataOk() {
    getSensorsUnits();  // instead of readAllSensors(). See the lib examples
    if (isCalibrating) return;
    samples_count++;
    if (samples_count >= MAX_SAMPLES_COUNT) {
        displayPartialMode(displayValuesCallback);
        uint16_t alarmValue = 0;
        uint16_t mainValue = sensors.getUnitValue(mainUnit);
        if (mainUnit == UNIT::PM25)
            alarmValue = PM25_ALARM_BEEP;
        else
            alarmValue = CO2_ALARM_BEEP;
        if (BEEP_ENABLE == 1 && mainValue > alarmValue) beep();
        samples_count = 0;
    }

    if (drawReady) {
        resetVariables(); // only for demostration connection and reconnection sensors
        shutdown();        
        drawReady = false;
    }
}

void onSensorNoData(const char * msg) {
    Serial.println("-->[MAIN] No data");
    resetVariables();
    if (drawReady) {
        resetVariables(); // only for demostration connection and reconnection sensors
        shutdown();        
        drawReady = false;
    }
    else displayPartialMode(displayValuesCallback);
}

void checkButtons() {
    M5.update();
    if (M5.BtnMID.isPressed()) {
        displayHome();
        sensors.readAllSensors();
        beep();
        displayPartialMode(displayValuesCallback);
    }
    if (M5.BtnUP.isPressed()) {
        displayHome();
        calibration_counter = 60;
        isCalibrating = true;
        while (!M5.BtnMID.isPressed()){
            M5.update();
            sensors.loop();
            displayPartialMode(calibrationTitleCallback);
            if (M5.BtnMID.wasPressed()) {
                beep(); 
                delay(300);
                beep();
                delay(300);
                sensors.setCO2RecalibrationFactor(400);
                displayMessageTitle(calibrationReadyCallBack);
                break;
            }
            if (M5.BtnDOWN.wasPressed()) {
                displayMessageTitle(loadingCallBack);
                samples_count = MAX_SAMPLES_COUNT-2;
                break;
            }
        }
        isCalibrating = false;
    }
}

void sensorsInit() {
    Serial.println("-->[SETUP] Detecting sensors..");
    Wire.begin(32, 33);                          // I2C external port (bottom connector)
    Wire1.begin(25, 26);                         // I2C via Hat (top connector)
    sensors.setSampleTime(2);                    // config sensors sample time interval
    sensors.setOnDataCallBack(&onSensorDataOk);  // all data read callback
    sensors.setOnErrorCallBack(&onSensorNoData); // [optional] error callback
    sensors.setDebugMode(false);                 // [optional] debug mode
    sensors.detectI2COnly(true);                 // force to only i2c sensors
    sensors.init();                              // Auto detection to UART and i2c sensors
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("-->[SETUP] setup init");

#ifdef DISABLE_LED    // turnoff it for improve battery life
    pinMode(LED_EXT_PIN, OUTPUT);
    digitalWrite(LED_EXT_PIN, HIGH);   
#endif
    
    M5.begin(false, false, true);
    sensorsInit();
    display.init(115200,false);
    checkButtons();

    delay(100);
    Serial.println("-->[SETUP] setup done");
}

void loop() {
    sensors.loop();
}
