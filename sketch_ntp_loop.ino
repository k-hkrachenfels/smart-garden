#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <LinkedList.h>
#include <DHT.h>

ESP8266WebServer server(80);
const int humidityPin = A0; // -> replace
const int analogPin = A0;
DHT dht(D4,DHT11);

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

// Pins to select analog channel
int s0 = D6;
int s1 = D7;
int s2 = D8;

// analog multiplexer setup
void setupMultiplexerSelectPins(){
  pinMode(s0, OUTPUT);
  pinMode(s1, OUTPUT);
  pinMode(s2, OUTPUT);
}

// analog multiplexer setup
int readAnalogPin( int pinNum) {
  int input = 0;
  int s0_val = pinNum & 1 ? HIGH : LOW;
  int s1_val = (pinNum >> 1) & 1 ? HIGH : LOW;
  int s2_val  = (pinNum >> 2) & 1 ? HIGH : LOW;
  digitalWrite(s0,s0_val);
  digitalWrite(s1,s1_val);
  digitalWrite(s2,s2_val);

  Serial.print(s2_val);
  Serial.print(s1_val);
  Serial.println(s0_val);
      
  input = analogRead(analogPin);
  Serial.print(" analogPin: ");
  Serial.print(pinNum);
  Serial.print(", value: ");
  Serial.println(input);
  delay(1000);  
  return input;
}


class JsonMappable{
  private:
  public:
    virtual JsonObject toJson( JsonObject parent)=0;
};

// ------- Conditions -------------
/**
 * Note: not yet used
 * 
 * Plan: each timer can be combined with condition
 * e.g. water on between 8:00 and 8:30 when condition humidity<20 holds
 * i.e. condition conbines sensor values.
 */
class Condition : public JsonMappable {
  private:
  public:
    virtual bool check(uint temp, uint humidity) =0;
    virtual String description()=0;
};

class Condition0 : public Condition{
    private:
  public:
    virtual bool check(uint temp, uint humidity){
      return true;
    }
    virtual String description(){
      return "always true";
    }
    
    virtual JsonObject toJson(JsonObject parent){
      JsonObject object = parent.createNestedObject("Condition0");
      object["description"] = description();
      return object;
    };
};

class Condition1: public Condition{
    private:
  public:
    virtual bool check(uint temp, uint humidity){
      return temp>5 && humidity<10;
    }
    virtual String description(){
      return "temp>x && humidity<y";
    }
    virtual JsonObject toJson(JsonObject parent){
      JsonObject object = parent.createNestedObject("Condition1");
      object["description"] = description();
      object["x"]=5;
      object["y"]=10;
      return object;
    };
};


// ------- Pins -------------
// a digital output pin
class Pin{
  private:
    int pin;
    int state;
    int requestedState;
  public:
    Pin(int pin, int state): state(state), pin(pin){
      digitalWrite(pin, state);
      Serial.printf("pin %d set to %d\n", pin, state);
    }
    void setState( int new_state){
      state = new_state;
      digitalWrite(pin, state);
      Serial.printf("pin %d set to %d\n", pin, state);
    }
    int readState(){
      return state;
    }
    void activate(){
      pinMode(pin, OUTPUT);
      digitalWrite(pin, state);
    }
    void requestState( uint requestMe){
      requestedState = requestMe;
    }
    void commitRequestedState(){
      setState( requestedState );
    }
    void toggleState(){
      if(state==HIGH){
        setState(LOW);
      } else if(state==LOW){
        setState(HIGH);
      } else  {
        Serial.println("invalid state"); // TODO: errorhandling
      }
    }
};

Pin pins[] = {Pin(D0, HIGH), Pin(D1,HIGH)};

Condition *conditions[] = {new Condition0(), new Condition1()};

// ------- SENSORS -------------
const int AirValue = 850;   
const int WaterValue = 350;  
float getHumidity(){
  int sensorVal = analogRead(humidityPin);
  int return_val = 100*(AirValue - sensorVal)/(AirValue-WaterValue);
  if(return_val>100)
    return_val=100;
  else if(return_val<0)
    return_val=0;
  return return_val;
}

