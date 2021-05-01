#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ErriezRotaryFullStep.h>
#include <ArduinoJson.h>

#define PARAM_TYPE "type"
#define PARAM_OPEN "open"
#define PARAM_CLOSE "close"
#define PARAM_BRIGHT "bright"
#define PARAM_AUTO "auto_set"
#define PARAM_DEVICE_NAME "device_name"
#define PARAM_NETWORK_NAME "network_name"
#define PARAM_NETWORK_PASSWORD "network_password"
#define PARAM_ACTION "action"
#define PARAM_VALUE "value"

#define ACTION_OPEN_BRIGHT "action_bright"
#define ACTION_OPEN "action_open"
#define ACTION_CLOSE"action_close"
#define ACTION_MIDDLE "action_middle"
#define ACTION_AUTO_TURN "action_auto_turn"
#define ACTION_SAVE_SETTINGS "action_save"

#define PAUSE_TIME 250
#define CONNECT_TRIES 10
#define ANGLE_DIFFERENCE 10
#define DEVICE_NAME_OFFSET 1
#define OPEN_VALUE_OFFSET 21
#define CLOSE_VALUE_OFFSET 31
#define BRIGHT_VALUE_OFFSET 41
#define NETWORK_NAME_OFFSET 51
#define NETWORK_PASSWORD_OFFSET 71

#define OTA_PAUSE 500
#define HANDLE_PAUSE 200
#define AUTO_TURN_PAUSE 500
#define BUTTON_FROM_ENCODER_PAUSE 200

#define BRIGHT_ANGLE 180
#define OPEN_ANGLE 150
#define CLOSE_ANGLE 5
#define MIDDLE_ANGLE 90
#define TURN_MANUAL_ANGLE 5

#define SERVO_LEFT_PIN 4
#define SERVO_RIGHT_PIN 0
#define LIGHT_PIN A0
#define ENCODER_BUTTON 14
#define ENCODER_PIN1 12
#define ENCODER_PIN2 13

#define START_AP 1

Servo servo_right, servo_left;
ESP8266WebServer server(3257);
RotaryFullStep rotary(ENCODER_PIN1, ENCODER_PIN2);

