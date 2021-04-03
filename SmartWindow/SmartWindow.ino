#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ErriezRotaryFullStep.h>

#define PARAM_OPEN "open_set"
#define PARAM_CLOSE "close_set"
#define PARAM_ANGLE "angle_set"
#define PARAM_AUTO "auto_set"
#define DEVICE_NAME_PARAM "device_name_set"
#define NETWORK_NAME_SET "network_name_set"
#define NETWORK_PASSWORD_SET "network_password_set"
#define PAUSE_TIME 250
#define CONNECT_TRIES 10
#define ANGLE_DIFFERENCE 10
#define MIN_ANGLE 30
#define TURN_STEP 10
#define DEVICE_NAME_OFFSET 1
#define OPEN_VALUE_OFFSET 21
#define CLOSE_VALUE_OFFSET 31
#define NETWORK_NAME_OFFSET 41
#define NETWORK_PASSWORD_OFFSET 61

#define SERVO_LEFT_PIN 5
#define SERVO_RIGHT_PIN 4
#define LIGHT_PIN A0
#define ENCODER_BUTTON 14
#define ENCODER_PIN1 12
#define ENCODER_PIN2 13

Servo servo_right, servo_left;
ESP8266WebServer server(3257);
RotaryFullStep rotary(ENCODER_PIN1, ENCODER_PIN2);

String pageStart = R"=====(<style>
          body{
             background: url(https://sun9-1.userapi.com/impg/hEAhUJ9G4XJuhY-OgqdDZWu8lZrw_7Re6v0kvA/y_lJSGTiOdE.jpg?size=2560x1640&quality=96&sign=f700f032283a93104c625cc76930362f&type=album) no-repeat fixed center; background-size: 100%;
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
    <meta http-equiv='refresh' content='20'>
    <title>Window controller</title>
  </head>
)====="; 

boolean autoTurn = true;
int openValue = 800;
int closeValue = 500;
String networkName = "", networkPassword = "";

void setup(void){
  Serial.begin(115200);
  openValue = readFromEEPROM(OPEN_VALUE_OFFSET).toInt();
  closeValue = readFromEEPROM(CLOSE_VALUE_OFFSET).toInt();
  
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN1), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN2), handleEncoder, CHANGE);

  pinMode(ENCODER_BUTTON, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT); 
  servo_right.attach(SERVO_RIGHT_PIN);
  servo_left.attach(SERVO_LEFT_PIN);
  
  delay(1000);

  connectToNetwork();

  server.on("/", [](){
    handleHomePage();
    server.send(200, "text/html", mainWebPage());
  });
  server.on("/json", [](){
    handleHomePage();
    server.send(200, "application/json", buildJson());
  });
  server.on("/close", [](){
    handleClosePage();
    server.sendHeader("Location", String("/"), true);
    server.send ( 302, "text/plain", "");
  });
  server.on("/json/close", [](){
    handleClosePage();
    server.send(200, "application/json", buildJson());
  });
  server.on("/open", [](){
    handleOpenPage();
    server.sendHeader("Location", String("/"), true);
    server.send ( 302, "text/plain", "");
  });
  server.on("/json/open", [](){
    handleOpenPage();
    server.send(200, "application/json", buildJson());
  });
   server.on("/middle", [](){
    handleMiddlePage();
    server.sendHeader("Location", String("/"), true);
    server.send ( 302, "text/plain", "");
  });
  server.on("/json/middle", [](){
    handleMiddlePage();
    server.send(200, "application/json", buildJson());
  });
  server.on("/restart", [](){
    WiFi.disconnect();
    ESP.restart();
  });
  
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");
  
  ArduinoOTA.begin();
}

void loop(void){
  ArduinoOTA.handle();
  delay(PAUSE_TIME);
  if(WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP){
    server.handleClient();
    delay(PAUSE_TIME);
  }else{
    connectToNetwork();
  }
  
  if(autoTurn){
    autoTurnServo();
  }else{
    delay(PAUSE_TIME);
  }

  if(digitalRead(ENCODER_BUTTON) == 0){
    autoTurn = true;
    blink(1);
  }
}