// ------- REST CALLS -------------
void toggle_0(){
  //ledState0 = !ledState0;
  //digitalWrite(ledPin0, ledState0);
  //server.send(200, "text/plain", "LED0 toggled");
}
void toggle_1(){
  //ledState1 = !ledState1;
  //digitalWrite(ledPin1, ledState1);
  //server.send(200, "text/plain", "LED1 toggled");
}
void on_0(){
  //ledState0 = HIGH;
  //digitalWrite(ledPin0, ledState0);
  //server.send(200, "text/plain", "LED0 HIGH");
}
void on_1(){
  //ledState1 = HIGH;
  //digitalWrite(ledPin1, ledState1);
  //server.send(200, "text/plain", "LED1 HIGH");
}
void off_0(){
  //ledState0 = LOW;
  //digitalWrite(ledPin0, ledState0);
  //server.send(200, "text/plain", "LED0 LOW");
}
void off_1(){
  //ledState1 = LOW;
  //digitalWrite(ledPin1, ledState1);
  //server.send(200, "text/plain", "LED1 LOW");
}

/*****************************************************
 * REST Api to query sensors
 *****************************************************/
void readSensors(){
  StaticJsonDocument<300> doc;
  doc["humidity"]=getHumidity();
  doc["air_humidity"]=dht.readHumidity();
  doc["temperature"]=dht.readTemperature();
  String output;
  serializeJsonPretty(doc, output);
  Serial.println();
  serializeJsonPretty(doc, Serial);
  server.send(200,"text/json",output);
  for( int i=0; i<4; i++)
    readAnalogPin(i);
}



// ------- TIMERS -------------
/*****************************************************
 * Timer Class definition
 *****************************************************/
class Timer : public JsonMappable {
  private:
  public:
    uint start_hour;
    uint start_minute;
    uint end_hour;
    uint end_minute;
    uint pin;
    Condition *condition;
    
    Timer(uint start_hour, uint start_minute, uint end_hour, uint end_minute, uint pin, Condition *condition) : 
          start_hour(start_hour), start_minute(start_minute), 
          end_hour(end_hour), end_minute(end_minute),
          pin(pin), condition(condition)
    {};   

    uint start_minutes(){
       return start_hour*60+start_minute;
    }
    
    boolean gt( Timer *other){
        return start_minutes() > other->start_minutes();
    }
    
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
      else if( hour==end_hour && minute >= end_minute)
        return true;
      return false;
    }

    Condition *getCondition(){
      return condition;
    }

    virtual JsonObject toJson(JsonObject parent){
      JsonObject object = parent.createNestedObject("Timer");
      object["start_hour"] = start_hour;
      object["start_minute"] = start_minute;
      object["end_hour"] = end_hour;
      object["end_minute"] = end_minute;
      object["pin"] = pin;
      Condition *condition = getCondition();
      condition->toJson(object); // return value not needed
      return object;
    };
    
};

int compare(Timer *&a, Timer *&b);
int compare(Timer *&a, Timer *&b) {
  if(a->start_minutes()<b->start_minutes())
    return -1;
  else if(a->start_minutes()>b->start_minutes())
    return 1;
  else
    return 0;
}

/*****************************************************
 * Declare pointer to List with timers
 *****************************************************/
LinkedList<Timer*> *timerList = NULL;

/*****************************************************
 * proc to init Timers by filling list
 *****************************************************/
LinkedList<Timer*>* initTimers(){
    LinkedList<Timer*>* timerList = new LinkedList<Timer*>();
    timerList->add(new Timer(19, 50, 20, 20, 0, new Condition0()));
    timerList->add(new Timer(8, 00, 8, 30, 0, new Condition0()));
    timerList->add(new Timer(10, 00, 10, 15, 0, new Condition0()));
    timerList->add(new Timer(0, 0, 24, 0, 1, new Condition1()));
    timerList->sort(compare); // order is only relevant for displaying timers
    return timerList;
}

void addTimer( uint start_hour, uint start_minute, uint end_hour, uint end_minute, uint pin, uint condition_num){
  Timer* timer = new Timer(start_hour, start_minute, end_hour, end_minute, pin, conditions[condition_num]);
  timerList->add(timer);
  timerList->sort(compare);
}

void deleteTimer( uint index){
  Timer *removeMe = timerList->remove(index);
}

/*****************************************************
 * REST call to query timers
 *****************************************************/

StaticJsonDocument<2048> doc;
void timers(){
    //DynamicJsonDocument doc(1024);
    
    JsonObject root = doc.to<JsonObject>();
    JsonArray timers = root.createNestedArray("timers");
    for (int i = 0; i < timerList->size(); i++) {
      Timer *t = timerList->get(i);
      JsonObject timer = timers.createNestedObject();
      t->toJson(timer);
    }
    //serializeJsonPretty(doc,Serial); 
    String output;
    serializeJsonPretty(doc, output);
    Serial.println();
    serializeJsonPretty(doc, Serial);
    server.send(200,"text/json",output);

}

/*****************************************************
 * execute Timer callbacks in 
 * will be executed in fixed intervals e.g. every 
 * 10 seconds
 *****************************************************/
