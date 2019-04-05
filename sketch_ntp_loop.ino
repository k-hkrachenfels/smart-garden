#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <LinkedList.h>



ESP8266WebServer server(80);
int ledPin = 16;  //D0
bool ledState = HIGH;
const int sensorPin = A0; //14
const int humidityPin = 15;  // find include file for A1 for NodeMCU board#

ESP8266WiFiMulti wifiMulti;      
WiFiUDP UDP;                     
IPAddress timeServerIP;          
const char* NTPServerName = "ptbtime1.ptb.de";
const int NTP_PACKET_SIZE = 48;  
byte NTPBuffer[NTP_PACKET_SIZE]; 
unsigned long intervalNTP = 60000; // 1 minute
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
uint32_t timeUNIX = 0;
unsigned long prevActualTime = 0;
unsigned long lastCallbackActivationTime = 0;

#define STATE_INACTIVE 0
#define STATE_ACTIVE 1

class Timer {
  private:
  public:
    uint start_hour;
    uint start_minute;
    uint end_hour;
    uint end_minute;
    uint relais_num;
    int state;
    
    Timer(uint start_hour, uint start_minute, uint end_hour, uint end_minute, uint relais_num) : 
          start_hour(start_hour), start_minute(start_minute), 
          end_hour(start_hour), end_minute(start_minute), 
          relais_num(relais_num), state(STATE_INACTIVE)
    {
    };   
};
LinkedList<Timer*> *timerList;

// ------- SENSORS -------------

float getTemp(){
  int sensorVal = analogRead(sensorPin);
  float voltage = (sensorVal / 1024.0) * 3.3;
  float temperature = (voltage - .5) * 100;
  return temperature;
}

float getHumidity(){
  int sensorVal = analogRead(humidityPin);
  return sensorVal;
}

// ------- REST CALLS -------------
void toggle(){
  ledState = !ledState;
  digitalWrite(ledPin, ledState);
  server.send(200, "text/plain", "LED toggled");
}

void readSensors(){
  StaticJsonDocument<300> doc;
  doc["temp"]=getTemp();
  doc["humidity"]=getHumidity();
  String output;
  serializeJsonPretty(doc, output);
  Serial.println();
  serializeJsonPretty(doc, Serial);
  server.send(200,"text/json",output);
}

LinkedList<Timer*>* initTimers(){
    Timer *t1 = new Timer(10, 0, 10, 10, 0);
    Timer *t2 = new Timer(11, 0, 11, 10, 1);
    LinkedList<Timer*>* timerList =  new LinkedList<Timer*>();
    timerList->add(t1);
    timerList->add(t2);
    return timerList;
}
void timers(){

    DynamicJsonDocument doc(1024);
    JsonObject root = doc.to<JsonObject>();
    JsonArray timers = root.createNestedArray("timers");
    for (int i = 0; i < timerList->size(); i++) {
      Timer *t = timerList->get(i);
      JsonObject timer = timers.createNestedObject();
      timer["start_hour"] = t->start_hour;
      timer["start_minute"] = t->start_minute;
      timer["end_hour"] = t->end_hour;
      timer["end_minute"] = t->end_minute;
      timer["relais_num"] = t->relais_num;
    }
    serializeJsonPretty(doc,Serial); 
    String output;
    serializeJsonPretty(doc, output);
    Serial.println();
    serializeJsonPretty(doc, Serial);
    server.send(200,"text/json",output);
}

// ------- TIME RELATED -------------

uint32_t getTime() {
  if (UDP.parsePacket() == 0)
    return 0;
  UDP.read(NTPBuffer, NTP_PACKET_SIZE); 
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  const uint32_t seventyYears = 2208988800UL;
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}

