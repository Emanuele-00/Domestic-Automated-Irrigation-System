/*This is the source-code for the Water-Management Sensor Node*/

//constants required for the connection to Blynk Server
#define BLYNK_TEMPLATE_ID "TMPL4LPV8L4vx"
#define BLYNK_TEMPLATE_NAME "Moisture"
#define BLYNK_AUTH_TOKEN "Dz_nmaYygJ4XKOc1Qhw3CxlPI0wCRZFD"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>

#define PUMP_PIN 12             //pin for driving the pump relay
#define WIFI_ON 5
#define GET_PIN 21
#define PUT_PIN 22

#define NTAP 10                 //number of samples to acquire for distance evaluation
#define ECHO_PIN 34             //pins for distance sensor
#define TRIG_PIN 32
#define SOUND_SPEED 0.034

const char* ID = "pump1";

const float STORAGE_AREA = 283.53;              //cross section of the water storage [cm^2]
const float STORAGE_DEPTH = 21.3;               //height of the water storage [cm]
const float MAXIMUM_CAPACITY = 4536.46;         //maximum water capacity [cm^3]
const float MINIMUM_CAPACITY = 1134.115;        //minimum water capacity [cm^3]

const float PUMP_FLUX = 28.6;                   //Water Flux of the implemented pump [(cm^3)/s]
const float PUMP_EFFICIENCY = 0.95;             //pump efficiency (empirically evaluated)

const char* SSID = "TIM-30637733";
const char* PASSWORD  = "Vp26bcyyxNjhHLaa";

IPAddress MQTT_BROKER_IP(192,168,1,9);
#define MQTT_TIMEOUT 20000                      //maximum time allocated for connection to the MQTT Broker during set-up phase
const char* topic_alert = "pump1/alert";        //alert topic
const char* topic_sampling = "pump1/sampling";  //topic to receive sampling frequency updates
const char* clientID = ID;
const char* mqtt_user = ID;                     //username to use in the connection to the MQTT Broker
const char* mqtt_pass = "freya";                 //corresponding password

WebServer server(80);
WiFiClient espClient;                           
PubSubClient mqtt(espClient);  

long duration, distance;
long i = 0;
int percentage;
int sampling_period = 5000;
int new_val = 5000;
int old_val = 5000;
float plant_water, water;
float pump_time = 0;
bool updated = 0;
unsigned long current_time;
unsigned long last_exe = 0;

/*Function to evaluate Water Availability in the storage*/
float get_water(){
  float distanceCm = 0;
  for(int i = 0; i < NTAP; i++){
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    duration = pulseIn(ECHO_PIN, HIGH);
    distanceCm += duration * SOUND_SPEED/2;
    delay(10);
  }
  distanceCm = distanceCm / NTAP;
  float water = (STORAGE_DEPTH - distanceCm) * STORAGE_AREA;
  return water; //cm^3
}

/*MQTT callback function to receive updates about sampling frequency on dedicated topic*/
void mqtt_callback(char* Topic, byte* payload, unsigned int length) {
  String msg;
  for(int i = 0; i < length; i++){
    msg += (char)payload[i];
  }
  if(strcmp(topic_sampling, Topic) == 0){
    new_val = msg.toInt();
    if(new_val > 0){
      if(new_val != old_val){
        sampling_period = new_val;
        old_val = new_val;
        Serial.print("New Sampling Time: ");
        Serial.println(new_val);
      }else{
        sampling_period = old_val;
        Serial.println("No Updates");
      }
    }else{
      Serial.println("ERROR: must be positive!");
      sampling_period = old_val;
    }
  }
}

/*HTTP callback function to handle accesses to root*/
void homePage() {
  digitalWrite(GET_PIN, HIGH);
  String message = "Device ID: PUMP 1:\nAvailable topics:\n-/pump1\n -/Water\n -/irrigation";
  Serial.println("ACCESSED Home Page");
  server.sendHeader("Content-Type", "text/plain; charset=utf-8");
  server.send(200, "text/plain", message);
  delay(500);
  digitalWrite(GET_PIN, LOW);
}
/*HTTP callback function to provide water availability inside the storage*/
void Water(){
  digitalWrite(GET_PIN, HIGH);
  float water = get_water();
  water = water - MINIMUM_CAPACITY;
  if(water < 0){
    water = 0;
  }
  
  //Transmit data with JSON format
  StaticJsonDocument<200> doc;
  doc["Water"] = water;
  String message;
  serializeJson(doc, message);
  server.send(200, "application/json", message);
  Serial.print("ACCESSED Water data: ");
  Serial.println(water);
  digitalWrite(GET_PIN, LOW);
}
/*HTTP function to handle PUT requests*/
void handlePut() {
  digitalWrite(PUT_PIN, HIGH);
  Serial.println("Incoming PUT request...");
  if (server.hasArg("plain") == false) {
    server.send(400, "text/plain", "400: Invalid Request");
    return;
  }

  //incoming data are expected to be in JSON format
  String jsonData = server.arg("plain");
  const size_t capacity = JSON_OBJECT_SIZE(10) + 200;
  DynamicJsonDocument doc(capacity);
  DeserializationError error = deserializeJson(doc, jsonData);

  if (error){
    server.send(400, "application/json", "{\"status\":\"400\",\"error\":\"Invalid JSON\"}");
    return;
  }else{
    String response = "Received JSON data:\n";
    for (JsonPair kv : doc.as<JsonObject>()) {
      response += String(kv.key().c_str()) + ": " + String(kv.value().as<String>()) + "\n";
    }
    server.send(200, "text/plain", response);
    plant_water = doc["water"];
    if(plant_water > water - MINIMUM_CAPACITY){
      plant_water = water - MINIMUM_CAPACITY;
    }

    pump_time = (plant_water/(PUMP_FLUX * PUMP_EFFICIENCY))*1000;   //evaluation of the pump active time
    Serial.print("Plant Water: ");
    Serial.println(plant_water);
    Serial.print("Pump Time: ");
    Serial.println(pump_time);
    digitalWrite(PUMP_PIN, LOW);                                    //turn ON the relay
    delay(pump_time);
    digitalWrite(PUMP_PIN, HIGH);                                   //turn OFF the relay
    digitalWrite(PUT_PIN, LOW);
  }
}

