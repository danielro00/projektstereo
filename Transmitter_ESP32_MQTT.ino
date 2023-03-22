/*
This example uses FreeRTOS softwaretimers as there is no built-in Ticker library
 https://github.com/marvinroger/async-mqtt-client/tree/master/examples/FullyFeatured-ESP32 
!!!  :-) 221108
Board WEMOS LOLIN32
*/

#define SAMPLES 512
uint16_t arr0[SAMPLES] = {0xff00, 0xff01, 0xff02, 0xff03, 0xff04, 0xff05, 0xff06, 0xff07, 0xff08, 0xff09, 0xff0a, 0xff0b, 0xff0c, 0xff0d, 0xff0e, 0xff0f, 0xff10};
#include <WiFi.h>
#include <driver/i2s.h>

TaskHandle_t  Core0TaskHnd ; 
TaskHandle_t  Core1TaskHnd ;
// Semaphore to trigger context switch
SemaphoreHandle_t xSemaphore0;
SemaphoreHandle_t xSemaphore1;
QueueHandle_t integerQueue;     //QueueHandle Reference for collect values in Queue and Send Serial

//Tasks/funcionsprototyping
void TaskSnd(void * pvParameters);
void TaskAnalogRead(void *pvParameters);
void TaskGetSamples(void *pvParameters);
void TaskProcessSamples(void *pvParameters);

// Define input buffer length. Es funktioniert mit 512!
#define bufferLen 512
int32_t sBuffer0[bufferLen][2];
int32_t sBuffer1[bufferLen][2];
int16_t sBuffer2[bufferLen];
int16_t sBuffer3[bufferLen];
size_t _num_bytes_read0 = 0;            //Reference to a byte-array
size_t _num_bytes_read1 = 0;            //Reference to a byte-array
uint8_t read_from[2] = {0,0};

//Configuration of i2s-1 microphone
const i2s_port_t I2S_PORT = I2S_NUM_0;
const i2s_config_t i2s_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = 20000,                          // Abtastung 20 kHz funktioniert!
  .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // Nicht verfügbar außer 32-Bit
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo
  .communication_format = I2S_COMM_FORMAT_PCM,   // PCM
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,      // Interrupt level 1
  .dma_buf_count = 2,                            // number of buffers
  .dma_buf_len = bufferLen                       // 512 samples per buffer
};
const i2s_pin_config_t pin_config = {
  .bck_io_num = 25,                   // BCLK
  .ws_io_num = 27,                    // LRCL
  .data_out_num = I2S_PIN_NO_CHANGE,  // Wird nicht mit einem Mikrofon verwendet.
  .data_in_num = 4                    // DOUT 4/32
};

//general IOs 
//#define pin 34
#define LED_BUILTIN 2
//#define pinLED 33

//Libs for RTOS 
extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}
//Lib for MQTT communication
#include <AsyncMqttClient.h>

// Replace with your network credentials
//const char* ssid = "Vodafone-2564"; ////// ändern
//const char* password = "noisypond318";  ////ändern 
const char* ssid = "iPhoneee"; //
const char* password = "12345678";  //

const char* topic_test = "Überlastsensorik/data/audio_data/Noe/Test/Daniel";

//#define MQTT_HOST IPAddress(192, 168, 1, 10)
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 1883
AsyncMqttClient mqttClient;               //MQTT-Client Object

TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

//char message[2048]; //received mqtt string storage


void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
        connectToMqtt();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("WiFi lost connection");
        xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		    xTimerStart(wifiReconnectTimer, 0);
        break;
    }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  //digitalWrite(pinLED, HIGH);                            //turn on green LED for show ready measure status
  //mqttClient.subscribe(topic_test, 0);

}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  //digitalWrite(pinLED, LOW);                            //Turn off LED for system not ready
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);   //reconnect mqtt
  }
}

uint16_t middle_of_3(uint16_t a, uint16_t b, uint16_t c){
  uint16_t middle;
   if ((a <= b) && (a <= c)) {
     middle = (b <= c) ? b : c;
   }
   else if ((b <= a) && (b <= c)) {
     middle = (a <= c) ? a : c;
   }
   else {
     middle = (a <= b) ? a : b;
   }
   return middle;
}

