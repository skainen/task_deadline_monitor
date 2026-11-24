#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ==================================================
// PIN CONFIGURATION
// ==================================================

#define SDA_PIN     8
#define SCL_PIN     9
#define LED_PIN     40
#define DHT_PIN     5

#define DHT_TYPE    DHT22

// ==================================================
// OLED CONFIGURATION
// ==================================================

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// ==================================================
// DHT22 CONFIGURATION
// ==================================================

DHT dht(DHT_PIN, DHT_TYPE);

// ==================================================
// FREERTOS CONFIGURATION
// ==================================================

const uint32_t TASK_PERIOD_MS = 2000;

// 1 = vTaskDelayUntil()
// 0 = vTaskDelay()
#define USE_DELAY_UNTIL 1

// Simulate an overrun every fifth cycle
#define SIMULATE_OVERRUN 1

const uint32_t NORMAL_WORK_MS = 250;
const uint32_t OVERRUN_WORK_MS = 2500;

// Shared status
volatile bool deadlineMissed = false;

// ==================================================
// OLED: NORMAL STATUS
// ==================================================

void showNormal(float temperature, float humidity) {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("SYSTEM STATUS");

  display.setCursor(0, 12);
  display.println("NORMAL");

  display.setCursor(0, 26);
  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" C");

  display.setCursor(0, 38);
  display.print("Humidity: ");
  display.print(humidity, 1);
  display.println(" %");

  display.setCursor(0, 52);
  display.println("Deadline: OK");

  display.display();
}

// ==================================================
// OLED: DEADLINE WARNING
// ==================================================

void showWarning(float temperature,
                 float humidity,
                 uint32_t executionTime) {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("!!! WARNING !!!");

  display.setCursor(0, 12);
  display.println("DEADLINE MISSED");

  display.setCursor(0, 26);
  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" C");

  display.setCursor(0, 38);
  display.print("Humidity: ");
  display.print(humidity, 1);
  display.println(" %");

  display.setCursor(0, 52);
  display.print("Exec: ");
  display.print(executionTime);
  display.println("ms");

  display.display();
}

// ==================================================
// SENSOR TASK
// ==================================================

void sensorTask(void *parameter) {

  TickType_t lastWakeTime = xTaskGetTickCount();

  const TickType_t period =
      pdMS_TO_TICKS(TASK_PERIOD_MS);

  uint32_t cycleCount = 0;

  while (1) {

    // Record the start of the workload
    uint32_t startTime = millis();

    // Read real DHT22 sensor
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Check sensor reading
    if (isnan(temperature) || isnan(humidity)) {

      Serial.println("DHT22 reading failed!");

      temperature = 0;
      humidity = 0;
    }

    // ==================================================
    // SIMULATE PROCESSING TIME
    // ==================================================

    uint32_t workload = NORMAL_WORK_MS;

#if SIMULATE_OVERRUN

    // Every fifth cycle simulates an overrun
    if ((cycleCount % 5) == 4) {

      workload = OVERRUN_WORK_MS;

      Serial.println("Simulating deadline overrun...");
    }

#endif

    // Simulate processing workload
    vTaskDelay(pdMS_TO_TICKS(workload));

    // Measure elapsed workload time
    uint32_t executionTime = millis() - startTime;

    // ==================================================
    // DEADLINE CHECK
    // ==================================================

    if (executionTime > TASK_PERIOD_MS) {

      deadlineMissed = true;

      Serial.println();
      Serial.println("!!! DEADLINE MISSED !!!");

      Serial.print("Execution time: ");
      Serial.print(executionTime);
      Serial.println(" ms");

      showWarning(
        temperature,
        humidity,
        executionTime
      );
    }
    else {

      deadlineMissed = false;

      Serial.println("Task completed on time.");

      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.println(" C");

      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");

      Serial.print("Execution time: ");
      Serial.print(executionTime);
      Serial.println(" ms");

      showNormal(
        temperature,
        humidity
      );
    }

    // ==================================================
    // PERIODIC SCHEDULING
    // ==================================================

#if USE_DELAY_UNTIL

    // Fixed periodic scheduling
    vTaskDelayUntil(
      &lastWakeTime,
      period
    );

#else

    // Delay after workload
    vTaskDelay(period);

#endif

    cycleCount++;
  }
}

// ==================================================
// LED TASK
// ==================================================

void ledTask(void *parameter) {

  while (1) {

    if (deadlineMissed) {

      // LED ON
      digitalWrite(LED_PIN, HIGH);

      vTaskDelay(pdMS_TO_TICKS(300));

      // LED OFF
      digitalWrite(LED_PIN, LOW);

      vTaskDelay(pdMS_TO_TICKS(300));

    }
    else {

      digitalWrite(LED_PIN, LOW);

      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize DHT22
  dht.begin();

  // Initialize OLED
  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        0x3C)) {

    Serial.println("OLED initialization failed!");

    while (1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  // Initial OLED screen
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("FreeRTOS Project");

  display.setCursor(0, 20);
  display.println("Deadline Monitor");

  display.setCursor(0, 40);
  display.println("DHT22 Starting...");

  display.display();

  // DHT22 stabilization
  delay(2000);

  // ==================================================
  // CREATE SENSOR TASK
  // ==================================================

  xTaskCreate(
    sensorTask,
    "SensorTask",
    4096,
    NULL,
    2,
    NULL
  );

  // ==================================================
  // CREATE LED TASK
  // ==================================================

  xTaskCreate(
    ledTask,
    "LEDTask",
    2048,
    NULL,
    1,
    NULL
  );
}

// ==================================================
// LOOP
// ==================================================

void loop() {

  // FreeRTOS manages the tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}