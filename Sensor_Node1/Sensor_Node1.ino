/*
This is the source-code for the Plant-Monitoring sensor Node. 
*/

//constants required for the connection to Blynk Server
#define BLYNK_TEMPLATE_ID "TMPL4LPV8L4vx"
#define BLYNK_TEMPLATE_NAME "Moisture"
#define BLYNK_AUTH_TOKEN "Blyn Token"

#include <coap-simple.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>
#include <IPAddress.h>
#include <esp_random.h>
#include <PubSubClient.h>
#include <InfluxDbClient.h>
#include <BlynkSimpleEsp32.h>

#define SENS_PIN 34                             //pin connected to the moisture sensor
#define NTAP 25                                 //number of samples to acquire 
#define DRY 4096 - 3430                         //calibration of the sensor (empirically achieved)
#define WET 4096 - 1460                        
#define uS_TO_S_FACTOR 1000000ULL               //factor to convert seconds to micro-seconds

const char* ID = "sens1";

/*WIFI SECTION*/
#define WIFI_TIMEOUT 15000                      //maximum time allocated for WiFi connection trial
#define WIFI_ON 26
const char* SSID = "TIM-30637733";
const char* PASS = "Vp26bcyyxNjhHLaa";

/*COAP SECTION*/
#define CoAP 4
#define CoAP_TIMEOUT 5000                       //maximum time dedicated to waiting for server response
const char* URI_MOISTURE = "sens1/moisture";    //server resource were to upload new data
int port = 5684;                                //port to listen on
bool coap_received = 0;                         //flag required for the performance evaluation

/*MQTT SECTION*/
#define MQTT 25
#define MQTT_TIMEOUT 10000
const char* topic = "sens1/sleep_time";         //MQTT topic where to publish data on
const char* mqtt_user = ID;                     //username used in the Broker connection establishment
const char* mqtt_pass = "francesca";            //corresponding password

/*INFLUX SECTION*/
#define INFL 19
const char* INFLUXDB_URL = "https://eu-central-1-1.aws.cloud2.influxdata.com";
const char* INFLUXDB_TOKEN = "influxTOKEN1";
const char* INFLUXDB_ORG = "ALMA MATER STUDIORUM - University of Bologna";
const char* INFLUXDB_BUCKET = "Plant-monitoring";                             //bucket to collect moisture data
const char* evaluation_bucket = "Evaluations";                                //bucket to collect performance evaluation data
const char* eval_token = "influxTOKEN2";

/*BLYNK*/
#define BLYNK_TIMEOUT 5000                            //maximum time allocated for Blynk Server connection trial
#define BLYNK_PIN 16

/*EVALUATION*/
unsigned long tc_start, tc_stop;                      //variable for delay evaluation
unsigned long th_start, th_stop;
RTC_DATA_ATTR int missed_tx = 0;                      //variable to count missed failures in transmissions

int time_to_sleep = 1800;                             //default sleep-time
RTC_DATA_ATTR int new_val = 1800;                     //history of sleep-time updates
RTC_DATA_ATTR int old_val = 1800;

IPAddress serverIP(192,168,1,9);                      //Data Proxy IP address 


WiFiUDP udp;
Coap coap(udp);

WiFiClient espClient;                           
PubSubClient mqtt(espClient);

/*CoAP callback function for Server response messages*/
void coap_response(CoapPacket &packet, IPAddress ip, int port) {
  tc_stop = millis(); 
  char response[packet.payloadlen + 1];
  memcpy(response, packet.payload, packet.payloadlen);
  response[packet.payloadlen] = '\0';

  Serial.print("Response from server: ");
  Serial.println(response);
  Serial.flush();

  if(strcmp(response, "OK") == 0){
    coap_received = 1;
  }else{
    coap_received = 0;
  }
}

/*MQTT callback function to handle publication of updates on topic he sensor has subscribed to*/
void mqtt_callback(char* Topic, byte* payload, unsigned int length) {
  String msg;
  for(int i = 0; i < length; i++){
    msg += (char)payload[i];
  }
  if(strcmp(topic, Topic) == 0){
    new_val = msg.toInt();
    if(new_val > 0){
      if(new_val != old_val){
        Serial.print("New TIME TO SLEEP: ");
        Serial.println(new_val);
      }else{
        Serial.println("No TIME TO SLEEP modifications");
      }
    }else{
      Serial.println("ERROR: must be positive!");
      new_val = old_val;
    }
  }
}

