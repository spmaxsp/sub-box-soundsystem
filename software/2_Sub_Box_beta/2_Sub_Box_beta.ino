#include <EEPROM.h>
#include <SPIFFS.h>
#include <Arduino.h>

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <ArduinoJson.h>

#include <Wire.h>
#include <SigmaDSP.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <AiEsp32RotaryEncoder.h>

#include "SigmaDSP_parameters.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

#define TOP_MUTE_PIN 16
#define SUB_MUTE_PIN 17

#define POWER_UP_PIN 12
#define POWER_OK_PIN 14

#define ROTARY_ENCODER_A_PIN 32
#define ROTARY_ENCODER_B_PIN 35
#define ROTARY_ENCODER_BUTTON_PIN 34
#define ROTARY_ENCODER_BUTTON_PIN_FAKE 18
#define ROTARY_ENCODER_STEPS 4

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, ROTARY_ENCODER_BUTTON_PIN_FAKE, -1, ROTARY_ENCODER_STEPS);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

SigmaDSP dsp(Wire, DSP_I2C_ADDRESS, 48000.00f /*,12*/);

AsyncWebServer server(80);

int display_state = -2; //-1: nothing -2:status(mute) 0:master-vol 1:bt-vol 2:aux-vol 3:sub-vol
int value_selected = 0;
unsigned long int last_millis = 0;
int mute = 0;
String options[] = {"Master Vol.", "BT. Vol.", "AUX Vol.", "Sub Vol."};
struct AudioParam {
  int eprom_valid;
  int bt_vol;
  int aux_vol;
  int main_vol;
  int EQband1;
  int EQband2;
  int EQband3;
  int EQband4;
  int EQband5;
  int EQband6;
  int EQband7;
  int sub_cutoff;
  int sub_vol;
};

AudioParam parameter_set;

const int capacity = JSON_OBJECT_SIZE(13);
StaticJsonDocument<capacity> json_doc;

int eeAddress = 0;

compressor compressor1;
compressor compressor2;
compressor compressor3;

firstOrderEQ eq1;

secondOrderEQ eqBand1;
secondOrderEQ eqBand2;
secondOrderEQ eqBand3;
secondOrderEQ eqBand4;
secondOrderEQ eqBand5;
secondOrderEQ eqBand6;
secondOrderEQ eqBand7;

