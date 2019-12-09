#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266FtpServer.h>
#include <FS.h>
#include <SI7021.h>
#include <EEPROM.h>

// const char* imagefile = "/image.png";
const char* htmlfile = "/index.html";

/* Pin for select Wi-Fi mode */
const int AP_MODE_PIN = D5;
#define OUT1  14
#define OUT2  12

#define MAX_COORDS_ARR  10

const char* ap_ssid = "Casper";
const char* ap_password = "Casper8266";

struct Param {
  uint32_t ChipID;
  uint8_t SetTemperature;
  bool Control;
} Param;

/* Объявление сервера, порт 80 */
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
FtpServer ftpSrv;
String webSite = "";
String XML = "";
bool readFlag = false;

SI7021 sht;
float temperature = 0.0;
int humidity = 0.0;

static os_timer_t timer;
void h_timer(void *arg) {
  readFlag = true;
}

void led_ctrl(uint8_t lev) {
  digitalWrite(2, !lev);
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
  Serial.println("EEPROM save param");
  int pSize = sizeof(Param);
  Serial.print("EEPROM data size: "); Serial.println(pSize);
  EEPROM.begin(pSize);
  Serial.print(F("Chip ID: "));           Serial.println(Param.ChipID);
  Serial.print(F("Set temperature: "));   Serial.println(Param.SetTemperature);
  Serial.print(F("Control: "));           Serial.println(Param.Control);
  EEPROM.put(0, Param);
  EEPROM.end();
  Serial.print("EEPROM save param completed");
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
  Serial.print(F("File: "));
  Serial.println(dataFile.name());
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
  Serial.println("HTML handler");
  server.sendHeader("Location", "/index.html", true);   //Redirect to our html web page
  server.send(302, "text/plane", "");
}

/* Обработчик/handler запроса XML данных */
/* Отправляет клиенту(браузеру) XML данные */
void h_XML() {
  build_XML();
  server.send(200, "text/xml", XML);
}

/* Обработчик/handler /clickBut */
/* Отправляет клиенту(браузеру) XML данные */
void h_clickBut() { 
  Serial.print(F("HTML button: "));
  //if(server.arg("val") == "AddPoint") {
  //}
  Serial.println(server.arg("val"));
  build_XML();
  server.send(200, "text/xml", XML);
}

void h_setParam() {
  Serial.println(F("Set param handler: "));
  
  Serial.print(F("Server has: ")); Serial.print(server.args()); Serial.println(F(" argument(s):"));
  
  for(int i = 0; i < server.args(); i++) {
    Serial.print(F("arg name: ")); Serial.println(server.argName(i));
  }

  if(server.hasArg("setTemp")) {
    Serial.print(F("setTemp: ")); Serial.println(server.arg("setTemp"));
    Param.SetTemperature = server.arg("setTemp").toInt();
    save_param();    
  }

  if(server.hasArg("setCtrl")) {
    Serial.print(F("setCtrl: ")); Serial.println(server.arg("setCtrl"));
    if(server.arg("setCtrl") == "false") Param.Control = false;
    if(server.arg("setCtrl") == "true") Param.Control = true;
    save_param(); 
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
  server.send(200, "text/html", "<br><br><br><br><br>Module will be rebooting...");
  delay(500);
  SPIFFS.end();
  Serial.println("Resetting ESP");
  ESP.restart();
}

void eeprom_init() {
  Serial.println("EEPROM initialization");
  int pSize = sizeof(Param);
  Serial.print("EEPROM data size: "); Serial.println(pSize);
  EEPROM.begin(pSize);
  Param.ChipID = ESP.getChipId();
  Param.SetTemperature = 27;
  Param.Control = true;
  EEPROM.put(0, Param);
  EEPROM.end();
  Serial.print("EEPROM initialization completed");
}

void setup() {
  Serial.begin(115200);
  uint16_t WiFiConnTimeOut = 50;

  Serial.println("");
  int pSize = sizeof(Param);
  EEPROM.begin(pSize);
  EEPROM.get(0, Param);
  if(Param.ChipID != ESP.getChipId()) {
    eeprom_init();
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
  server.begin();

  os_timer_disarm(&timer);
  os_timer_setfn(&timer, (os_timer_func_t *)h_timer, NULL);
  os_timer_arm(&timer, 1000, 1);

    /*********************** Relay configure **********************/
  pinMode(OUT1, OUTPUT);
  pinMode(OUT2, OUTPUT);
  /**************************************************************/

  sht.begin(SDA, SCL);
}

void loop() {
  /* Обработка запросов HTML клиента */
  server.handleClient();
  /* Обработка запросов FTP клиента */
  ftpSrv.handleFTP();
  delay(1);

  if(readFlag) {
    readFlag = false;
    if(sht.sensorExists()) {
      temperature = constrF((sht.getCelsiusHundredths() / 100.0), 10.0, 40.0);
      humidity = constr(sht.getHumidityPercent(), 0, 100);
      build_XML();
      if(Param.Control == 1) {
        if(temperature <= (float)(Param.SetTemperature - 1)) {
          digitalWrite(OUT1, HIGH);
        }
        if(temperature >= (float)Param.SetTemperature) {
          digitalWrite(OUT1, LOW);
        }
      }
      else {
        digitalWrite(OUT1, LOW);
      }
    }
    else Serial.println(F("SHT is loss"));
  }
}