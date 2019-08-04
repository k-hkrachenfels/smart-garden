#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <LinkedList.h>
#include <DHT.h>

//use ESpressif SDK for api to set of mac address
extern "C" {
  #include <user_interface.h>
}

void setVorgartenMacAddress(){
  uint8_t mac[6] {0xCC, 0x50, 0xE3, 0x0A, 0x1B, 0xF2};     
  wifi_set_macaddr(0, const_cast<uint8*>(mac));    
}

void setGewaechshausMacAddress(){
  uint8_t mac[6] {0xCC, 0x50, 0xE3, 0x0A, 0x1F, 0x9E};     
  wifi_set_macaddr(0, const_cast<uint8*>(mac));    
}
 

ESP8266WebServer server(80);
const int humidityPin = A0; // -> replace
const int analogPin = A0;
DHT dht(D4,DHT11);

ESP8266WiFiMulti wifiMulti;      
WiFiUDP UDP;                     
IPAddress timeServerIP(192,53,103,108);       
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
  
  Serial.print("D6: ");
  Serial.println(D6);
  
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


// ------- Pins -------------
// a digital output pin
class Pin: public JsonMappable{
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
    }
    int readState(){
      return state;
    }
    void activate(){
      Serial.print("setup: pinMode(");
      Serial.print(pin);
      Serial.print(", OUTPUT)");
      pinMode(pin, OUTPUT);
      digitalWrite(pin, state);
    }
    void requestState( uint requestMe){
      requestedState = requestMe;
    }
    void commitRequestedState(){
      setState( requestedState );
    }
    virtual JsonObject toJson(JsonObject parent){
      parent["Pin_"] = pin;
      parent["State"]= state;
      return parent;
    };
};


class State: public JsonMappable {
  private:
    boolean autoMode=true; 
    Pin pins[4]= {Pin(D0, HIGH), Pin(D1,HIGH),Pin(D2, HIGH), Pin(D3,HIGH)};
  public:
    virtual JsonObject toJson(JsonObject parent){
      parent["autoMode"]=autoMode;
      JsonArray jsonpins = parent.createNestedArray("Pins");
      for (int i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
        JsonObject jsonpin = jsonpins.createNestedObject();
        jsonpin["Id"]=i;
        pins[i].toJson(jsonpin);
      }
      return parent;  //todo: does return make sense at all?
    };

    Pin *getPins(){
      return pins;
    }

    void setMode( boolean modePar){
      autoMode=modePar;
    }
    boolean isAutoMode(){
      return autoMode;
    }
    void debugPrint(){
      for (int i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
        Serial.print("Pin #");
        Serial.print(i);
        Serial.print(", state: ");
        Serial.println(pins[i].readState());
      }
    }
};
State state = State();
Pin *pins = state.getPins();

// ------- Conditions -------------
/**
 * Note: not yet used
 * 
 * Plan: each timer can be combined with condition
 * e.g. water on between 8:00 and 8:30 when condition humidity<20 holds
 * i.e. condition conbines sensor values.
 */
class Condition : public JsonMappable {
  protected:
    int triggerTemp=0;
    int triggerHumidity=0;
    virtual String type()=0;
  public:
    Condition( int temp=20, int humidity=50):triggerTemp(temp), triggerHumidity(humidity){}
    virtual bool check(uint temp, uint humidity) =0;
    virtual String description()=0;
    virtual void setTemp(int temp){
      triggerTemp = temp;
    };
    virtual void setHumidity(int humidity){
      triggerHumidity = humidity;
    };
    virtual JsonObject toJson(JsonObject parent){
      parent["type"]=type();
      parent["triggerTemp"] = triggerTemp;
      parent["triggerHumidity"] = triggerHumidity;
      parent["description"]=description();
      return parent;
    };
};

class ConditionAllwaysTrue : public Condition{
    private:
  protected:
    virtual String type(){ 
      return "ConditionAllwaysTrue";
    };
  public:
    virtual bool check(uint temp, uint humidity){
      return true;
    }
    virtual String description(){
      return "always true";
    }
};

class ConditionTempAndHumidity: public Condition{
    private:
  protected:
    virtual String type(){ 
      return "ConditionTempAndHumidity";
    };
  public:
    ConditionTempAndHumidity(int temp, int humidity){
      triggerTemp = temp;
      triggerHumidity = humidity;
    }
    virtual bool check(uint temp, uint humidity){
      Serial.printf(" temp(%ud)>triggerTemp(%d) && humidity(%ud)<triggerHumidity(%d)\n",temp, triggerTemp, humidity, triggerHumidity);
      return temp>triggerTemp && humidity<triggerHumidity;
    }
    virtual String description(){
      return "temp>triggerTemp && humidity<triggerHumidity";
    }
};