void sendNTPpacket(IPAddress& address) {
  memset(NTPBuffer, 0, NTP_PACKET_SIZE);  
  NTPBuffer[0] = 0b11100011;   // LI, Version, Mode
  UDP.beginPacket(address, 123); 
  UDP.write(NTPBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}
inline int getSeconds(uint32_t UNIXTime) { return UNIXTime % 60;}
inline int getMinutes(uint32_t UNIXTime) {return UNIXTime / 60 % 60;}
inline int getHours(uint32_t UNIXTime) {return UNIXTime / 3600 % 24;}

uint32_t updateTimes(){
  // NOTE: USES GLOBAL VARS, COULD BE REFACTORED USING A CLASS INSTEAD
  // get system time, but use it only for calculating delta times since last NTP response
  unsigned long currentMillis = millis();

  // once per intervalNTP: update time from NTP
  if (currentMillis - prevNTP > intervalNTP) { 
    prevNTP = currentMillis;
    Serial.println("\r\nSending NTP request ...");
    sendNTPpacket(timeServerIP);               
  }

  // check NTP response and reboot if not lively
  uint32_t time = getTime();                   
  if (time) {                                  
    timeUNIX = time;
    Serial.print("NTP response:\t");
    Serial.println(timeUNIX);
    lastNTPResponse = currentMillis;
  } else if ((currentMillis - lastNTPResponse) > 3600000) {
    Serial.println("More than 1 hour since last NTP response. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  // calculate actual time based on internal clock (currentMillis) and last NTP response
  uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse)/1000;
  if (actualTime != prevActualTime && timeUNIX != 0) { 
    prevActualTime = actualTime;
    Serial.printf("\rUTC time:\t%d:%d:%d   ", getHours(actualTime), 
                  getMinutes(actualTime), getSeconds(actualTime));
    Serial.println();
  }  
  return actualTime;
}


// LAN AND NETWORKING

void startWiFi() { 
  wifiMulti.addAP("dlink-4DA8", "31415926089612867501764661889901764662708917072004000000000");   
  Serial.println("Connecting");
  while (wifiMulti.run() != WL_CONNECTED) {  
    delay(250);
    Serial.print('.');
  }
  Serial.println("\r\n");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());             
  Serial.print("IP address:\t");
  Serial.print(WiFi.localIP());           
  Serial.println("\r\n");
}

void startUDP() {
  Serial.println("Starting UDP");
  UDP.begin(123);                          
  Serial.print("Local port:\t");
  Serial.println(UDP.localPort());
  Serial.println();
}

// -- callbacks --

void triggerCallbacks( uint32_t actualTime ){
    if(actualTime - lastCallbackActivationTime < 10)
      return;
    int actualHour = getHours(actualTime);
    int actualMinute =getMinutes(actualTime);
        Serial.print(actualHour);
    Serial.print(":");
    Serial.println(actualMinute);

  for(int i=0; i<timerList->size(); i++){
    Timer *timer=timerList->get(i);
    Serial.print(i);
    Serial.print(". ");
    Serial.print(timer->start_hour);
    Serial.print(":");
    Serial.println(timer->start_minute);
  }
}

// -- setup and main loop

void setup() {
  
  Serial.begin(9600);          
  delay(10);
  Serial.println("\r\n");

  timerList = initTimers();
  startWiFi();                   
  startUDP();

  //for(int i=0; i<10; i++)
  //  timers();

  if(!WiFi.hostByName(NTPServerName, timeServerIP)) { 
    Serial.println("DNS lookup failed. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  Serial.println(timeServerIP);
  Serial.println("\r\nSending NTP request ...");
  sendNTPpacket(timeServerIP);  


  // define PIN Modes
  pinMode(D0, OUTPUT);
  
  // register handlers
  server.on("/toggle", toggle);
  server.on("/timers", timers);
  server.on("/sensors", readSensors);
 
  // Start the server
  server.begin();
  Serial.println("Server started");

    // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
}


void loop() {
  uint32_t actual_time = updateTimes();
  triggerCallbacks(actual_time);
  server.handleClient();
}