String pageStart = R"=====(<style>
          .picture{
             background: url(https://sun9-1.userapi.com/impg/hEAhUJ9G4XJuhY-OgqdDZWu8lZrw_7Re6v0kvA/y_lJSGTiOdE.jpg?size=2560x1640&quality=96&sign=f700f032283a93104c625cc76930362f&type=album); background-repeat: no-repeat; background-size: auto;
          }
          .color{
            background: #22b6f5;
            background: -moz-linear-gradient(top, #22b6f5, #315463);
            background: -webkit-linear-gradient(top, #22b6f5, #315463);
            background: -o-linear-gradient(top, #22b6f5, #315463);
            background: -ms-linear-gradient(top, #22b6f5, #315463);
          }
         .smoove{
              margin: 5px;
              border-radius: 8px
          }
          .block{
              border: 1px solid #0b2cde;
              border-radius: 4px;
              background-color: rgba(150, 170, 195, 0.4);
              width: 350px;
          }
          .alert{
              margin: 5px;
              border-radius: 8px;
              border: 1px solid #eb1010;
              background-color: red;
          }
          .ok{
             margin: 5px;
             border-radius: 8px;
             border: 1px solid #eb1010;
             background-color: green;
          }
      </style>
  <head>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'/>
    <meta charset='utf-8'>
    <meta http-equiv='refresh' content='60'>
    <title>Window controller</title>
    <link rel='icon' href='https://psv4.userapi.com/c856428/u291961412/docs/d11/c98618fcbc08/clap.gif' type="image/gif">
  </head>
)====="; 

boolean autoTurn = true;
int openValue, closeValue, brightValue;
String networkName, networkPassword, deviceName;

void setup(void){
  Serial.begin(115200);
  
  networkName = readFromEEPROM(NETWORK_NAME_OFFSET);
  networkPassword = readFromEEPROM(NETWORK_PASSWORD_OFFSET);
  networkName.trim(); networkPassword.trim();
  openValue = readFromEEPROM(OPEN_VALUE_OFFSET).toInt();
  openValue = openValue == 0 ? 900 : openValue;
  closeValue = readFromEEPROM(CLOSE_VALUE_OFFSET).toInt();
  closeValue = closeValue == 0 ? 300 : closeValue;
  brightValue = readFromEEPROM(BRIGHT_VALUE_OFFSET).toInt();
  brightValue = brightValue == 0 ? 1024 : brightValue;
  deviceName = readFromEEPROM(DEVICE_NAME_OFFSET);
  deviceName.trim();

  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN1), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN2), handleEncoder, CHANGE);

  pinMode(ENCODER_BUTTON, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
   
  servo_right.attach(SERVO_RIGHT_PIN);
  servo_left.attach(SERVO_LEFT_PIN);

  if(!connectToNetwork()){
    while(true){
      blink(1, 500);
    }
  }

  server.on("/", [](){
    server.send(200, "text/html", mainWebPage());
  });

  server.on("/json", [](){
    server.send(200, "text/html", buildJson());
  });
  
  server.on("/smarthome", [](){
    if(server.hasArg("plain")){
      server.send (consumeSmartHome(server.arg("plain")) ? 200 : 500, "application/json",  buildJson());
    }else{
      server.send (400, "application/json",  buildJson());
    } 
  });
  
  server.on("/actions", [](){
    consumeAction(server.arg(PARAM_ACTION), server.arg(PARAM_VALUE));
    server.sendHeader("Location", String("/"), true);
    server.send ( 302, "text/html", "");
  });
  
  server.on("/settings", [](){
    for(int i = 0; i < server.args() - 1; i += 2){
      consumeSetting(server.arg(i), server.arg(i+1));
    }
    server.sendHeader("Location", String("/"), true);
    server.send ( 302, "text/html", "");
  });
  
  server.on("/light", [](){
    server.send(200, "text/html", String(analogRead(LIGHT_PIN)));  
  });
  
  server.on("/restart", [](){
    ESP.restart();
  });
  
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");
  
  ArduinoOTA.begin();
}

long lastOta, lastHandle, lastAuto, lastButton;

void loop(void){
  if(millis() - lastOta > OTA_PAUSE){
    ArduinoOTA.handle();
    lastOta = millis();
  }
  
  if(millis() - lastHandle > HANDLE_PAUSE){
    if(WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP){
      server.handleClient();
      lastHandle = millis();  
    }else if (START_AP){
      connectToNetwork();
    }
  }
  
  if(autoTurn && millis() - lastAuto > AUTO_TURN_PAUSE){
    autoTurnServo();
    lastAuto = millis();
  }

  if(millis() - lastButton > BUTTON_FROM_ENCODER_PAUSE && digitalRead(ENCODER_BUTTON) == 0){
    autoTurn = true;
    blink(1, 100);
    lastButton = millis();
  }
}

bool connectToNetwork(){
  if(WiFi.getMode() == WIFI_AP){
    WiFi.softAPdisconnect (true);
  }else{
    WiFi.disconnect();  
  }

  if(!networkName.isEmpty()){
    int c = 0;
    WiFi.mode(WIFI_STA);
    WiFi.hostname(deviceName);
    WiFi.begin(networkName, networkPassword);
    Serial.print("Connecting to ");
    Serial.print(networkName);
    Serial.print(" with password ");
    Serial.println(networkPassword);
    while (c < CONNECT_TRIES && WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      c++;
      blink(2, 100);
      delay(PAUSE_TIME);
    }
  }
  
  bool res = false;
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("");
    Serial.println("Connected");
    Serial.print("IP address: "); 
    Serial.println(WiFi.localIP());
    res = true;
  }else{
    Serial.println("Can't connect");
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    res = WiFi.softAP(deviceName.isEmpty() ? "MY_SMART_WINDOW" : deviceName);
    Serial.print("Started ap with ip: ");
    Serial.println(WiFi.softAPIP());
    blink(1, 500);
  }
  return res;
}

#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
ICACHE_RAM_ATTR
#endif
void handleEncoder(){
  int val = rotary.read();
  if (val > 0) {
    turnLeft(servo_left.read() - TURN_MANUAL_ANGLE);
    turnRight(servo_right.read() + TURN_MANUAL_ANGLE);
    autoTurn = false;
    lastButton = millis() + BUTTON_FROM_ENCODER_PAUSE;
  }else if(val < 0 ){
    turnLeft(servo_left.read() + TURN_MANUAL_ANGLE);
    turnRight(servo_right.read() - TURN_MANUAL_ANGLE);
    autoTurn = false;
    lastButton = millis() + BUTTON_FROM_ENCODER_PAUSE;
  }
}

bool consumeSmartHome(String json){
  Serial.println(json);
  StaticJsonDocument<512> parsedJson;
  DeserializationError error = deserializeJson(parsedJson, json);
  if(error){
    Serial.println(error.f_str());
    return false;
  }

  JsonArray arrayActions = parsedJson["actions"].as<JsonArray>();
  for(int i = 0; i < arrayActions.size(); i++){
    String action = arrayActions[i][PARAM_ACTION];
    if(!action.equals(ACTION_SAVE_SETTINGS)){
      if(!consumeAction(action, arrayActions[i][PARAM_VALUE]))
        return false;
    }else if(action.length() > 0){
      if(!consumeSetting(arrayActions[i][PARAM_TYPE], arrayActions[i][PARAM_VALUE]))
        return false;
    }else{
      break;
    }
  }
  return true;
}


bool consumeSetting(String paramType, String value){
  Serial.print("Changing setting :: ");
  Serial.print(paramType);
  Serial.print("::");
  Serial.println(value);
  if(paramType.equals(PARAM_OPEN)){
    openValue = value.toInt();
    writeToEEPROM(OPEN_VALUE_OFFSET, value);
  }else if(paramType.equals(PARAM_CLOSE)){
    closeValue = value.toInt();
    writeToEEPROM(CLOSE_VALUE_OFFSET, value);
  }else if(paramType.equals(PARAM_BRIGHT)){
    brightValue = value.toInt();
    writeToEEPROM(BRIGHT_VALUE_OFFSET, value);
  }else if(paramType.equals(PARAM_DEVICE_NAME)){
    value.toUpperCase();
    if(value.length() <= 20 && !value.equals(deviceName)){
      deviceName = value;
      writeToEEPROM(DEVICE_NAME_OFFSET, deviceName);
      WiFi.hostname(deviceName);
    }
  }else if(paramType.equals(PARAM_NETWORK_NAME)){
    value.trim();
    if(value.length() <= 20 && !value.equals(networkName)){
      networkName = value;
      writeToEEPROM(NETWORK_NAME_OFFSET, value);
    }
  }else if(paramType.equals(PARAM_NETWORK_PASSWORD)){
    value.trim();
    if(value.length() <= 20 && !value.equals(networkPassword)){
      networkPassword = value;
      writeToEEPROM(NETWORK_PASSWORD_OFFSET, value);
    }
  }else{
    return false;
  }
  return true;
}

bool consumeAction(String action, String value){
  if(action.length() > 0){
    Serial.print("Action :: ");
    Serial.print(action);
    Serial.print("::");
    Serial.println(value);
    if(action.equals(ACTION_AUTO_TURN)){
      autoTurn = value.equals("on");
      return true;
    } else if(value.toInt() > 0 && value.toInt() < 4){
      return servosAtion(action, value.toInt());
    }
  }
  return false;
}

bool servosAtion(String action, int servo){
  autoTurn = false;
  if(action.equals(ACTION_OPEN_BRIGHT)){
    openFull(servo);
  }else if(action.equals(ACTION_OPEN)){
    open(servo);
  }else if(action.equals(ACTION_MIDDLE)){
    middle(servo);
  }else if(action.equals(ACTION_CLOSE)){
    close(servo);
  }else{
    autoTurn = true;
    return false;
  }
  return true;
}

void blink(int c, int pauseTime){
  for(int i = 0; i < c; i++){
    delay(pauseTime);
    digitalWrite(LED_BUILTIN, LOW);
    delay(pauseTime);
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

void turnLeft(int d){
  if(servo_left.read() != d ) servo_left.write(d);
}
void turnRight(int d){
  if(servo_right.read() != d ) servo_right.write(d);
}

void openFull(int servo){
  switch(servo){
    case 1:
      turnLeft(BRIGHT_ANGLE);
      break;
    case 2:
      turnRight(180 - BRIGHT_ANGLE);
      break;
    case 3:
      turnLeft(BRIGHT_ANGLE);
      turnRight(180 - BRIGHT_ANGLE);
      break;
  }
}

void open(int servo){
  switch(servo){
    case 1:
      turnLeft(OPEN_ANGLE);
      break;
    case 2:
      turnRight(180 - OPEN_ANGLE);
      break;
    case 3:
      turnLeft(OPEN_ANGLE);
      turnRight(180 - OPEN_ANGLE);
      break;
  }
}

void middle(int servo){
  switch(servo){
    case 1:
      turnLeft(MIDDLE_ANGLE);
      break;
    case 2:
      turnRight(180 - MIDDLE_ANGLE);
      break;
    case 3:
      turnLeft(MIDDLE_ANGLE);
      turnRight(180 - MIDDLE_ANGLE);
      break;
  }
}

void close(int servo){
  switch(servo){
    case 1:
      turnLeft(CLOSE_ANGLE);
      break;
    case 2:
      turnRight(180 - CLOSE_ANGLE);
      break;
    case 3:
      turnLeft(CLOSE_ANGLE);
      turnRight(180 - CLOSE_ANGLE);
      break;
  }
}

void autoTurnServo(){
  int lightVal = analogRead(LIGHT_PIN);
  if(lightVal < closeValue){
    close(3);
  }else if (lightVal > openValue){
    if(brightValue != 0 && brightValue > openValue){
      if(lightVal >= brightValue){
        openFull(3);
      }else{
        int angle = map(lightVal, openValue, brightValue, OPEN_ANGLE, BRIGHT_ANGLE);
        if(abs(180 - angle - servo_right.read()) > ANGLE_DIFFERENCE && abs(angle - servo_left.read()) > ANGLE_DIFFERENCE ){
          turnLeft(angle);
          turnRight(180 - angle);
        }
      }
    }else{
      open(3);
    }
  }else{
    int angle = map(lightVal, closeValue, openValue, CLOSE_ANGLE, OPEN_ANGLE);
    if(abs(180 - angle - servo_right.read()) > ANGLE_DIFFERENCE && abs(angle - servo_left.read()) > ANGLE_DIFFERENCE ){
      turnLeft(angle);
      turnRight(180 - angle);
    }
  }
}

String mainWebPage(){
  String webPage = "<html>";
  webPage += pageStart; 
  if(WiFi.getMode() == WIFI_AP){
    webPage += "<body class = \"color\">";
  }else{
    webPage += "<body class = \"picture\">";
  }
  webPage += "<center><h1>Window";
  webPage += deviceName.isEmpty() ?  "" : " :: ";
  webPage += deviceName;
  webPage += "</h1><p> left | right angle </p><p>";
  webPage += servo_left.read();
  webPage += " | ";
  webPage += servo_right.read();
  webPage += R"=====(</p><div class = "block">
            <p><p>Bright</p><a href="actions?action=action_bright&value=1"><button class = "smoove">Left</button></a><a href="actions?action=action_bright&value=2"><button class = "smoove">Right</button></a><a href="actions?action=action_bright&value=3"><button class = "smoove">Both</button></a></p>
            <p><p>Open</p><a href="actions?action=action_open&value=1"><button class = "smoove">Left</button></a><a href="actions?action=action_open&value=2"><button class = "smoove">Right</button></a><a href="actions?action=action_open&value=3"><button class = "smoove">Both</button></a></p>
            <p><p>Middle</p><a href="actions?action=action_middle&value=1"><button class = "smoove">Left</button></a><a href="actions?action=action_middle&value=2"><button class = "smoove">Right</button></a><a href="actions?action=action_middle&value=3"><button class = "smoove">Both</button></a></p>
            <p><p>Close</p><a href="actions?action=action_close&value=1"><button class = "smoove">Left</button></a><a href="actions?action=action_close&value=2"><button class = "smoove">Right</button></a><a href="actions?action=action_close&value=3"><button class = "smoove">Both</button></a></p>
           </div>)=====";
  if (autoTurn) {
    webPage += R"=====(<div class = "block"><p><font class = "ok">Auto turn is on </font></a>&nbsp;<a href="actions?action=action_auto_turn&value=off"><button class = "alert">turn off</button></a></p>)=====";
  } else {
    webPage += R"=====(<div class = "block"><p><font class = "alert">Auto turn is off </font></a>&nbsp;<a href="actions?action=action_auto_turn&value=on"><button class = "ok">turn on</button></a></p>)=====";
  }
  webPage += "<p> Light analog value = ";
  webPage +=  analogRead(LIGHT_PIN);
  webPage += R"=====(</p>
              <form action="/settings" ><p>Seted bright/open/close light values:</p>               
                <p><input type="hidden" name="type" class = "smoove" value = "bright"></p>
                <p>bright<input type="number" name="value" class = "smoove" value = ")=====";
  webPage += brightValue;
  webPage += R"=====(" maxlength = "8"></p>
                <p><input type="hidden" name="type" class = "smoove" value = "open"></p>
                <p>open<input type="number" name="value" class = "smoove" value = ")=====";
  webPage += openValue;
  webPage += R"=====(" maxlength = "8"></p>
                <p><input type="hidden" name="type" class = "smoove" value = "close"></p>
                <p>close<input type="number" name="value" class = "smoove" value = ")=====";
  webPage += closeValue;
  webPage += R"=====(" maxlength = "8"></p>
                <p><input type="submit" value="save" class = "smoove"></p>
              </form>
              </div>)=====";
  webPage += R"=====(<div class = "block">
              <form action="/settings">Set device name (it should contain only letters): 
                <p><input type="hidden" name="type" class = "smoove" value = "device_name"></p>
                <input type="text" name="value" class = "smoove" maxlength = "20" value = ")=====";
  webPage += deviceName;
  webPage += R"=====(">
                <input type="submit" value="save" class = "smoove">
              </form></div>)=====";
  webPage += R"=====(<div class="block">
              <form action="/settings">Connect to network with name/password:
                 <p><input type="hidden" name="type" class = "smoove" value = "network_name"></p>
                 <input type="name" name="value" class = "smoove" maxlength = "20" value = ")=====";
  webPage += networkName;
  webPage += R"=====(">
                <p><input type="hidden" name="type" class = "smoove" value = "network_password"></p>
                <input type="name" name="value" class = "smoove" maxlength = "20"value = ")=====";
  webPage += networkPassword;
  webPage += R"=====("><p><input type="submit" value="save" class = "smoove"></p>
                </form></div>)=====";
  webPage += R"=====(<p><a href="/restart"><button class = "alert">***RESTART***</button></a></p>)=====";
  webPage += "</center></body></html>";
