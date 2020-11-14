# Smart Garden 

## Overview

Smart Garden is a webvservice for ESP8266 to control your garden and/or
greenhouse. You can set **timers** that turn on or off a device 
based on a **condition**.

Conditions can be a combination of simple **predicates** combined with and (conjunctions).
A simple predicate is a comparison of a **sensor value** with a constant.

### Modes
The control has two modes:
- one mode to control all actors by directly setting them.
  This can be used to control all actors (irrigation, etc)
  manually.
- one mode where all actors are controlled automatically.
  This means that the system is controlled by a set of timers
  where each timer defines a timeslot, an ouput pin (=actor) and
  a condition that must match during this slot (e.g. temperature > 20)
  so that the corresponding actor is turned on.
  
### Sensors
The board supports 3 sensors, two of which are on the main board
(temperature and air humidity). A soil humidity sensor can be attached
externally.

### Conditions/Predicates
The following conditions are currently fix coded and indexed
starting with 0:
```
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
```
This might be work for future improvement...


# Web Service Interface
In the followig examples the ESP runs with IP address 192.168.1.155`
with 4 configured pins as start configuration.
## Examples 
TBD: not yet complete, list of commands is missing

### Query timers
`get http://192.168.1.155/timers

Example result:
```
  "timers": [
    {
      "start_hour": 4,
      "start_minute": 10,
      "end_hour": 4,
      "end_minute": 16,
      "pin": 0
    },
    {
      "start_hour": 4,
      "start_minute": 20,
      "end_hour": 4,
      "end_minute": 25,
      "pin": 1
    },
    {
      "start_hour": 4,
      "start_minute": 30,
      "end_hour": 4,
      "end_minute": 35,
      "pin": 2
    },
    {
      "start_hour": 4,
      "start_minute": 40,
      "end_hour": 4,
      "end_minute": 45,
      "pin": 3
    }
  ]
}
```
### Query states
`get http://192.168.1.155/state`
```{
  "autoMode": 1,
  "Pins": [
    {
      "Id": 0,
      "Pin_": 16,
      "State": 1
    },
    {
      "Id": 1,
      "Pin_": 5,
      "State": 1
    },
    {
      "Id": 2,
      "Pin_": 4,
      "State": 1
    },
    {
      "Id": 3,
      "Pin_": 0,
      "State": 1
    }
  ]
}
```
### Get conditions
`get 192.168.1.155/condition`
```{
  "Conditions": [
    {
      "id": 0,
      "type": "ConditionAllwaysTrue",
      "triggerTemp": 20,
      "triggerHumidity": 50,
      "description": "always true"
    },
    {
      "id": 1,
      "type": "ConditionTemp"
      "triggerTemp": 20,
      "description": "temp>triggerTemp && humidity<triggerHumidity"
    },
    {
      "id": 2,
      "type": "ConditionTempAndHumidity",
      "triggerTemp": 20,
      "triggerHumidity": 50,
      "description": "temp>triggerTemp && humidity<triggerHumidity"
    }
  ]
}
```
### Add timer
`post `

with the following json as request body will set a new timer that is triggered when condition 0 is true:
```{
	"startHour": 4,
	"startMinute":30,
	"endHour": 4,
	"endMinute":33,
	"pin":2,
	"condition":0
}
```
This setting turns on pin2. The condition AlwaysTrue says
that the pin is on (independent from humidity or temperature)

### Set state to manual
`post 192.168.1.155/state`
with following request body
```
{
    "AutoMode": false,
    "Pin0": 0,
    "Pin1": 0,
    "Pin2": 0,
    "Pin3": 0
}
```
sets all actors (relais) to on.

Note: 0=on, 1=off

### Set state to auto
`post 192.168.1.155/state`
with request body`
``` 
{
   "AutoMode": true
}
```
sets the mode to auto.

### Set condition

put 192.168.1.143/condition`

```
{
	"condition": 1,
	"temp": 24,
	"humidity":50
}
```

### Query Sensors
To query the sensors use

`get 192.168.1.155/sensors`

an example result is
```
{
  "humidity": 100,
  "air_humidity": 17,
  "temperature": 22
}
```
Not that in the example there is no soil humidity sensor attached.