void connectToNetwork(){
  WiFi.disconnect();
  WiFi.hostname(readFromEEPROM(DEVICE_NAME_OFFSET));
  networkName = readFromEEPROM(NETWORK_NAME_OFFSET);
  networkPassword = readFromEEPROM(NETWORK_PASSWORD_OFFSET);
  if(networkName.length() > 0){
    int c = 0;
    WiFi.mode(WIFI_STA);
    WiFi.begin(networkName, networkPassword);
    Serial.print("Connecting to ");
    Serial.print(networkName);
    Serial.print(" with password ");
    Serial.println(networkPassword);
    while (c < CONNECT_TRIES && WiFi.status() != WL_CONNECTED) {
      blink(2);
      Serial.print(".");
      c++;
    }
    if(WiFi.status() == WL_CONNECTED){
      Serial.println("");
      Serial.println("Connected");
      Serial.print("IP address: "); 
      Serial.println(WiFi.localIP());
    }else{
      Serial.println("Can't connect");
      WiFi.disconnect();
      WiFi.hostname(readFromEEPROM(DEVICE_NAME_OFFSET));
      WiFi.mode(WIFI_AP);
      WiFi.softAP("SMART_WINDOW");
      Serial.print("Started ap with ip: ");
      Serial.println(WiFi.softAPIP());
    }
  }else{
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SMART_WINDOW");
    Serial.print("Started ap with ip: ");
    Serial.println(WiFi.localIP());
  }
  
}

#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
ICACHE_RAM_ATTR
#endif
void handleEncoder(){
  int val = rotary.read();
  if (val > 0) {
    turnLeft(servo_left.read() - 5);
    turnRight(servo_right.read() + 5);
    autoTurn = false;
  }else if(val < 0 ){
    turnLeft(servo_left.read() + 5);
    turnRight(servo_right.read() - 5);
    autoTurn = false;
  }
}

void handleHomePage(){
  if (server.arg(PARAM_OPEN) != "" && server.arg(PARAM_OPEN).length() < 8) {
      openValue = server.arg(PARAM_OPEN).toInt();
      writeToEEPROM(OPEN_VALUE_OFFSET, server.arg(PARAM_OPEN));
    }
    if (server.arg(PARAM_CLOSE) != "" && server.arg(PARAM_CLOSE).length() < 8) {
      closeValue = server.arg(PARAM_CLOSE).toInt();
      writeToEEPROM(CLOSE_VALUE_OFFSET, server.arg(PARAM_CLOSE));
    }
    if (server.arg(PARAM_ANGLE) != "") {
      int angle = server.arg(PARAM_ANGLE).toInt();
      if(angle > 0){
        autoTurn = false;
        turnLeft(angle);
        turnRight(180 - angle);
      }
    }
    if (server.arg(PARAM_AUTO) != "") {
      String val = server.arg(PARAM_AUTO);
      if(val.equals("on")){
        autoTurn = true;
      }else if(val.equals("off")){
        autoTurn = false;
      }
    }
    if(server.arg(DEVICE_NAME_PARAM ) != ""){
      String name = server.arg(DEVICE_NAME_PARAM);
      if(name.length() <= 20 && !name.equals(WiFi.hostname())){
        if(!name.startsWith("{w}")){
          name = "{w}" + name;
        }
        writeToEEPROM(DEVICE_NAME_OFFSET, name);
        WiFi.hostname(name);
      }
    }
    if (server.arg(NETWORK_NAME_SET) != "" && server.arg(NETWORK_NAME_SET).length() < 20) {
      networkName = server.arg(NETWORK_NAME_SET);
      writeToEEPROM(NETWORK_NAME_OFFSET, server.arg(NETWORK_NAME_SET));
    }
    if (server.arg(NETWORK_PASSWORD_SET) != "" && server.arg(NETWORK_PASSWORD_SET).length() < 20) {
      networkPassword = server.arg(NETWORK_PASSWORD_SET);
      writeToEEPROM(NETWORK_PASSWORD_OFFSET, server.arg(NETWORK_PASSWORD_SET));
    }
}