void setup()
{
  pinMode(TOP_MUTE_PIN, OUTPUT); 
  pinMode(SUB_MUTE_PIN, OUTPUT);

  pinMode(POWER_UP_PIN, OUTPUT);
  pinMode(POWER_OK_PIN, INPUT_PULLDOWN);
  pinMode(ROTARY_ENCODER_BUTTON_PIN, INPUT);

  digitalWrite(ROTARY_ENCODER_BUTTON_PIN_FAKE, HIGH);

  rotaryEncoder.begin();
  rotaryEncoder.setup(
    [] { rotaryEncoder.readEncoder_ISR(); },
    [] { rotary_onButtonClick(); });
  rotaryEncoder.setAcceleration(0);
  
  Serial.begin(9600);
  Serial.println(F(">Booting Up....\n"));

  Serial.println(">Starting SSD1306");
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(">SSD1306 allocation failed");
    for(;;);
  }
  display.display();
  delay(1000);
  display.clearDisplay();
  display.display();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Sub Box 2.1 (beta)");

  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.println("Booting...");

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.print("WELCOME");

  display.display();
  delay(2000);

  Serial.println(">Initialize SPIFFS");
  if(!SPIFFS.begin(true)){
    Serial.println(">An Error has occurred while mounting SPIFFS");
    return;
  }

  Serial.println(F(">Reading EEPROM..."));
  EEPROM.begin(4000);
  EEPROM.get( eeAddress, parameter_set);

  if (parameter_set.eprom_valid == -1282470013) {
    Serial.println(F(">EEPROM valid... "));
  }
  else {
    Serial.println(F(">EEPROM invalid... (using default)"));
    parameter_set = {-1282470013, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0 };
  }

  Serial.println(F(">Starting Communication to DSP...: "));
  Wire.begin();
  dsp.begin();

  delay(2000);

  Serial.println(F(">Pinging i2c bus... (0: deveice is present, 2: device is not present"));
  Serial.print(F(">DSP response: "));
  Serial.println(dsp.ping());

  Serial.println(F(">Loading Programm to DSP...: "));
  loadProgram(dsp);

  Serial.println(F(">Open AP...: "));
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Audio Setup", "0123456789");
  IPAddress myIP = WiFi.softAPIP();
  Serial.print(">AP IP address: ");
  Serial.println(myIP);
  
  Serial.println(F(">Booting up Webserver...: "));
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
    json_doc["bt_vol"] = parameter_set.bt_vol;
    json_doc["aux_vol"] = parameter_set.aux_vol;
    json_doc["main_vol"] = parameter_set.main_vol;
    json_doc["EQband1"] = parameter_set.EQband1;
    json_doc["EQband2"] = parameter_set.EQband2;
    json_doc["EQband3"] = parameter_set.EQband3;
    json_doc["EQband4"] = parameter_set.EQband4;
    json_doc["EQband5"] = parameter_set.EQband5;
    json_doc["EQband6"] = parameter_set.EQband6;
    json_doc["EQband7"] = parameter_set.EQband7;
    json_doc["sub_cutoff"] = parameter_set.sub_cutoff;
    json_doc["sub_vol"] = parameter_set.sub_vol;
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(json_doc, *response);
    request->send(response);
  });
  
  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
    int paramsNr = request->params();
    for (int i = 0; i < paramsNr; i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p->name() == "bt_vol") {
        parameter_set.bt_vol = p->value().toInt();
        Serial.print(">setting bt_vol to ");
        Serial.println(p->value());
      }
      else if (p->name() == "aux_vol") {
        parameter_set.aux_vol = p->value().toInt();
        Serial.print(">setting aux_vol to ");
        Serial.println(p->value());
      }
      else if (p->name() == "main_vol") {
        parameter_set.main_vol = p->value().toInt();
        Serial.print(">setting main_vol to ");
        Serial.println(p->value());
      }
      else if (p->name() == "EQband1") {
        parameter_set.EQband1 = p->value().toInt();
        Serial.print(">setting EQband1 to ");
        Serial.println(p->value());
      }
      else if (p->name() == "EQband2") {
        parameter_set.EQband2 = p->value().toInt();
        Serial.print(">setting EQband2 to ");
        Serial.println(p->value());
      }
      else if (p->name() == "EQband3") {
        parameter_set.EQband3 = p->value().toInt();
        Serial.print(">setting EQband3 to ");
        Serial.println(p->value());
      }
      else if (p->name() == "EQband4") {
        parameter_set.EQband4 = p->value().toInt();
        Serial.print(">setting EQband4 to ");
        Serial.println(p->value());
      }
      else if (p->name() == "EQband5") {
        parameter_set.EQband5 = p->value().toInt();
        Serial.print(">setting EQband5 to ");
        Serial.println(p->value());
      }
      else if (p->name() == "EQband6") {
        parameter_set.EQband6 = p->value().toInt();
        Serial.print(">setting EQband6 to ");
        Serial.println(p->value());
      }
      else if (p->name() == "EQband7") {
        parameter_set.EQband7 = p->value().toInt();
        Serial.print(">setting EQband7 to ");
        Serial.println(p->value());
      }
      else if (p->name() == "sub_cutoff") {
        parameter_set.sub_cutoff = p->value().toInt();
        Serial.print(">setting sub_cutoff to ");
        Serial.println(p->value());
      }
      else if (p->name() == "sub_vol") {
        parameter_set.sub_vol = p->value().toInt();
        Serial.print(">setting sub_vol to ");
        Serial.println(p->value());
      }
    }
    request->send(200, "text/txt", "ok");
    configigureDSP();
  });

  server.on("/eeprom", HTTP_GET, [](AsyncWebServerRequest *request){
    EEPROM.put(eeAddress, parameter_set);
    if (EEPROM.commit()) {
        Serial.println(">EEPROM successfully committed !!!");
      } else {
        Serial.println(">ERROR! EEPROM commit failed");
      }
    request->send(200, "text/txt", "ok");
  });
  
  server.begin(); 

  configigureDSP();

  Serial.println(">MUTING all aps. ...");
  digitalWrite(TOP_MUTE_PIN, HIGH); 
  digitalWrite(SUB_MUTE_PIN, HIGH);

  Serial.println(">POWER UP");
  digitalWrite(POWER_UP_PIN, HIGH);

  Serial.print(">waiting for power-controll-module...");
  while(!digitalRead(POWER_OK_PIN)){
    delay(500);
  }
  
  Serial.println("  OK");
  Serial.println(">UNMUTING all aps. ...");
  digitalWrite(TOP_MUTE_PIN, LOW); 
  digitalWrite(SUB_MUTE_PIN, LOW);
  
  Serial.println("> ---SETUP FINISH!!!---");
  last_millis = millis();
  setForOption();
}

