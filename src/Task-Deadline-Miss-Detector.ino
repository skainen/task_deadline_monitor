
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ==================================================
// PIN CONFIGURATION
// ==================================================

#define SDA_PIN       8
#define SCL_PIN       9
#define LED_PIN       40
#define DHT_PIN       5
#define BUTTON_PIN    0  // Onboard BOOT button

#define DHT_TYPE      DHT22

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

const uint32_t NORMAL_WORK_MS = 250;
const uint32_t OVERRUN_WORK_MS = 2500;

#define SIMULATE_OVERRUN 1

#define SENSOR_PRIORITY 2
#define OTHER_PRIORITY  1

// ==================================================
// DATA STRUCTURES
// ==================================================

struct SensorData {

  float temperature;
  float humidity;

  uint32_t executionTime;

  bool sensorValid;
  bool deadlineMissed;
};

enum Command {
  ACKNOWLEDGE_WARNING
};

// ==================================================
// QUEUES
// ==================================================

QueueHandle_t displayQueue;
QueueHandle_t ledQueue;
QueueHandle_t commandQueue;

// ==================================================
// OLED DISPLAY FUNCTIONS
// Only DisplayTask accesses OLED
// ==================================================

void showNormal(const SensorData &data) {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("ENVIRONMENT MONITOR");

  display.setCursor(0, 12);
  display.println("STATUS: NORMAL");

  display.setCursor(0, 26);

  if (data.sensorValid) {

    display.print("Temp: ");
    display.print(data.temperature, 1);
    display.println(" C");

    display.setCursor(0, 38);
    display.print("Humidity: ");
    display.print(data.humidity, 1);
    display.println(" %");

  } else {

    display.println("DHT22 SENSOR ERROR");

    display.setCursor(0, 38);
    display.println("Check wiring");
  }

  display.setCursor(0, 52);
  display.println("Deadline: OK");

  display.display();
}

// ==================================================
// WARNING DISPLAY
// ==================================================

void showWarning(const SensorData &data) {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("!!! WARNING !!!");

  display.setCursor(0, 12);
  display.println("DEADLINE MISSED");

  display.setCursor(0, 26);

  if (data.sensorValid) {

    display.print("Temp: ");
    display.print(data.temperature, 1);
    display.println(" C");

    display.setCursor(0, 38);
    display.print("Humidity: ");
    display.print(data.humidity, 1);
    display.println(" %");

  } else {

    display.println("Sensor unavailable");
  }

  display.setCursor(0, 52);
  display.print("Exec: ");
  display.print(data.executionTime);
  display.println(" ms");

  display.display();
}

// ==================================================
// TASK 1: SENSOR TASK
// Priority 2
// ==================================================

void sensorTask(void *parameter) {

  Serial.println("SensorTask started");

  // Sensor stabilization
  vTaskDelay(pdMS_TO_TICKS(2000));

  TickType_t lastWakeTime = xTaskGetTickCount();

  const TickType_t period =
      pdMS_TO_TICKS(TASK_PERIOD_MS);

  uint32_t cycleCount = 0;

  bool warningLatched = false;

  while (1) {

    // ----------------------------------------------
    // RECEIVE USER COMMANDS
    // ----------------------------------------------

    Command receivedCommand;

    while (xQueueReceive(
      commandQueue,
      &receivedCommand,
      0
    ) == pdPASS) {

      if (receivedCommand == ACKNOWLEDGE_WARNING) {

        warningLatched = false;

        Serial.println("Warning acknowledged!");
      }
    }

    // ----------------------------------------------
    // START EXECUTION TIME
    // ----------------------------------------------

    uint32_t startTime = millis();

    // ----------------------------------------------
    // READ DHT22
    // ----------------------------------------------

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    bool sensorValid =
      !isnan(temperature) && !isnan(humidity);

    if (!sensorValid) {

      Serial.println("DHT22 reading failed!");
    }

    // ----------------------------------------------
    // SIMULATE WORKLOAD
    // ----------------------------------------------

    uint32_t workload = NORMAL_WORK_MS;

#if SIMULATE_OVERRUN

    if ((cycleCount % 5) == 4) {

      workload = OVERRUN_WORK_MS;

      Serial.println("Simulating deadline overrun...");
    }

#endif

    vTaskDelay(pdMS_TO_TICKS(workload));

    // ----------------------------------------------
    // MEASURE EXECUTION TIME
    // ----------------------------------------------

    uint32_t executionTime = millis() - startTime;

    // ----------------------------------------------
    // DEADLINE CHECK
    // ----------------------------------------------

    if (executionTime > TASK_PERIOD_MS) {

      warningLatched = true;

      Serial.println("!!! DEADLINE MISSED !!!");
    }

    // ----------------------------------------------
    // PREPARE SENSOR DATA
    // ----------------------------------------------

    SensorData data;

    data.temperature = temperature;
    data.humidity = humidity;

    data.executionTime = executionTime;

    data.sensorValid = sensorValid;
    data.deadlineMissed = warningLatched;

    // ----------------------------------------------
    // SEND DATA TO OTHER TASKS
    // ----------------------------------------------

    xQueueOverwrite(displayQueue, &data);

    xQueueOverwrite(ledQueue, &data);

    // ----------------------------------------------
    // SERIAL MONITOR
    // ----------------------------------------------

    if (sensorValid) {

      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.println(" C");

      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");
    }

    Serial.print("Execution time: ");
    Serial.print(executionTime);
    Serial.println(" ms");

    if (warningLatched) {

      Serial.println("WARNING ACTIVE");

    } else {

      Serial.println("SYSTEM NORMAL");
    }

    // ----------------------------------------------
    // PERIODIC SCHEDULING
    // ----------------------------------------------

    vTaskDelayUntil(
      &lastWakeTime,
      period
    );

    cycleCount++;
  }
}