void handleMiddlePage(){
  autoTurn = false;
    if (server.arg("servo") != "") {
      String servName = server.arg("servo");
      if(servName.equals("left")){
        middle(true, false);
      }else if(servName.equals("right")){
        middle(false, true);
      }
    }else{
      middle(true, true);
    }
}

void handleClosePage(){
  autoTurn = false;
    if (server.arg("servo") != "") {
      String servName = server.arg("servo");
      if(servName.equals("left")){
        close(true, false);
      }else if(servName.equals("right")){
        close(false, true);
      }
    }else{
      close(true, true);
    }
}

void handleOpenPage(){
  autoTurn = false;
    if (server.arg("servo") != "") {
      String servName = server.arg("servo");
      if(servName.equals("left")){
        open(true, false);
      }else if(servName.equals("right")){
        open(false, true);
      }
    }else{
      open(true, true);
    }
}

void blink(int c){
  for(int i = 0; i < c; i++){
    delay(PAUSE_TIME);
    digitalWrite(LED_BUILTIN, LOW);
    delay(PAUSE_TIME);
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

void turnLeft(int d){
  if(servo_left.read() != d ) servo_left.write(d);
}
void turnRight(int d){
  if(servo_right.read() != d ) servo_right.write(d);
}

void open(boolean leftServ, boolean rightServ){
  if(leftServ) turnLeft(180 - MIN_ANGLE);
  if(rightServ) turnRight(MIN_ANGLE);
}

void middle(boolean leftServ, boolean rightServ){
  if(leftServ) turnLeft(90);
  if(rightServ) turnRight(90);
}

void close(boolean leftServ, boolean rightServ){
  if(leftServ) turnLeft(MIN_ANGLE);
  if(rightServ) turnRight(180 - MIN_ANGLE);
}

void autoTurnServo(){
  if(analogRead(LIGHT_PIN) < closeValue){
    close(true, true);
  }else if (analogRead(LIGHT_PIN) > openValue){
    open(true, true);
  }else{
    int angle = map(analogRead(LIGHT_PIN), closeValue, openValue, MIN_ANGLE, 180 - MIN_ANGLE);
    if(abs(180 - angle - servo_right.read()) > ANGLE_DIFFERENCE && abs(angle - servo_left.read()) > ANGLE_DIFFERENCE ){
      turnLeft(angle);
      turnRight(180 - angle);
    }
  }
}

String mainWebPage(){
  String webPage = "<html>";
  webPage += pageStart; 
  webPage += R"=====(<body>
    <center>
      <h1>Window controller</h1><p> Left angle = )=====";
  webPage += servo_left.read();
  webPage += "</p><p> Right angle = ";
  webPage += servo_right.read();
  webPage += R"=====(</p><div class = "block">
          <p>Open <a href="open?servo=left"><button class = "smoove">Left</button></a><a href="open?servo=right"><button class = "smoove">Right</button></a><a href="open"><button class = "smoove">Both</button></a></p>
          <p>Middle <a href="middle?servo=left"><button class = "smoove">Left</button></a><a href="middle?servo=right"><button class = "smoove">Right</button></a><a href="middle"><button class = "smoove">Both</button></a></p>
          <p>Close <a href="close?servo=left"><button class = "smoove">Left</button></a><a href="close?servo=right"><button class = "smoove">Right</button></a><a href="close"><button class = "smoove">Both</button></a></p>
          <form action="/" >Set angle [0, 180]: 
            <input type="text" name="angle_set" class = "smoove">
            <input type="submit" value="set" class = "smoove">
          </form>
         </div>)=====";
  if (autoTurn) {
    webPage += R"=====(<div class = "block"><p><font class = "ok">Auto turn is on </font></a>&nbsp;<a href="?auto_set=off"><button class = "alert">turn off</button></a></p>)=====";
  } else {
    webPage += R"=====(<div class = "block"><p><font class = "alert">Auto turn is off </font></a>&nbsp;<a href="?auto_set=on"><button class = "ok">turn on</button></a></p>)=====";
  }
  webPage += "<p> Light analog value = ";
  webPage +=  analogRead(LIGHT_PIN);
  webPage += "</p>";
  webPage += "<p>Open/close values : ";
  webPage += openValue;
  webPage += "/";
  webPage += closeValue;
  webPage += R"=====(<form action="/">Set open value (open if light greater):
             <input type="number" name="open_set" class = "smoove" maxlength = "8">
              <input type="submit" value="set" class = "smoove">
            </form>
          <form action="/" >Set close value (close if ligth less): 
            <input type="number" name="close_set" class = "smoove" maxlength = "8">
            <input type="submit" value="set" class = "smoove">
          </form></div>)=====";
  webPage += R"=====(<div class = "block">
          <form action="/">Set device name: 
            <input type="text" name="device_name_set" class = "smoove" maxlength = "20" value = ")=====";
  webPage += WiFi.hostname();
  webPage += R"=====(">
            <input type="submit" value="change" class = "smoove">
          </form></div>)=====";
  webPage += R"=====(<div class="block">
          <form action="/">Connect to network with name:
             <input type="name" name="network_name_set" class = "smoove" maxlength = "20" value = ")=====";
  webPage += networkName;
  webPage += R"=====("><input type="name" name="network_password_set" class = "smoove" maxlength = "20"value = ")=====";
  webPage += networkPassword;
  webPage += R"=====("><p><input type="submit" value="save" class = "smoove"></p>
            </form></div>)=====";
  webPage += R"=====(<p><a href="/restart"><button class = "alert">***RESTART***</button></a></p>)=====";
  webPage += "</center></body></html>";
  return webPage;
}

