#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266FtpServer.h>
#include <FS.h>
#include <SI7021.h>
#include <EEPROM.h>
#include <Wire.h>
#include <RtcDS3231.h>

// const char* imagefile = "/image.png";
const char* htmlfile = "/index.html";

/* Pin for select Wi-Fi mode */
const int AP_MODE_PIN = D5;
#define OUT1  14
#define OUT2  12

const char* ap_ssid = "Casper";
const char* ap_password = "Casper8266";

#define PARAM_ADDR              (0)
#define BUFFER_SIZE 24

const uint8_t defSchTimes[7][4][2] = 
{
  {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
  {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
  {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
  {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
  {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
  {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
  {{0, 8}, {16, 24}, {0, 0}, {0, 0}}
};  

struct Param {
  uint32_t ChipID;         
  uint8_t SetTemperature = 20;       
  bool Control = true;  
  uint8_t SchTimes[7][4][2] = 
  {
    {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
    {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
    {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
    {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
    {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
    {{0, 8}, {16, 24}, {0, 0}, {0, 0}},
    {{0, 8}, {16, 24}, {0, 0}, {0, 0}}
  };              
} Param;                       

struct BufferData {
  uint16_t bIndex = 0; 
  float temperature[BUFFER_SIZE]; 
  uint8_t humidity[BUFFER_SIZE]; 
  uint8_t hour[BUFFER_SIZE];      
} BufferData;                    

struct WorkTime {
  uint8_t second = 0;
  uint8_t minute = 0;
  uint8_t hour = 0;
  uint16_t day = 0;
} WorkTime;

/* Объявление сервера, порт 80 */
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
FtpServer ftpSrv;
String webSite = "";
String XML = "";
bool readFlag = false;
bool forceControl = false;
RtcDS3231<TwoWire> rtc(Wire);
RtcDateTime dt;

SI7021 sht;
float temperature = 0.0;
int humidity = 0.0;
uint8_t  OUT1State = 0;
uint8_t  OUT2State = 0;

void led_ctrl(uint8_t lev) {
  digitalWrite(2, !lev);
}

void push_bufferData(float temp, uint8_t hum, uint8_t hour) {
  led_ctrl(HIGH);
  int lIndex = BufferData.bIndex;
  BufferData.temperature[lIndex] = temp;
  BufferData.humidity[lIndex] = hum;
  BufferData.hour[lIndex] = hour;    
  if(++BufferData.bIndex == BUFFER_SIZE) {
      BufferData.bIndex = 0;
  }
  led_ctrl(LOW);
}

uint8_t get_bufferIndex(uint8_t addr) {
    int laddr = 0;
    if((addr + BufferData.bIndex) < BUFFER_SIZE) {
        laddr = addr + BufferData.bIndex;
    }
    else {
        laddr = addr - (BUFFER_SIZE - BufferData.bIndex);
    }
    return laddr;
}

float get_bufferTemp(uint8_t addr) {
  return BufferData.temperature[get_bufferIndex(addr)];
}

uint8_t get_bufferHum(uint8_t addr) {
  return BufferData.humidity[get_bufferIndex(addr)];
}

uint8_t get_bufferHour(uint8_t addr) {
  return BufferData.hour[get_bufferIndex(addr)];
}

void upd_wt() {
  if(++WorkTime.second == 60) {
    WorkTime.second = 0;
    if(++WorkTime.minute == 60) {
      WorkTime.minute = 0;
      if(++WorkTime.hour == 24) {
        WorkTime.hour = 0;
        ++WorkTime.day;
      }
    }
  }
}

bool getCtrlFromSchedule(uint8_t dow, uint8_t hour) {
  uint8_t tmp = 0;
  for(uint8_t zone = 0; zone < 4; zone++) {
    if((hour >= Param.SchTimes[dow][zone][0]) &&
       (hour < Param.SchTimes[dow][zone][1])) {
         tmp++;
    }
  }
  return (tmp > 0)?(true):(false);
}

uint32_t tws = 0;
static os_timer_t timer;
void h_timer(void *arg) {
  readFlag = true;
  upd_wt();
}   

void getWTStr(char *str) {
  sprintf(str, "%d.%02d:%02d:%02d", WorkTime.day,
                                    WorkTime.hour,
                                    WorkTime.minute,
                                    WorkTime.second);
}

void sblink(uint8_t rep, uint16_t del) {
  for(int i = 0; i < rep; i++) {
    led_ctrl(HIGH);
    delay(del);
    led_ctrl(LOW);
    delay(del);
  }
}

long constr(long val, long min, long max) {
	if(val < min) return min;
	else if(val > max) return max;
	else return val;
}

float constrF(float val, float min, float max) {
	if(val < min) return min;
	else if(val > max) return max;
	else return val;
}

void save_param() {
  led_ctrl(HIGH);
  int eSize = sizeof(Param);
  EEPROM.begin(eSize);
  EEPROM.put(PARAM_ADDR, Param);
  EEPROM.end();
  led_ctrl(LOW);
}

void eeprom_init() {
  Serial.println("EEPROM initialization");

  int eSize = sizeof(Param);
  EEPROM.begin(eSize);
  Param.ChipID = ESP.getChipId();
  Param.SetTemperature = 25;
  Param.Control = true;

  for(uint8_t dow = 0; dow < 7; dow++) {
    for(uint8_t zone = 0; zone < 4; zone++) {
      Param.SchTimes[dow][zone][0] = defSchTimes[dow][zone][0];
      Param.SchTimes[dow][zone][1] = defSchTimes[dow][zone][1];
    }
  }

  EEPROM.put(PARAM_ADDR, Param);
  EEPROM.end();
  Serial.println("EEPROM initialization completed");
  Serial.println("");
}

String getTimeStr()
{
    char str[9];
    sprintf(str, 
            "%02u:%02u:%02u",
            dt.Hour(),
            dt.Minute(),
            dt.Second());
    return str;
}

String getDateStr()
{
    char str[11];
    sprintf(str, 
            "%02u-%02u-%04u",
            dt.Day(),
            dt.Month(),
            dt.Year());
    return str;
}

/* Функция формирования строки XML данных */
void build_XML() {
  char txt[16];
  XML = "<?xml version='1.0'?>";
  XML +=    "<xml>";
  XML +="     <x_temp>";
  dtostrf(temperature, 4, 2, txt);
  XML +=        txt;
  XML +=      "</x_temp>";    
  XML +=      "<x_hum>";
  XML +=        humidity;
  XML +=      "</x_hum>";    
  XML +=      "<x_setTemp>";
  XML +=        Param.SetTemperature;
  XML +=      "</x_setTemp>";    
  XML +=      "<x_ctrl>";
  XML +=        Param.Control;
  XML +=      "</x_ctrl>";
  XML +=      "<x_forceCtrl>";
  XML +=        forceControl;
  XML +=      "</x_forceCtrl>";
  XML +=      "<x_out1>";
  XML +=        OUT1State;
  XML +=      "</x_out1>";
  XML +=      "<x_out2>";
  XML +=        OUT2State;
  XML +=      "</x_out2>";  
  XML +=      "<x_heap>";
  XML +=        ESP.getFreeHeap();
  XML +=      "</x_heap>";  
  getWTStr(txt);
  XML +=      "<x_wt>";
  XML +=        txt;
  XML +=      "</x_wt>";    
  for(int i = 0; i < BUFFER_SIZE; i++) {
    XML +=      "<x_btemp>";
    XML +=        get_bufferTemp(i);
    XML +=      "</x_btemp>"; 
    XML +=      "<x_bhum>";
    XML +=        get_bufferHum(i);
    XML +=      "</x_bhum>"; 
    XML +=      "<x_bhour>";
    XML +=        get_bufferHour(i);
    XML +=      "</x_bhour>"; 
  }
  for(uint8_t dw = 0; dw < 7; dw++) {
    for(uint8_t tz = 0; tz < 4; tz++) {
      for(uint8_t t = 0; t < 2; t++) {
        XML +=      "<x_scht>";
        XML +=      Param.SchTimes[dw][tz][t];
        XML +=      "</x_scht>"; 
      }
    }
  }
  /* Date . Time */
  XML +=      "<x_time>";
  XML +=        getTimeStr();
  XML +=      "</x_time>";
  XML +=      "<x_date>";
  XML +=        getDateStr();
  XML +=      "</x_date>";

  XML +=     "</xml>";
  // Serial.println(XML);
}

bool loadFromSpiffs(String path) {
  led_ctrl(HIGH);
  Serial.print(F("load From Spiffs: "));
  Serial.println(path);
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.html";

  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".html")) dataType = "text/html";
  else if(path.endsWith(".htm")) dataType = "text/html";
  else if(path.endsWith(".css")) dataType = "text/css";
  else if(path.endsWith(".js")) dataType = "application/javascript";
  else if(path.endsWith(".png")) dataType = "image/png";
  else if(path.endsWith(".gif")) dataType = "image/gif";
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".ico")) dataType = "image/x-icon";
  else if(path.endsWith(".xml")) dataType = "text/xml";
  else if(path.endsWith(".pdf")) dataType = "application/pdf";
  else if(path.endsWith(".zip")) dataType = "application/zip";
  File dataFile = SPIFFS.open(path.c_str(), "r");
  //Serial.print(F("File: "));
  //Serial.println(dataFile.name());
  if (server.hasArg("download")) dataType = "application/octet-stream";
  if (server.streamFile(dataFile, dataType) != dataFile.size()) {}

  dataFile.close();
  led_ctrl(LOW);
  return true;
}

void handleWebRequests() {
  if(loadFromSpiffs(server.uri())) return;
  Serial.println(F("File Not Detected"));
  String message = "File Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " NAME:"+server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  Serial.println(message);
}

/* Обработчик/handler запроса HTML страницы */
/* Отправляет клиенту(браузеру) HTML страницу */
void h_Website() {  
  //Serial.println("HTML handler");
  server.sendHeader("Location", "/index.html", true);   //Redirect to our html web page
  server.send(302, "text/plane", "");
}

/* Обработчик/handler запроса XML данных */
void h_XML() {
  build_XML();
  server.send(200, "text/xml", XML);
}

/* Обработчик/handler /clickBut */
void h_clickBut() { 
  Serial.print(F("HTML button: "));
  Serial.println(server.arg("val"));
  build_XML();
  server.send(200, "text/xml", XML);
}

void h_factoryDef() {
  eeprom_init();
}

void h_setSchedule() {
  /*Serial.println(F("Set param handler: "));
  
  Serial.print(F("Server has: ")); Serial.print(server.args()); Serial.println(F(" argument(s):"));*/

  uint8_t argVal[4];
  
  for(int i = 0; i < server.args(); i++) {
    argVal[i] = server.arg(i).toInt();
    //Serial.print(F("arg name: ")); Serial.print(server.argName(i)); Serial.print(F(", value: ")); Serial.println(argVal[i]);
  }

  if((server.hasArg("zone")) && (server.hasArg("from")) && (server.hasArg("to"))) {
    Param.SchTimes[argVal[0] - 1][argVal[1] - 1][0] = argVal[2];
    Param.SchTimes[argVal[0] - 1][argVal[1] - 1][1] = argVal[3];
    save_param();    
  }
}

void h_setParam() {
  /*Serial.println(F("Set param handler: "));
  
  Serial.print(F("Server has: ")); Serial.print(server.args()); Serial.println(F(" argument(s):"));
  
  for(int i = 0; i < server.args(); i++) {
    Serial.print(F("arg name: ")); Serial.print(server.argName(i)); Serial.print(F(", value: ")); Serial.println(server.arg(i));
  }*/

  if(server.hasArg("setTemp")) {
    //Serial.print(F("setTemp: ")); Serial.println(server.arg("setTemp"));
    Param.SetTemperature = server.arg("setTemp").toInt();
    save_param();    
  }

  if(server.hasArg("setCtrl")) {
    //Serial.print(F("setCtrl: ")); Serial.println(server.arg("setCtrl"));
    if(server.arg("setCtrl") == "false") Param.Control = false;
    if(server.arg("setCtrl") == "true") Param.Control = true;
    save_param(); 
  }

  if(server.hasArg("setForceCtrl")) {
    //Serial.print(F("setForceCtrl: ")); Serial.println(server.arg("setForceCtrl"));
    if(server.arg("setForceCtrl") == "false") forceControl = false;
    if(server.arg("setForceCtrl") == "true") forceControl = true;
    save_param(); 
  }

  if((server.hasArg("hour")) && (server.hasArg("minute"))) {
    rtc.SetDateTime(RtcDateTime(dt.Year(), dt.Month(), dt.Day(), server.arg("hour").toInt(), server.arg("minute").toInt(), 0));
  }

  if((server.hasArg("day")) && (server.hasArg("month")) && (server.hasArg("year"))) {
    rtc.SetDateTime(RtcDateTime(server.arg("year").toInt(),
                                server.arg("month").toInt(),
                                server.arg("day").toInt(), dt.Hour(), dt.Minute(), dt.Second()));
  }

  build_XML();
  server.send(200, "text/xml", XML);
}

void h_wifi_param() {
  Serial.println(F("Apply Wi-Fi parameters"));
  String ssid = server.arg("SSID");
  String pass = server.arg("stapass");
  Serial.print("Wi-Fi SSID: "); Serial.println(ssid);
  Serial.print("Wi-Fi pass: "); Serial.println(pass);
  WiFi.begin(ssid, pass);
  server.send(200, "text/html", "Wi-Fi setting is updated, module will be rebooting...");
  delay(500);
  SPIFFS.end();
  Serial.println("Resetting ESP");
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  uint16_t WiFiConnTimeOut = 50;

  Serial.println("");
  int eSize = sizeof(Param);
  EEPROM.begin(eSize);
  EEPROM.get(PARAM_ADDR, Param);
  if(Param.ChipID != ESP.getChipId()) {
    eeprom_init();
  }

  /* Buffer init */
  BufferData.bIndex = 0;
  for(int i = 0; i < (BUFFER_SIZE); i++) {    
    push_bufferData(1.0, 1, 0);
  }

  pinMode(AP_MODE_PIN, INPUT_PULLUP);
  pinMode(2, OUTPUT);
  led_ctrl(LOW);

  sblink(10, 25);

  delay(1000);

  Serial.println("");
  Serial.println(F("--- Wi-Fi config ---"));
  if(WiFi.SSID() != NULL) {
    sblink(1, 500);
    Serial.println(F("Wi-Fi STA mode"));
    Serial.print(F("SSID: ")); Serial.print(WiFi.SSID());
    WiFi.begin();
    // Ожидание подключения
    Serial.println("");
    Serial.print(F("Wi-Fi is connecting..."));
    while (WiFi.status() != WL_CONNECTED) {      
      sblink(1, 10);
      delay(190);
      Serial.print(".");
      if(--WiFiConnTimeOut == 0) break;
    }
    if(WiFiConnTimeOut == 0) {
      Serial.println("");
      Serial.println(F("Wi-Fi connected timeout"));
    }
    else {
      sblink(2, 500);
      Serial.println("");
      Serial.print(F("STA connected to: ")); 
      Serial.println(WiFi.SSID());
      IPAddress myIP = WiFi.localIP();
      Serial.print(F("Local IP address: ")); Serial.println(myIP);
      WiFi.enableAP(false);
    }
  }
  if((WiFiConnTimeOut == 0) || (WiFi.SSID() == NULL)) {
    sblink(3, 500);
    Serial.println(F("Wi-Fi AP mode"));
    Serial.println(F("AP configuring..."));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password); 
    Serial.println(F("done"));
    IPAddress myIP = WiFi.softAPIP();
    Serial.print(F("AP IP address: "));
    Serial.println(myIP);
  }

  Serial.println();

  if(SPIFFS.begin()) {
    Serial.println(F("SPIFFS opened!"));
    ftpSrv.begin("esp8266","esp8266"); 
    Serial.println(F("FTP server started!"));
    Serial.println(F("user: esp8266"));
    Serial.println(F("pass: esp8266"));
  }

  // Ининциализация строки XML
  build_XML();       

  /* OTA */
  // httpUpdater.setup(&server, ota_user, ota_pass);
  httpUpdater.setup(&server);
  
  // Регистрация обработчиков
  server.on("/", h_Website);    
  server.onNotFound(handleWebRequests); //Set setver all paths are not found so we can handle as per URI              
  server.on("/xml", h_XML);
  server.on("/clickBut", h_clickBut);
  server.on("/setParam", h_setParam);
  server.on("/wifi_param", h_wifi_param);
  server.on("/zoneParam", h_setSchedule);
  server.on("/factoryDef", h_factoryDef);
  server.begin();

  os_timer_disarm(&timer);
  os_timer_setfn(&timer, (os_timer_func_t *)h_timer, NULL);
  os_timer_arm(&timer, 1000, 1);

    /*********************** Relay configure **********************/
  pinMode(OUT1, OUTPUT);
  pinMode(OUT2, OUTPUT);
  OUT1State = LOW;
  OUT2State = LOW;
  /**************************************************************/

  rtc.Begin();
  sht.begin(SDA, SCL);  
  // rtc.SetDateTime(RtcDateTime(2019, 12, 17, 14, 57, 0));
}

void loop() {
  /* Обработка запросов HTML клиента */
  server.handleClient();
  /* Обработка запросов FTP клиента */
  ftpSrv.handleFTP();
  delay(1);

  if(readFlag) {
    readFlag = false;

    dt = rtc.GetDateTime();

    /* RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); */
    // printDateTime(dt);

    temperature = constrF((sht.getCelsiusHundredths() / 100.0), 10.0, 40.0);
    humidity = constr(sht.getHumidityPercent(), 0, 100);

    if((dt.Minute() == 0) && (dt.Second() == 0)) {
      push_bufferData(temperature, humidity, dt.Hour());
    }    

    build_XML();

    if((Param.Control == 1) && ((getCtrlFromSchedule((dt.DayOfWeek() == 0)?(6):(dt.DayOfWeek() - 1), dt.Hour())) || (forceControl))) {
      
      if(temperature <= (float)(Param.SetTemperature - 1)) {
        digitalWrite(OUT1, HIGH);
        OUT1State = HIGH;
      }
      if(temperature >= (float)Param.SetTemperature) {
        digitalWrite(OUT1, LOW);
        OUT1State = LOW;
      }
    }
    else {
      digitalWrite(OUT1, LOW);
      OUT1State = LOW;
    }
  }
}