void configigureDSP(){

  Serial.println(">Reconfiguring DSP...");

  //Volume BT
  dsp.volume_slew(MOD_SWVOL3_ALG0_TARGET_ADDR, parameter_set.bt_vol);

  //Volume AUX
  dsp.volume_slew(MOD_SWVOL1_ALG0_TARGET_ADDR, parameter_set.aux_vol);

  //Volume Main
  dsp.volume_slew(MOD_SWVOL2_ALG0_TARGET_ADDR, parameter_set.main_vol);

  //Volume Sub
  dsp.volume_slew(MOD_SWVOL4_ALG0_TARGET_ADDR, parameter_set.sub_vol);

  //EQ band 1
  eqBand1.filterType = parameters::filterType::lowShelf;
  eqBand1.S = 1;
  eqBand1.Q = 0.7071;
  eqBand1.freq = 50;
  eqBand1.boost = parameter_set.EQband1;
  eqBand1.state = parameters::state::on;
  dsp.EQsecondOrder(MOD_MIDEQ1_ALG0_STAGE0_B0_ADDR, eqBand1);

  //EQ band 2
  eqBand2.filterType = parameters::filterType::peaking;
  eqBand2.Q = 1.41;
  eqBand2.freq = 120;
  eqBand2.boost = parameter_set.EQband2;
  eqBand2.state = parameters::state::on;
  dsp.EQsecondOrder(MOD_MIDEQ1_ALG0_STAGE1_B0_ADDR, eqBand2);

  //EQ band 3
  eqBand3.filterType = parameters::filterType::peaking;
  eqBand3.Q = 1.41;
  eqBand3.freq = 500;
  eqBand3.boost = parameter_set.EQband3;
  eqBand3.state = parameters::state::on;
  dsp.EQsecondOrder(MOD_MIDEQ1_ALG0_STAGE2_B0_ADDR, eqBand3);

  //EQ band 4
  eqBand4.filterType = parameters::filterType::peaking;
  eqBand4.Q = 1.41;
  eqBand4.freq = 1000;
  eqBand4.boost = parameter_set.EQband4;
  eqBand4.state = parameters::state::on;
  dsp.EQsecondOrder(MOD_MIDEQ1_ALG0_STAGE3_B0_ADDR, eqBand4);

  //EQ band 5
  eqBand5.filterType = parameters::filterType::peaking;
  eqBand5.Q = 1.41;
  eqBand5.freq = 2000;
  eqBand5.boost = parameter_set.EQband5;
  eqBand5.state = parameters::state::on;
  dsp.EQsecondOrder(MOD_MIDEQ1_ALG0_STAGE4_B0_ADDR, eqBand5);

  //EQ band 6
  eqBand6.filterType = parameters::filterType::peaking;
  eqBand6.Q = 1.41;
  eqBand6.freq = 10000;
  eqBand6.boost = parameter_set.EQband6;
  eqBand6.state = parameters::state::on;
  dsp.EQsecondOrder(MOD_MIDEQ1_ALG0_STAGE5_B0_ADDR, eqBand6);

  //EQ band 7
  eqBand7.filterType = parameters::filterType::lowShelf;
  eqBand7.S = 1;
  eqBand7.Q = 1.7071;
  eqBand7.freq = 20000;
  eqBand7.boost = parameter_set.EQband7;
  eqBand7.state = parameters::state::on;
  dsp.EQsecondOrder(MOD_MIDEQ1_ALG0_STAGE6_B0_ADDR, eqBand7);

  //Sub EQ cutoff
  eq1.filterType = parameters::filterType::lowpass;
  eq1.freq = parameter_set.sub_cutoff;
  eq1.gain = 6;
  eq1.state = parameters::state::on; 
  dsp.EQfirstOrder(MOD_GEN1STORDER1_ALG0_PARAMB00_ADDR, eq1);

  //Compressor 1  
  /*
  compressor1.threshold = 0.0; // -90 / +6 [dB]
  compressor1.ratio     = 4; // 1 - 100
  compressor1.attack    = 200; // 1 - 500 [ms]
  compressor1.hold      = 200; // 0 - 500 [ms]
  compressor1.decayMs   = 200; // 0 - 2000 [ms]
  compressor1.postgain  = 24; // -30 / +24 [dB]
  dsp.compressorRMS(MOD_LIMITER1_ALG0_S1_ADDR, compressor1);
  */

  //Compressor 2  
  /*
  compressor2.threshold = 0.0; // -90 / +6 [dB]
  compressor2.ratio     = 4; // 1 - 100
  compressor2.attack    = 200; // 1 - 500 [ms]
  compressor2.hold      = 200; // 0 - 500 [ms]
  compressor2.decayMs   = 200; // 0 - 2000 [ms]
  compressor2.postgain  = 24; // -30 / +24 [dB]
  dsp.compressorRMS(MOD_LIMITER1_ALG0_S1_ADDR, compressor2);
  */

  //Compressor 3  
  /*
  compressor3.threshold = 0.0; // -90 / +6 [dB]
  compressor3.ratio     = 4; // 1 - 100
  compressor3.attack    = 200; // 1 - 500 [ms]
  compressor3.hold      = 200; // 0 - 500 [ms]
  compressor3.decayMs   = 200; // 0 - 2000 [ms]
  compressor3.postgain  = 24; // -30 / +24 [dB]
  dsp.compressorRMS(MOD_LIMITER1_ALG0_S1_ADDR, compressor3);
  */

  if (mute == 1){
    Serial.println(">Muting...");
    digitalWrite(TOP_MUTE_PIN, HIGH); 
    digitalWrite(SUB_MUTE_PIN, HIGH);
  }
  else{
    Serial.println(">Unmuting...");
    digitalWrite(TOP_MUTE_PIN, LOW); 
    digitalWrite(SUB_MUTE_PIN, LOW);
  }
}