return webPage;
}

void handle_NotFound() {
  server.send(404, "text/html", "<html><body><center><p><h2>404</h2></p><p><h1>B R U H</h1></p></center></body></html>");
}

String buildJson() {
  StaticJsonDocument<512> response;
  response["servoLeft"]       = servo_left.read();
  response["servoRight"]      = servo_right.read();
  response["lightValue"]      = analogRead(LIGHT_PIN);
  response["closeValue"]      = closeValue;
  response["openValue"]       = openValue;
  response["brightValue"]     = brightValue;
  response["autoTurn"]        = autoTurn;
  response["name"]            = deviceName.startsWith("{w}") ? deviceName : "{w}" + deviceName;
  response["networkName"]     = networkName;
  response["networkPassword"] = networkPassword;

  String json;
  serializeJsonPretty(response, json);
  return json;
}

void writeToEEPROM(int addrOffset, const String &strToWrite) {
  byte len = strToWrite.length();
  EEPROM.begin(4096);
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
  EEPROM.commit();
}

String readFromEEPROM(int addrOffset) {
  EEPROM.begin(4096);
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  int val = 0;
  for (int i = 0; i < newStrLen; i++) {
    val = EEPROM.read(addrOffset + 1 + i);
    data[i] = isAscii(val) ? val : ' ';
  }
  data[newStrLen] = '\0';
  EEPROM.commit();
  return data;
}