void handle_NotFound() {
  String apiPage = "<html>";
  apiPage += pageStart;
  apiPage += R"=====(<body><center><h1>Wrong address!</h1>
        <div class = "block"
          <p>Api:</p>
          <ul>
            <li>/open[?servo=[left|right]] - open left/right servo. Without paramter opens both</li>
            <li>/middle[?servo=[left|right]] - set at middle left/right servo. Without paramter set at middle both</li>
            <li>/close[?servo=[left|right]] - close left/right servo. Without paramter close both</li>
            <li>/auto[?val=[on|off]] - enable/disable auto angle set</li>
            <li>/?open_set={$val} - set {$val} as open value</li>
            <li>/?close_set={$val} - set {$val} as close value</li>
          </ul>
          </div></body></center></html>)=====";
  server.send(404, "text/html", apiPage);
}

String buildJson() {
  String json = "{\n\"servoLeft\":\"";
  json += servo_left.read();
  json += "\",\n\"servoRight\":\"";
  json += servo_right.read();
  json += "\",\n\"lightValue\":\"";
  json += analogRead(LIGHT_PIN);
  json += "\",\n\"closeValue\":\"";
  json += closeValue;
  json += "\",\n\"openValue\":\"";
  json += openValue;
  json += "\",\n\"autoTurn\":\"";
  if (autoTurn) {
    json += "true";
  } else {
    json += "false";
  }
  json += "\"\n,\"name\":\"";
  json += WiFi.hostname();
  json += "\"\n,\"networkName\":\"";
  json += networkName;
  json += "\",\"networkPassword\":\"";
  json += networkPassword;
  json += "\"}";
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
  for (int i = 0; i < newStrLen; i++) {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';
  EEPROM.commit();
  return data;
}