void triggerCallbacks( uint32_t actualTime ){
    if(actualTime - lastCallbackActivationTime < 10)
      return;
    int actualHour = getHours(actualTime);
    int actualMinute =getMinutes(actualTime);
    Serial.print(actualHour);
    Serial.print(":");
    Serial.println(actualMinute);

  int num_pins = sizeof(pins)/sizeof(pins[0]);
  Serial.printf("%d pin(s) configured\n", num_pins);

  for( uint act_pin = 0; act_pin<num_pins; act_pin++){
    Pin *pin=&pins[act_pin];
    pin->requestState(HIGH);
  }
    
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

    Pin *pin=&pins[timer->pin];
    Condition *condition = timer->getCondition();
    if(timer->isBeforeStartTime(actualHour, actualMinute) ){
      Serial.println(" not yet reached");
    } else if(timer->isAfterEndTime(actualHour, actualMinute) ){
      Serial.println(" already passed");
    } else {
      Serial.println(" active");
      float humidity = getHumidity();
      float temperature = dht.readTemperature();
      boolean activateCondition = condition->check(humidity, temperature); // check types
      pin->requestState(LOW);
    }   
  }
  for( uint act_pin = 0; act_pin<num_pins; act_pin++){
    Pin *pin=&pins[act_pin];
    pin->commitRequestedState();
  }
}

void json_to_resource(DynamicJsonDocument jsonBody) {
    uint start_hour = jsonBody["startHour"];
    uint start_minute = jsonBody["startMinute"];
    uint end_hour = jsonBody["endHour"];
    uint end_minute = jsonBody["endMinute"];
    uint pin = jsonBody["pin"];
    uint condition = jsonBody["condition"];
    addTimer(start_hour, start_minute, end_hour, end_minute, pin, condition);
}

DynamicJsonDocument jsonBody(1000); // avoid mem leaks
void post_put_timer() {
    String post_body = server.arg("plain");
    Serial.print("body=");
    Serial.println(post_body);

    // Deserialize the JSON document
    DeserializationError error = deserializeJson(jsonBody, post_body);
  
    // Test if parsing succeeds.
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      server.send(400);
      return;
    }
 
    json_to_resource(jsonBody);
    server.send(200,"text/plain","added Timer");

}



void delete_from_json(DynamicJsonDocument jsonBody) {
    uint id = jsonBody["id"];
    deleteTimer(id);
}
void delete_timer() {
    String post_body = server.arg("plain");
    Serial.print("body=");
    Serial.println(post_body);
    DynamicJsonDocument jsonBody(1000);
    DeserializationError error = deserializeJson(jsonBody, post_body);
  
    // Test if parsing succeeds.
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      server.send(400);
      return;
    }
 
    delete_from_json(jsonBody);
    server.send(200,"text/plain","deleted Timer");

}

// ------- NTP TIME -------------
uint32_t getTime() {
  if (UDP.parsePacket() == 0)
    return 0;
  UDP.read(NTPBuffer, NTP_PACKET_SIZE); 
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  const uint32_t seventyYears = 2208988800UL;
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime+2*3600; // convert to MEZ summer timer
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
  }  
  return actualTime;
}


// ------------ LAN AND NETWORKING ------------

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


// -- setup and main loop

void setup() {
  
  Serial.begin(9600);          
  delay(10);
  Serial.println("\r\n");

  // init digital DHT11 sensor
  dht.begin();
  
  timerList = initTimers();
  startWiFi();                   
  startUDP();

  if(!WiFi.hostByName(NTPServerName, timeServerIP)) { 
    Serial.println("DNS lookup failed. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  // init output pins
  //pinMode(D0, OUTPUT);
  //digitalWrite(ledPin0, ledState0);
  int num_pins = sizeof(pins)/sizeof(pins[0]);
  Serial.printf("%d pin(s) configured\n", num_pins);

  for( uint act_pin = 0; act_pin<num_pins; act_pin++){
    Pin *pin=&pins[act_pin];
    pin->activate();
  }
  
  // register handlers
  server.on("/toggle1", toggle_1);
  server.on("/on1", on_1);
  server.on("/off1", off_1);
  server.on("/toggle0", toggle_0);
  server.on("/on0", on_0);
  server.on("/off0", off_0);
  server.on("/timers", timers);
  server.on("/sensors", readSensors);
  server.on("/timer", HTTP_POST, post_put_timer);
  server.on("/timer", HTTP_PUT, post_put_timer);
  server.on("/timer", HTTP_DELETE, delete_timer);
 
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