void setForOption(){
  Serial.println(">Reconfiguring Encoder...");
  if (value_selected == 1){
    switch (display_state){
    case 0:
        rotaryEncoder.setBoundaries(-50, 6, false);
        rotaryEncoder.setEncoderValue(parameter_set.main_vol);
        break;
    case 1:
        rotaryEncoder.setBoundaries(-50, 6, false);
        rotaryEncoder.setEncoderValue(parameter_set.bt_vol);
        break;
    case 2:
        rotaryEncoder.setBoundaries(-50, 6, false);
        rotaryEncoder.setEncoderValue(parameter_set.aux_vol);
        break;
    case 3:
        rotaryEncoder.setBoundaries(-50, 6, false);
        rotaryEncoder.setEncoderValue(parameter_set.sub_vol);
        break;
    default:
        break;
    }
  }
  else {
    rotaryEncoder.setBoundaries(0, 3, true);
    rotaryEncoder.setEncoderValue(0);
  }
  dispaly_update();
}

void rotary_onButtonClick() //-1: nothing -2:status(mute) 0:master-vol 1:bt-vol 2:aux-vol 3:sub-vol
{
  Serial.println(">Encoder Button Click");

  last_millis = millis();
  if (display_state == -1) {
    display_state = -2;
  }
  else if (display_state == -2) {
    if (mute == 0){ mute = 1; }
    else {          mute = 0; }
    configigureDSP();
  }
  else if (display_state >= 0) {
    if (value_selected == 0){ value_selected = 1; }
    else {                    value_selected = 0; }
  }
  setForOption();
}

void change_value(){
  Serial.println(">Encoder changing Value");

  last_millis = millis();
  if (value_selected == 1){
    switch (display_state){
    case 0:
        parameter_set.main_vol = rotaryEncoder.readEncoder();
        break;
    case 1:
        parameter_set.bt_vol = rotaryEncoder.readEncoder();
        break;
    case 2:
        parameter_set.aux_vol = rotaryEncoder.readEncoder();
        break;
    case 3:
        parameter_set.sub_vol = rotaryEncoder.readEncoder();
        break;
    default:
        break;
    }
    configigureDSP();
  }
  else {
    display_state = rotaryEncoder.readEncoder();
  }
  dispaly_update();
}

void dispaly_update(){
  display.clearDisplay();
  display.setCursor(0,0);
  
  switch (display_state){
    case -2:
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.println("Sub Box 2.1 (beta)");
    
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.println("ready...");
    
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      display.print("IP:");
    
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.print(WiFi.softAPIP());
      break;
    case -1:
      break;
    case 0:
      display.setTextSize(1);
      if (value_selected == 0){ display.setTextColor(SSD1306_WHITE); }
      else {                    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); }
      display.println("master-vol:");
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      display.println(parameter_set.main_vol);
      break;
    case 1:
      display.setTextSize(1);
      if (value_selected == 0){ display.setTextColor(SSD1306_WHITE); }
      else {                    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); }
      display.println("bt-vol:");
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      display.println(parameter_set.bt_vol);
      break;
    case 2:
      display.setTextSize(1);
      if (value_selected == 0){ display.setTextColor(SSD1306_WHITE); }
      else {                    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); }
      display.println("aux-vol:");
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      display.println(parameter_set.aux_vol);
      break;
    case 3:
      display.setTextSize(1);
      if (value_selected == 0){ display.setTextColor(SSD1306_WHITE); }
      else {                    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); }
      display.println("sub-vol:");
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      display.println(parameter_set.sub_vol);
      break;
  }
  
  Serial.print(">Updating Display to ");
  Serial.println(display_state);
  display.display();
  delay(100);
}

void loop()
{
  if (millis() - last_millis >= 3000 and display_state != -1){
    Serial.println(">OLED protection timeout");
    value_selected = 0;
    display_state = -1;
    setForOption();
  }
  if (rotaryEncoder.encoderChanged()) {
    change_value();
  }
  if(!digitalRead(ROTARY_ENCODER_BUTTON_PIN)){
    while (!digitalRead(ROTARY_ENCODER_BUTTON_PIN)){
      delay(500);
    }
    rotary_onButtonClick();
  }
}