class ConditionHumidity: public Condition{
  protected:
    virtual String type(){ 
      return "ConditionHumidity";
    };
  public:
    ConditionHumidity(int humidity){
      triggerHumidity = humidity;
    }
    virtual bool check(uint temp, uint humidity){
      return humidity<triggerHumidity;
    }
    virtual String description(){
      return "humidity<triggerHumidity";
    }
};

class ConditionTemp: public Condition{
  protected:
    virtual String type(){ 
      return "ConditionTemp";
    };
  public:
    ConditionTemp(int temp){
      triggerTemp = temp;
    }
    virtual bool check(uint temp, uint humidity){
      return temp>triggerTemp;
    }
    virtual String description(){
      return "temp>triggerTemp";
    }
};

Condition *conditions[] = { new ConditionAllwaysTrue(),
                            new ConditionTempAndHumidity(15,50), 
                            new ConditionTempAndHumidity(20,50), 
                            new ConditionHumidity(45), 
                            new ConditionHumidity(50), 
                            new ConditionHumidity(55), 
                            new ConditionTemp(15),
                            new ConditionTemp(20),
                            new ConditionTemp(25)
                          };

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
    int last_temp;
    int last_humidity;
    int current_state;
    int active_now;

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

    virtual JsonObject toJson(JsonObject object){
      object["start_hour"] = start_hour;
      object["start_minute"] = start_minute;
      object["end_hour"] = end_hour;
      object["end_minute"] = end_minute;
      object["pin"] = pin;
      //object["lastTemp"] = last_temp;
      //object["lastHumidity"] = last_humidity;
      //object["activeNow"] = active_now;
      //object["currentState"] = current_state;
      //Condition *condition = getCondition();
      //condition->toJson(object); // return value not needed
      return object;
    };

    void set_humidity_temp_state_activeTime( int humidity, int temp, int state, int activeNow){
      last_humidity = humidity;
      last_temp = temp;
      current_state = state;
      active_now = activeNow;
    }
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
LinkedList<Timer*>* initTimersGewaechshaus(){
    LinkedList<Timer*>* timerList = new LinkedList<Timer*>();

    
    timerList->add(new Timer(4, 10, 4, 16, 0, conditions[0]));
    timerList->add(new Timer(4, 20, 4, 25, 1, conditions[0]));
    timerList->add(new Timer(4, 30, 4, 35, 2, conditions[0]));
    timerList->add(new Timer(4, 40, 4, 45, 3, conditions[0]));
    timerList->sort(compare); // order is only relevant for displaying timers
    return timerList;
}

LinkedList<Timer*>* initTimersVorgarten(){
    LinkedList<Timer*>* timerList = new LinkedList<Timer*>();

    timerList->add(new Timer(20, 0, 20, 10, 0, conditions[0]));
    timerList->add(new Timer(7, 0, 7, 10, 0, conditions[0]));
    timerList->sort(compare); // order is only relevant for displaying timers
    return timerList;
}

void addTimer( uint start_hour, uint start_minute, uint end_hour, uint end_minute, uint pin, uint condition_num){
  Timer* timer = new Timer(start_hour, start_minute, end_hour, end_minute, pin, conditions[condition_num]);
  timerList->add(timer);
  timerList->sort(compare);
}

void deleteTimer( uint index){
  Timer *removeMe = timerList->remove(index); // TODO: free Timer
}

/*****************************************************
 * REST call to query timers
 *****************************************************/

//StaticJsonDocument<2048> doc;
DynamicJsonDocument doc(10000);
void timers(){
    //DynamicJsonDocument doc(1024);
    
    JsonObject root = doc.to<JsonObject>();
    JsonArray timers = root.createNestedArray("timers");
    for (int i = 0; i < timerList->size(); i++) {
      Timer *t = timerList->get(i);
      JsonObject timer = timers.createNestedObject();
      t->toJson(timer);
    }
    String output;
    serializeJsonPretty(doc, output);
    Serial.println();
    serializeJsonPretty(doc, Serial);
    server.send(200,"text/json",output);

}


void get_conditions(){
    JsonObject root = doc.to<JsonObject>();
    JsonArray jsonconditions = root.createNestedArray("Conditions");
    for (int i = 0; i < sizeof(conditions)/sizeof(conditions[0]); i++) {
      Condition *c = conditions[i];
      JsonObject jsonConditions = jsonconditions.createNestedObject();
      jsonConditions["id"]=i;
      c->toJson(jsonConditions);
    }
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

    if( !state.isAutoMode()){
      Serial.println("manual Mode - timers deactivated");
      return;
    }
      

  int num_pins = 4;  //TODO
  Serial.printf("%d pin(s) configured\n", num_pins);

  for( uint act_pin = 0; act_pin<num_pins; act_pin++){
    Serial.print("setting pin ");
    Serial.print(act_pin);
    Serial.println();
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
      timer->set_humidity_temp_state_activeTime( 0, 0, 0, 0);
    } else if(timer->isAfterEndTime(actualHour, actualMinute) ){
      Serial.println(" already passed");
      timer->set_humidity_temp_state_activeTime( 0, 0, 0, 0);
    } else {
      Serial.println(" active");
      float humidity = getHumidity();
      float temperature = dht.readTemperature();
      if( condition->check(temperature, humidity)){
        timer->set_humidity_temp_state_activeTime( humidity, temperature, 1, 1);
        pin->requestState(LOW);
        Serial.print("Activate Pin #");
        Serial.println(timer->pin);
      } else {
        timer->set_humidity_temp_state_activeTime( humidity, temperature, 0, 1);
      }
    }   
  }
  for( uint act_pin = 0; act_pin<num_pins; act_pin++){
    Pin *pin=&pins[act_pin];
    pin->commitRequestedState();
  }
  state.debugPrint();
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