/**
 * Analog read task
 * Reads an analog input from i2s ADC, 
 * put data to one of two the arrays and give the first semaphore.
 */
void TaskAnalogRead(void *pvParameters) {
  (void) pvParameters;
  //core 1
  int16_t _vL;
  esp_err_t _err;
  while(1){
    _err = i2s_read(I2S_PORT,                     // i2s_read() ist eine blockierende Funktion!
                              (char *)sBuffer0,  //_samples, 
                              8*bufferLen,      //8,  // Anzahl der Bytes, nicht Anzahl der Elemente                            
                              &_num_bytes_read0, 
                              portMAX_DELAY);  // no timeout
    read_from[0] = 0;
    xSemaphoreGive(xSemaphore0);
    _err = i2s_read(I2S_PORT, 
                              (char *)sBuffer1,  //_samples, 
                              8*bufferLen,      //8,  // Anzahl der Bytes, nicht Anzahl der Elemente                            
                              &_num_bytes_read1, 
                              portMAX_DELAY);  // no timeout
    read_from[0] = 1;
    xSemaphoreGive(xSemaphore0);
  }
}
/*
 * Get samples task
 * Take the first semaphore; extract samples from input array into an int16_t array
 * and give the second semaphore 
 */
void TaskGetSamples(void *pvParameters) {
  (void) pvParameters;
  //core 1
  int16_t _vL;
  int16_t _vR;

  while(1){
    xSemaphoreTake(xSemaphore0, portMAX_DELAY);
    if (read_from[0] == 0) {
      for(int i = 0; i < bufferLen; i++){
        _vL = (sBuffer0[i][1]>>16); 
        _vR = (sBuffer0[i][0]>>16);
        _vL = ~_vL;
        _vR = ~_vR;
        if (i%2 == 0) {
          sBuffer2[i] = _vR;
        }else{
          sBuffer2[i] = _vL;
        }
        //sBuffer2[i] = _vR;
        //sBuffer2[i] = _vL;
        xQueueSend(integerQueue, &_vL, 1);//____
        xQueueSend(integerQueue, &_vR, 1);//____
      }
      read_from[1] = 2;
    }else{
      for(int i = 0; i < bufferLen; i++){
        _vL = (sBuffer1[i][1]>>16); 
        _vR = (sBuffer1[i][0]>>16); 
        _vL = ~_vL;
        _vR = ~_vR;
        if (i%2 == 0) {
          sBuffer3[i] = _vR;
        }else{
          sBuffer3[i] = _vL;
        }
        //sBuffer3[i] = _vR;
        //sBuffer3[i] = _vL;
        xQueueSend(integerQueue, &_vL, 1);//____
        xQueueSend(integerQueue, &_vR, 1);//____
      }
      read_from[1] = 3;      
    }
    xSemaphoreGive(xSemaphore1);
  }
}

/*
 * Process samples task
 * Take the second semaphore
 * get data from int16_t array and process it (send to the queue)
 */
void TaskProcessSamples(void *pvParameters) {
  (void) pvParameters;
  //long int count_publish1 = 0;
  //long int count_publish2 = 0;  
  //core 0
  int16_t _vL;
  int16_t _vR;

  while(1){
    xSemaphoreTake(xSemaphore1, portMAX_DELAY);
    if (read_from[1] == 2) {
      // for(int i = 0; i < bufferLen; i++){
      //   _vL = sBuffer2[i];
      //   xQueueSend(integerQueue, &_vL, 1);
      // }
      mqttClient.publish(topic_test, 2, true, (char*)sBuffer2, 2*bufferLen);
      //mqttClient.publish(topic_test, 2, true, sBuffer2, 2*bufferLen);
      //count_publish1++;
      //Serial.println(count_publish1);
      //Serial.println("sBuffer2 published");
    }else{
      // for(int i = 0; i < bufferLen; i++){
      //   _vL = sBuffer3[i];
      //   xQueueSend(integerQueue, &_vL, 1);
      //}
      mqttClient.publish(topic_test, 2, true, (char*)sBuffer3, 2*bufferLen);
      //mqttClient.publish(topic_test, 2, true, sBuffer3, 2*bufferLen);
      //count_publish2++;
      //Serial.println(count_publish2);
      //if (count_publish2 == 512){
      //  exit(0);
      //}
      //Serial.println("sBuffer3 published");
    }
  }
}