void setup() {
  pinMode(WIFI_ON, OUTPUT);                 //used leds to show connections outcomes
  pinMode(MQTT, OUTPUT);                    //during in-field operations
  pinMode(CoAP, OUTPUT);
  pinMode(INFL, OUTPUT);
  pinMode(BLYNK_PIN, OUTPUT);

  Serial.begin(115200);
  delay(3000);

  //Connecion to WiFi
  Serial.print("Connecting to WiFi...");
  WiFi.begin(SSID, PASS);
  bool wifi = 1;
  unsigned long wifi_connection = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifi_connection) < WIFI_TIMEOUT) {
    digitalWrite(WIFI_ON, wifi);
    delay(500);
    Serial.print(".");
    wifi = !wifi;
  }
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(SSID);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(WIFI_ON, HIGH);

    //set the connection to the Blynk Server
    Blynk.config(BLYNK_AUTH_TOKEN, "blynk.cloud", 80);
    Serial.println("Connection to Blynk server:");
   int blynk_timer = millis();
    bool blink = 0;
    while (!Blynk.connect() && (millis() - blynk_timer) < BLYNK_TIMEOUT) {
      blink = !blink;
      digitalWrite(BLYNK_PIN, blink);
      delay(1000);
    }
    if(Blynk.connected()){
      digitalWrite(BLYNK_PIN, HIGH);
      Serial.println("Connected to Blynk Server");
    }else{
      digitalWrite(BLYNK_PIN, LOW);
      Serial.println("FAILED to connect to Blynk Server");
    }
  }else{
    //if it was impossible to connect to the WiFI go back to sleep
    missed_tx++;
    Serial.println("Failed to connect to the WiFi\nGoing to sleep now");
    Serial.flush();
    digitalWrite(WIFI_ON, LOW);
    esp_sleep_enable_timer_wakeup(time_to_sleep * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
  }

  //define CoAP object
  if (coap.start()) {
    coap.response(coap_response);
  } else {
    Serial.println("\nFailed to initialize Coap Object\n");
  }
  delay(500);

  //define MQTT object
  mqtt.setServer(serverIP, 1883);
  mqtt.setCallback(mqtt_callback);

  /*Start MQTT Session*/
  Serial.println("MQTT session: START");
  digitalWrite(MQTT, HIGH);
  bool mqtt_led = 1;
  unsigned long mqtt_connection = millis();
  while (!mqtt.connected() && millis() - mqtt_connection < MQTT_TIMEOUT) {
    Serial.println("Connecting to MQTT...");
    if (mqtt.connect(ID, mqtt_user, mqtt_pass)) {
      Serial.println("MQTT client connected to the broker");
      mqtt.subscribe(topic);
      digitalWrite(MQTT, HIGH);
    } else {
      mqtt_led = !mqtt_led;
      digitalWrite(MQTT, mqtt_led);
      Serial.print("MQTT client NOT connected, rc = ");
      Serial.println(mqtt.state());
      delay(1000);
    }
  }
  delay(50);

  unsigned long mqtt_listen = millis();
  while(millis() - mqtt_listen < (MQTT_TIMEOUT/2)){
    mqtt.loop();
    digitalWrite(MQTT, HIGH);
  }
  digitalWrite(MQTT, LOW);

  if(new_val != old_val){
    time_to_sleep = new_val;
    old_val = new_val;
  }else{
    Serial.println("No TIME TO SLEEP modifications");
    time_to_sleep = old_val;
  }
  Serial.println("MQTT session: TERMINATED\n");

  //Update the timer wake-up from sleep mode
  esp_sleep_enable_timer_wakeup(time_to_sleep * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(time_to_sleep) + " Seconds\n");

  //Start CoAP Session
  Serial.println("CoAP session: START");
  digitalWrite(CoAP, HIGH);
  int moisture = 0;
  int pause = 1000/NTAP;
  for(int i = 0; i < NTAP; i++){
    moisture += (4096 - analogRead(SENS_PIN));
    delay(pause);
  }
  moisture = moisture / NTAP;                             //averaging of the acquired samples

  if (moisture == 0) {
    Serial.println("ERROR IN READING SENSOR");
    digitalWrite(CoAP, LOW);
  } else {
    moisture = map(moisture, DRY, WET, 0, 100);           //map analog value in the interval [0; 100]
    moisture = constrain(moisture, 0, 100);               //cap the extremis due to manual calibration

    if(Blynk.connected()){
      Blynk.virtualWrite(V0, moisture);                   //update value on Blynk Application
    }
    
    char payload[4];                                      //prepre CoAP Packet payload
    snprintf(payload, sizeof(payload), "%d", moisture);
    Serial.print("PAYLOAD: ");
    Serial.println(payload);

    uint8_t tokenlen = 4;
    uint8_t token[tokenlen];
    esp_fill_random(token, tokenlen);                     //define a random 4 x 8-bits token
    uint16_t messageID = (uint16_t)esp_random();          //define a random MID
    tc_start = millis();
    uint16_t messageid = coap.send(serverIP, port, URI_MOISTURE, COAP_CON, COAP_PUT, token, tokenlen, (uint8_t*)payload, strlen(payload), COAP_TEXT_PLAIN, messageID);
    
    unsigned long timeout = millis() + CoAP_TIMEOUT;
    while (millis() < timeout) {
      coap.loop();                                        //wait for Server response
    }

    Serial.println("CoAP session: TERMINATED");
    digitalWrite(CoAP, LOW);

    if(coap_received == 1){
      coap_received = 0;
      Serial.println("CoAP packet delivery: SUCCESS");
    }else{
      /*Start InfluxDB Session*/
      digitalWrite(INFL, HIGH);
      Serial.println("CoAP packet delivery: FAILED\nSending data to INFLUX-DB");
      InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);
      Point sensor(ID);
      sensor.addTag("Sensor ID", ID);
      sensor.clearFields();
      sensor.addField("moisture", moisture);
      th_start = millis();
      if (!client.writePoint(sensor)) {
        missed_tx++;
        th_stop = millis();
        Serial.print("InfluxDB write failed: ");
        Serial.println(client.getLastErrorMessage());
      }else{
        th_stop = millis();
        Serial.println("Data Sent to INFLUX");
      }
      delay(500);
    }


    unsigned long delta_c = tc_stop - tc_start;
    InfluxDBClient perf(INFLUXDB_URL, INFLUXDB_ORG, evaluation_bucket, eval_token);

    Point missed("missed_tx");
    missed.clearTags();
    missed.clearFields();
    missed.addTag("SensorID", ID);
    missed.addField("missed transmissions", missed_tx);
    if (!perf.writePoint(missed)) {
      Serial.print("Missed | InfluxDB write failed: ");
      Serial.println(perf.getLastErrorMessage());
    }else{
      Serial.println("Missed | Data Sent to INFLUX");
    }

    Point protocol("Latency");
    if(delta_c < CoAP_TIMEOUT){
      protocol.clearTags();
      protocol.clearFields();
      protocol.addTag("Protocol", "coap");
      protocol.addField("delay", delta_c);
      if (!perf.writePoint(protocol)) {
        Serial.print("COAP | InfluxDB write failed: ");
        Serial.println(perf.getLastErrorMessage());
      }else{
        Serial.println("COAP | Data Sent to INFLUX");
      }
    }else{
      unsigned long delta_h = th_stop - th_start;
      protocol.clearFields();
      protocol.clearTags();
      protocol.addField("delay", delta_h);
      protocol.addTag("Protocol", "http");
      if (!perf.writePoint(protocol)) {
        Serial.print("HTTP | InfluxDB write failed: ");
        Serial.println(perf.getLastErrorMessage());
      }else{
        Serial.println("HTTP | Data Sent to INFLUX");
      }
    }


    Serial.println("Going to sleep now");
    Serial.flush(); 
    digitalWrite(WIFI_ON, LOW);
    digitalWrite(INFL, LOW);
    digitalWrite(CoAP, LOW);
    digitalWrite(MQTT, LOW);
    
    esp_deep_sleep_start();                                             //go to sleep
  }
}

void loop() {
  // Nothing to do here
}