DynamicJsonDocument jsonBody(2048); // avoid mem leaks
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

void set_state(DynamicJsonDocument jsonBody) {
    boolean mode = jsonBody["AutoMode"];
    int pin0 = jsonBody["Pin0"];
    int pin1 = jsonBody["Pin1"];
    int pin2 = jsonBody["Pin2"];
    int pin3 = jsonBody["Pin3"];
    state.setMode(mode);
    pins=state.getPins();
    pins[0].setState(pin0);
    pins[1].setState(pin1);
    pins[2].setState(pin2);
    pins[3].setState(pin3); 
}

void post_put_state() {
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
 
    set_state(jsonBody);
    server.send(200,"text/plain","state set");

}

void set_condition(DynamicJsonDocument jsonBody) {
    int humidity = jsonBody["humidity"];
    int condition_id = jsonBody["condition"];
    int temp = jsonBody["temp"];
    Condition *condition = conditions[condition_id];
    condition->setHumidity(humidity);
    condition->setTemp(temp);
}

void post_put_condition() {
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
 
    set_condition(jsonBody);
    server.send(200,"text/plain","condition set");

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

void get_state() {
    JsonObject root = doc.to<JsonObject>();
    state.toJson(root);
    String output;
    serializeJsonPretty(doc, output);
    Serial.println();
    serializeJsonPretty(doc, Serial);
    server.send(200,"text/json",output);

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
  IPAddress gateway(192, 168, 1, 2);   
  IPAddress subnet(255, 255, 255, 0);  
  IPAddress dns(8, 8, 8, 8);  
  String vorgartenMac = "CC:50:E3:0A:1B:F2";
  String gewaechshausMac ="CC:50:E3:0A:1F:9E";
  //setVorgartenMacAddress();
  setGewaechshausMacAddress();
  String currentMac = WiFi.macAddress();
  if( currentMac.equals(vorgartenMac)){
    Serial.println("using config V O R G A R T E N");
    timerList = initTimersVorgarten();
    WiFi.hostname("esp-vorgarten"); 
    IPAddress staticIP(192, 168, 1, 141); 
    WiFi.config(staticIP, subnet, gateway, dns);
  } else if(  currentMac.equals(gewaechshausMac)){
    Serial.println("using config G E W A E C H S H A U S");
    timerList = initTimersGewaechshaus();
    WiFi.hostname("esp-gewaechshaus"); 
    IPAddress staticIP(192, 168, 1, 103);
    WiFi.config(staticIP, subnet, gateway, dns);
  } else
    Serial.println(" I N V A L I D   C O N F I G");
    
  Serial.print("MAC ADDRESS: ");
  Serial.print(currentMac); 
  Serial.println();
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


// -- setup and main loop

void setup() {
  
  Serial.begin(9600);          
  delay(10);
  Serial.println("\r\n");

  // init digital DHT11 sensor
  dht.begin();
  
  startWiFi();                   
  startUDP();

  /*if(!WiFi.hostByName(NTPServerName, timeServerIP)) { 
    Serial.println("DNS lookup failed. Rebooting.");
    Serial.flush();
    ESP.reset();
  }*/

  int num_pins = 4;  //TODO
  Serial.printf("%d pin(s) configured\n", num_pins);

  for( uint act_pin = 0; act_pin<num_pins; act_pin++){
    Pin *pin=&pins[act_pin];
    pin->activate();
  }
  
  // register handlers
  server.on("/timers", timers);
  server.on("/sensors", readSensors);
  server.on("/timer", HTTP_POST, post_put_timer);
  server.on("/timer", HTTP_PUT, post_put_timer);
  server.on("/timer", HTTP_DELETE, delete_timer);
  server.on("/state", HTTP_GET, get_state);
  server.on("/state", HTTP_POST, post_put_state);
  server.on("/state", HTTP_PUT, post_put_state);
  server.on("/condition", HTTP_PUT, post_put_condition);
  server.on("/condition", HTTP_GET, get_conditions);
 
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