//Task for printing values to serial monitor
void TaskSnd(void * pvParameters) {
  (void) pvParameters;
  Serial.println();
  int16_t x, tmp0, i;
  uint8_t mybuffer[10];
  //uint16_t tmp0;
  //core 0 Kernel/WIFI
  while (1) {
    if (xQueueReceive(integerQueue, &x, portMAX_DELAY) == pdPASS) {
      if(x<0){
        x = -x;
        mybuffer[0] = '-';
      }else{
        mybuffer[0] = '+';
      }
      tmp0 = x / 10000;
      x = x % 10000;
      mybuffer[1] = ((char) tmp0 | 0x30);
      tmp0 = x / 1000;
      x = x % 1000;
      mybuffer[2] = ((char) tmp0 | 0x30);
      tmp0 = x / 100;
      x = x % 100;
      mybuffer[3] = ((char) tmp0 | 0x30);
      tmp0 = x / 10;
      x = x % 10;
      mybuffer[4] = ((char) tmp0 | 0x30);
      mybuffer[5] = ((char) x | 0x30);
      //mybuffer[6] = ' ';
      mybuffer[6] = '\n';
      //mybuffer[7] = 0;
      while(Serial.availableForWrite()==0){ 
        digitalWrite(LED_BUILTIN, HIGH);
        yield();
      }
      digitalWrite(LED_BUILTIN, LOW);
      Serial.write(mybuffer, 7);//___
    }else{
      yield();
    }
  }
}

void i2sInit(){
  esp_err_t _err;
  Serial.println("Configuring I2S...");
  _err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (_err != ESP_OK) {
    Serial.printf("Failed installing driver: %d\n", _err);
    while (true)
      ;
  }
  _err = i2s_set_pin(I2S_PORT, &pin_config);
  if (_err != ESP_OK) {
    Serial.printf("Failed setting pin: %d\n", _err);
    while (true)
      ;
  }
  Serial.println("I2S driver installed.");
}

void setup() {
  Serial.begin(2000000);
  i2sInit();
  Serial.println();
  Serial.println();
  //pinMode(pinLED, OUTPUT);        //Init of state_LED MQTT connection established 
    // Initilize hardware serial:
  delay(500);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
  // create semaphore
  vSemaphoreCreateBinary(xSemaphore0);
  vSemaphoreCreateBinary(xSemaphore1);

// Instance Queue Save/Give Samples to TaskSnd for show values in serial monitor
  integerQueue = xQueueCreate(10, // Queue length
                              sizeof(int16_t) // Queue item size
                              );
  // Create task that consumes the queue if it was created.
  xTaskCreatePinnedToCore(
    TaskSnd, // Task function
    "Serial", // A name just for humans
    2000,  // This stack size can be checked & adjusted by reading the Stack Highwater
    NULL,  //Params
    1, // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    &Core0TaskHnd,
    0);

  // Create task that starts ADC with DMA and two buffers.
  xTaskCreatePinnedToCore(
    TaskProcessSamples, // Task function
    "TaskProcessSamples", // Task name
    2000,  // Stack size
    NULL, 
    2, // Priority
    &Core1TaskHnd,
    0); 

  xTaskCreatePinnedToCore(
    TaskGetSamples, // Task function
    "TaskGetSamples", // Task name
    2000,  // Stack size
    NULL,  //Params
    2, // Priority
    &Core1TaskHnd,
    1); 

  // Create task that starts ADC with DMA and two buffers.
  xTaskCreatePinnedToCore(
    TaskAnalogRead, // Task function
    "AnalogRead", // Task name
    2000,  // Stack size
    NULL,  //Params
    3, // Priority
    &Core1TaskHnd,
    1); 


  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  //  mqttClient.onSubscribe(onMqttSubscribe);
  //  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  //mqttClient.onMessage(onMqttMessage);
  //  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT); 

  connectToWifi();
}
//Loop empty --> Tasks defined
void loop(){
}