//function to acquire control command from the Blynk App
BLYNK_WRITE(V3) {
  int relay = param.asInt();
  digitalWrite(PUMP_PIN, !relay); 
  Serial.print("PUMP: ");
  Serial.println(relay);
  }



void setup(void) {
  pinMode(TRIG_PIN, OUTPUT);                  //sensor pins
  pinMode(ECHO_PIN, INPUT);
  pinMode(PUMP_PIN, OUTPUT);                  //pump pin
  pinMode(WIFI_ON, OUTPUT);                   //leds used during the in-field operation
  pinMode(GET_PIN, OUTPUT);
  pinMode(PUT_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, HIGH);               //immediately turn off the pump

  Serial.begin(115200);

  /*WiFi connection*/
  WiFi.begin(SSID, PASSWORD);
  bool wifi = 1;
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(WIFI_ON, wifi);
    delay(500);
    Serial.print(".");
    wifi = !wifi;
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(WIFI_ON, LOW);

  //make the device visible on the netwok as pump1.local
  if (!MDNS.begin(ID)) {  
    Serial.println("Error starting mDNS");
    return;
  }
  Serial.println("mDNS started");
  digitalWrite(WIFI_ON, HIGH);
  digitalWrite(PUT_PIN, HIGH);
  digitalWrite(GET_PIN, HIGH);

  /*Connection to Blynk Server*/
  Blynk.config(BLYNK_AUTH_TOKEN, "blynk.cloud", 80);
  if (!Blynk.connected()) {
      Serial.println("Attempting to connect to Blynk server...");
      while (!Blynk.connect()) {
        Serial.println("Failed to connect to Blynk server, retrying...");
        delay(1000); 
      }
      Serial.println("Connected to Blynk server");
    }
  delay(1000);
  Blynk.syncVirtual(V3);                              //fectch the last commands uploaded on the app (if any)
  digitalWrite(WIFI_ON, LOW);
  digitalWrite(GET_PIN, LOW);
  digitalWrite(PUT_PIN, LOW);

  mqtt.setServer(MQTT_BROKER_IP, 1883);
  mqtt.setCallback(mqtt_callback);

  //Connect to the MQTT Broker
  unsigned long mqtt_connection = millis();
  while (!mqtt.connected() && millis() - mqtt_connection < MQTT_TIMEOUT) {
    Serial.println("Connecting to MQTT...");
    if (mqtt.connect(clientID, mqtt_user, mqtt_pass)) {
      Serial.println("MQTT client connected to the broker");
      mqtt.subscribe(topic_sampling);
    } else {
      Serial.print("MQTT client NOT connected, rc = ");
      Serial.println(mqtt.state());
      delay(1000);
    }
  }

  server.on("/", homePage);
  server.on("/Water", Water);
  server.on("/irrigation", HTTP_PUT, handlePut);
  server.begin();
  Serial.println("HTTP server started");
}

void loop(void) {
  current_time = millis();
  if(last_exe > current_time){
    last_exe = 0;
  }
  server.handleClient();
  if(mqtt.connected()){
    mqtt.loop();
  }

  //periodically monitor the water and ry connecting to the MQTT Broker in case of previous failures
  if(current_time - last_exe >= sampling_period){
    if(!mqtt.connected()){
      Serial.println("Connecting to MQTT...");
    if (mqtt.connect(clientID, mqtt_user, mqtt_pass)) {
      Serial.println("MQTT client connected to the broker");
      mqtt.subscribe(topic_sampling);
    } else {
      Serial.print("MQTT client NOT connected, rc = ");
      Serial.println(mqtt.state());
      } 
    }
    digitalWrite(WIFI_ON, HIGH);
    water = get_water();
    if(water >= MAXIMUM_CAPACITY && mqtt.connected()){
      mqtt.publish(topic_alert, "100", 1);
    }
    Serial.print(i++);
    Serial.print(" | Water: ");
    Serial.print(water);
    Serial.print(" cm^3 -> ");
    percentage = map(water, 0, MAXIMUM_CAPACITY, 0, 100);
    percentage = constrain(percentage, 0, 100);
    Serial.print(percentage);
    Serial.println("%");
    if(Blynk.connected()){
      Blynk.virtualWrite(V1, percentage);
    }
    digitalWrite(WIFI_ON, LOW);
    last_exe = current_time;
  }
  delay(10);
}