// ==================================================
// TASK 2: DISPLAY TASK
// Priority 1
// ==================================================

void displayTask(void *parameter) {

  Serial.println("DisplayTask started");

  SensorData receivedData;

  while (1) {

    if (xQueueReceive(
      displayQueue,
      &receivedData,
      portMAX_DELAY
    ) == pdPASS) {

      if (receivedData.deadlineMissed) {

        showWarning(receivedData);

      } else {

        showNormal(receivedData);
      }
    }
  }
}

// ==================================================
// TASK 3: BOOT BUTTON + LED TASK
// Priority 1
// ==================================================

void buttonLedTask(void *parameter) {

  Serial.println("ButtonLEDTask started");

  SensorData receivedData;

  bool warningActive = false;

  // Button debounce
  bool lastRawState = HIGH;
  bool stableState = HIGH;

  uint32_t lastDebounceTime = 0;

  const uint32_t debounceDelay = 30;

  // LED control
  bool ledState = false;

  uint32_t lastBlinkTime = 0;

  while (1) {

    // ----------------------------------------------
    // RECEIVE SENSOR STATUS
    // ----------------------------------------------

    if (xQueueReceive(
      ledQueue,
      &receivedData,
      pdMS_TO_TICKS(10)
    ) == pdPASS) {

      warningActive = receivedData.deadlineMissed;
    }

    // ----------------------------------------------
    // READ BOOT BUTTON
    // ----------------------------------------------

    bool rawState = digitalRead(BUTTON_PIN);

    if (rawState != lastRawState) {

      lastDebounceTime = millis();

      lastRawState = rawState;
    }

    if ((millis() - lastDebounceTime) >= debounceDelay) {

      if (rawState != stableState) {

        stableState = rawState;

        // Button pressed
        if (stableState == LOW) {

          if (warningActive) {

            Command command = ACKNOWLEDGE_WARNING;

            if (xQueueSend(
              commandQueue,
              &command,
              0
            ) == pdPASS) {

              warningActive = false;

              ledState = false;

              digitalWrite(LED_PIN, LOW);

              Serial.println("BOOT pressed: warning reset");
            }

          } else {

            Serial.println("BOOT pressed: no active warning");
          }
        }
      }
    }

    // ----------------------------------------------
    // LED WARNING INDICATION
    // ----------------------------------------------

    if (warningActive) {

      if (millis() - lastBlinkTime >= 300) {

        lastBlinkTime = millis();

        ledState = !ledState;

        digitalWrite(LED_PIN, ledState);
      }

    } else {

      ledState = false;

      digitalWrite(LED_PIN, LOW);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);

  // Hardware initialization
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);

  dht.begin();

  // OLED initialization
  if (!display.begin(
    SSD1306_SWITCHCAPVCC,
    0x3C
  )) {

    Serial.println("OLED initialization failed!");

    while (1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("FreeRTOS Project");

  display.setCursor(0, 20);
  display.println("Environmental");

  display.setCursor(0, 32);
  display.println("Monitoring System");

  display.display();

  // ----------------------------------------------
  // CREATE QUEUES
  // ----------------------------------------------

  displayQueue = xQueueCreate(
    1,
    sizeof(SensorData)
  );

  ledQueue = xQueueCreate(
    1,
    sizeof(SensorData)
  );

  commandQueue = xQueueCreate(
    5,
    sizeof(Command)
  );

  if (
    displayQueue == NULL ||
    ledQueue == NULL ||
    commandQueue == NULL
  ) {

    Serial.println("Queue creation failed!");

    while (1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  Serial.println("All queues created successfully");

  // ----------------------------------------------
  // CREATE SENSOR TASK
  // ----------------------------------------------

  if (xTaskCreate(
    sensorTask,
    "SensorTask",
    4096,
    NULL,
    SENSOR_PRIORITY,
    NULL
  ) != pdPASS) {

    Serial.println("SensorTask creation failed!");
  }

  // ----------------------------------------------
  // CREATE DISPLAY TASK
  // ----------------------------------------------

  if (xTaskCreate(
    displayTask,
    "DisplayTask",
    4096,
    NULL,
    OTHER_PRIORITY,
    NULL
  ) != pdPASS) {

    Serial.println("DisplayTask creation failed!");
  }

  // ----------------------------------------------
  // CREATE BUTTON + LED TASK
  // ----------------------------------------------

  if (xTaskCreate(
    buttonLedTask,
    "ButtonLEDTask",
    4096,
    NULL,
    OTHER_PRIORITY,
    NULL
  ) != pdPASS) {

    Serial.println("ButtonLEDTask creation failed!");
  }

  Serial.println("All FreeRTOS tasks created");
}

// ==================================================
// LOOP
// ==================================================

void loop() {

  vTaskDelay(pdMS_TO_TICKS(1000));
}
