#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <LinkedList.h>

ESP8266WebServer server(80);
int ledPin0 = D0;   // D0
int ledPin1 = D1;  // D1
bool ledState0 = LOW;
bool ledState1 = LOW;
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
          end_hour(start_hour), end_minute(end_minute), 
          relais_num(relais_num), state(STATE_INACTIVE)
    {};   

    boolean isBeforeStartTime(uint hour, uint minute){
      if( hour< start_hour)
        return true;
      else if( hour==start_hour && minute < start_minute)
        return true;
      return false;
    }

    boolean isAfterEndTime(uint hour, uint minute){
      if( hour> end_hour)
        return true;
      else if( hour==end_hour && minute > end_minute)
        return true;
      return false;
    }
};
LinkedList<Timer*> *timerList;

class Condition{
  private:
  public:
    virtual bool check(uint temp, uint humidity) =0;
    virtual String description()=0;
};

class Condition0{
    private:
  public:
    virtual bool check(uint temp, uint humidity){
      return true;
    }
    virtual String description(){
      return "true";
    }
};

class Condition1{
    private:
  public:
    virtual bool check(uint temp, uint humidity){
      return temp>5 && humidity<10;
    }
    virtual String description(){
      return "temp>5 && humidity<10";
    }
};

//Condition conditions[] = {};

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
void toggle_0(){
  ledState0 = !ledState0;
  digitalWrite(ledPin0, ledState0);
  server.send(200, "text/plain", "LED0 toggled");
}
void toggle_1(){
  ledState1 = !ledState1;
  digitalWrite(ledPin1, ledState1);
  server.send(200, "text/plain", "LED1 toggled");
}
void on_0(){
  ledState0 = HIGH;
  digitalWrite(ledPin0, ledState0);
  server.send(200, "text/plain", "LED0 HIGH");
}
void on_1(){
  ledState1 = HIGH;
  digitalWrite(ledPin1, ledState1);
  server.send(200, "text/plain", "LED1 HIGH");
}
void off_0(){
  ledState0 = LOW;
  digitalWrite(ledPin0, ledState0);
  server.send(200, "text/plain", "LED0 LOW");
}
void off_1(){
  ledState1 = LOW;
  digitalWrite(ledPin1, ledState1);
  server.send(200, "text/plain", "LED1 LOW");
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
    Timer *t1 = new Timer(16, 30, 16, 59, 0);
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
    //Serial.printf("\rUTC time:\t%d:%d:%d   ", getHours(actualTime), 
    //              getMinutes(actualTime), getSeconds(actualTime));
    //Serial.println();
  }  
  return actualTime;
}


// LAN AND NETWORKING

void startWiFi() { 
  wifiMulti.addAP("dlink-4DA8", "31415926089612867501764661889901764662708917072004000000000");   
  WiFi.hostname("ESP2"); // TODO - table with mac addresses and host-names here to get
                         // static ip addresses
  Serial.println("Connecting");
  while (wifiMulti.run() != WL_CONNECTED) {  
    delay(250);
    Serial.print('.');
  }
  
  WiFi.hostname("ESP2"); // TODO - table with mac addresses and host-names here to get
                         // static ip addresses
  
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

    Serial.print("Timer ");
    Serial.print(i);
    Serial.print(". ");
    Serial.print(timer->start_hour);
    Serial.print(":");
    Serial.print(timer->start_minute);
    Serial.print(" - ");
    Serial.print(timer->end_hour);
    Serial.print(":");
    Serial.print(timer->end_minute);
    Serial.print(" ");

    if(timer->isBeforeStartTime(actualHour, actualMinute) ){
      Serial.println(" not yet reached");
    } else if(timer->isAfterEndTime(actualHour, actualMinute) ){
      Serial.println(" already passed");
    } else {
      Serial.println(" active");
    }
    
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

  if(!WiFi.hostByName(NTPServerName, timeServerIP)) { 
    Serial.println("DNS lookup failed. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  // define PIN Modes
  pinMode(D0, OUTPUT);
  
  // register handlers
  server.on("/toggle1", toggle_1);
  server.on("/on1", on_1);
  server.on("/off1", off_1);
  server.on("/toggle0", toggle_0);
  server.on("/on0", on_0);
  server.on("/off0", off_0);
  server.on("/timers", timers);
  server.on("/sensors", readSensors);
 
  // Start the server
  server.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println(timeServerIP);
  Serial.println("\r\nSending NTP request ...");
  sendNTPpacket(timeServerIP);  
}

uint32_t lastTriggerTime=0;
void loop() {
  uint32_t actual_time = updateTimes()/10; //TODO: bad !!
  // call trigger at max once a second
  if(lastTriggerTime != actual_time){
    lastTriggerTime = actual_time;
    triggerCallbacks(actual_time*10);
  }
  server.handleClient();
}
