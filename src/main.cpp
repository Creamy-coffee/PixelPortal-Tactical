#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ArduinoBLE.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "time.h"

// ================= 用户配置区域 =================
const char* ssid     = "PCDN";
const char* password = "43122720";
const int localPort  = 12345;
const int TIMEOUT_MS = 5000;
const char* ntpServer = "ntp1.aliyun.com";
const long  gmtOffset_sec = 8 * 3600;
const int   daylightOffset_sec = 0;
// ===========================================

#define PANEL_RES_X 64      
#define PANEL_RES_Y 64      
#define PANEL_CHAIN 1       
const int SENSOR_PIN = 32;   
const int LDR_PIN = 35;      
const int BRIGHTNESS = 20;   

WiFiUDP Udp;
uint8_t fullBuffer[8192];     
bool chunkReceived[8] = {0};  
unsigned long lastUdpTime = 0;
bool isMapMode = false; 

MatrixPanel_I2S_DMA *dma_display = nullptr;
Preferences preferences;

// --- 逻辑变量 ---
bool hasPerson = false;
unsigned long lastTriggerTime = 0;  
int currentLightVal = 4095;          
bool isLampRealOn = false;           
bool lastLampRealOn = false;
bool systemStateOn = false;          
unsigned long lastFlashTime = 0;     
int retryCount = 0;

// === [修改点] 补刀上限改为 300 次 (即 60秒) ===
const int MAX_RETRIES = 300; 

unsigned long lastActionTime = 0;
int actionType = 0; 
const unsigned long DELAY_TIME = 20000; 
const int LDR_THRESHOLD = 2000; 

// --- BUG修复 & 防抖变量 ---
unsigned long lampRealOnTime = 0; 
unsigned long lastOffCmdTime = 0; 

// --- 蓝牙数据 ---
const uint16_t COMPANY_ID = 0xFFFF;
uint8_t CODE_ON[] = { 0xFF, 0xFF, 0x10, 0x01, 0xC9, 0x6C, 0x2C, 0x60, 0xB7, 0x49, 0xDB, 0x49, 0x4C, 0x48, 0xE3, 0xE3, 0xE3, 0xE3, 0xB6 };
uint8_t CODE_OFF[] = { 0xFF, 0xFF, 0x10, 0x01, 0xC9, 0x6C, 0x2C, 0x60, 0xB7, 0x49, 0xDA, 0x48, 0x4C, 0xE3, 0xE3, 0xE3, 0xE3, 0xE3, 0x1D };
uint8_t onByte10, onByte18, offByte10, offByte18;

// --- Mario 素材 ---
#define COLOR_SKY      0x5CBF 
#define COLOR_GROUND   0xD385 
#define COLOR_BRICK    0xEA24 
#define COLOR_SHADOW   0x0000 
#define COLOR_MARIO_R  0xF800 
#define COLOR_MARIO_S  0xFCD0 
#define COLOR_MARIO_B  0x001F 
#define COLOR_MARIO_H  0x6180 

const uint8_t PROGMEM MARIO_SPRITE[16][12] = {
    {0,0,0,1,1,1,1,1,0,0,0,0}, {0,0,1,1,1,1,1,1,1,1,1,0},
    {0,0,4,4,4,2,2,4,2,0,0,0}, {0,4,2,4,2,2,2,4,2,2,2,0},
    {0,4,2,4,4,2,2,2,4,2,2,4}, {0,4,4,2,2,2,2,4,4,4,4,0},
    {0,0,0,2,2,2,2,2,2,2,0,0}, {0,0,1,1,3,1,1,3,1,1,0,0},
    {0,1,1,1,3,1,1,3,1,1,1,0}, {1,1,1,1,3,3,3,3,1,1,1,1},
    {2,2,1,3,0,3,3,0,3,1,2,2}, {2,2,2,3,3,3,3,3,3,2,2,2},
    {2,2,3,3,3,3,3,3,3,3,2,2}, {0,0,3,3,3,0,0,3,3,3,0,0},
    {0,4,4,4,0,0,0,0,4,4,4,0}, {4,4,4,4,0,0,0,0,4,4,4,4}
};

void sendSeparatePacket(int type);
void updateMarioDisplay(unsigned long now);
void drawMarioSprite(int x, int y);
void drawBrick(int x, int y);

void setup() {
  Serial.begin(115200);
  pinMode(SENSOR_PIN, INPUT);
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  mxconfig.gpio.r1=25; mxconfig.gpio.g1=27; mxconfig.gpio.b1=26; 
  mxconfig.gpio.r2=14; mxconfig.gpio.g2=13; mxconfig.gpio.b2=12; 
  mxconfig.gpio.a=23; mxconfig.gpio.b=19; mxconfig.gpio.c=5; mxconfig.gpio.d=17; 
  mxconfig.gpio.e=18; mxconfig.gpio.lat=4; mxconfig.gpio.oe=15; mxconfig.gpio.clk=16;
  mxconfig.double_buff = true; 
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(BRIGHTNESS);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Udp.begin(localPort);
  BLE.begin();

  preferences.begin("lamp_v37", false);
  onByte10 = preferences.getUChar("on10", 0xDB);
  onByte18 = preferences.getUChar("on18", 0xB6);
  offByte10 = preferences.getUChar("off10", 0xDA);
  offByte18 = preferences.getUChar("off18", 0x1D);
  preferences.end();
}

void loop() {
  unsigned long now = millis();
  
  // 1. 雷达逻辑
  int packetSize = Udp.parsePacket();
  if (packetSize > 0) {
    uint8_t tempBuf[1026];
    int len = Udp.read(tempBuf, 1026);
    if (len >= 1024 && tempBuf[1] == 8) {
      uint8_t id = tempBuf[0];
      if (id < 8) { memcpy(fullBuffer + (id * 1024), tempBuf + 2, len - 2); chunkReceived[id] = true; }
      bool allComplete = true;
      for (int i=0; i<8; i++) if (!chunkReceived[i]) allComplete = false;
      if (allComplete) {
        dma_display->drawRGBBitmap(0, 0, (uint16_t*)fullBuffer, 64, 64);
        dma_display->flipDMABuffer();
        lastUdpTime = now;
        isMapMode = true;
        memset(chunkReceived, 0, 8);
      }
    }
  }
  if (isMapMode && (now - lastUdpTime > TIMEOUT_MS)) isMapMode = false;
  if (isMapMode) return;

  // 2. 状态读取
  hasPerson = (digitalRead(SENSOR_PIN) == HIGH); 
  currentLightVal = analogRead(LDR_PIN);
  isLampRealOn = (currentLightVal < LDR_THRESHOLD);

  // === 3. 状态变化 (含屏蔽逻辑 + BUG修复) ===
  if (isLampRealOn != lastLampRealOn) {
    if (isLampRealOn) { 
        // 4秒屏蔽期：如果刚发过关灯指令，假装没看见灯亮
        if (now - lastOffCmdTime > 4000) { 
            if (actionType != 2) {
                lastTriggerTime = now; 
                systemStateOn = true; 
                actionType = 0; 
                lampRealOnTime = now; 
            }
        }
    } 
    else { 
        // BUG检测：系统开 + 非手动关 + 刚亮5秒内 + 离上次关灯超4秒
        if (systemStateOn && actionType != 2 && (now - lampRealOnTime < 5000) && (now - lastOffCmdTime > 4000)) {
             sendSeparatePacket(2); delay(80); sendSeparatePacket(1); 
        } 
        else {
             systemStateOn = false; 
             actionType = 0; 
        }
    }
    lastLampRealOn = isLampRealOn; 
  }

  // 4. 人体感应 ---
  if (hasPerson) {
    lastTriggerTime = now;
    if (!systemStateOn && !isLampRealOn) systemStateOn = true;
  }

  // 5. 控制逻辑 ---
  if (systemStateOn) {
    if (now - lastTriggerTime > DELAY_TIME) systemStateOn = false; 
    else if (!isLampRealOn && actionType == 0) { actionType = 1; retryCount = 0; lastActionTime = now; sendSeparatePacket(1); lastFlashTime = now; }
  } else {
    if (isLampRealOn && actionType == 0) { actionType = 2; retryCount = 0; lastActionTime = now; sendSeparatePacket(2); lastFlashTime = now; }
  }

  //6. 补刀逻辑 (MAX_RETRIES = 300)
  if (actionType != 0 && (now - lastActionTime > 200)) {
     if (actionType == 1) { 
        if (isLampRealOn) actionType = 0; 
        else if (retryCount < MAX_RETRIES) { sendSeparatePacket(1); lastFlashTime = now; retryCount++; lastActionTime = now; } else actionType = 0;
     } else if (actionType == 2) { 
        if (!isLampRealOn) actionType = 0; 
        else if (retryCount < MAX_RETRIES) { sendSeparatePacket(2); lastFlashTime = now; retryCount++; lastActionTime = now; } else actionType = 0;
     }
  }

  updateMarioDisplay(now);
}

void sendSeparatePacket(int type) {
  uint8_t* payload; size_t len;
  if (type == 1) { payload = CODE_ON; len = sizeof(CODE_ON); payload[10] = onByte10; payload[18] = onByte18; }
  else { 
      // 记录关灯时间，用于4秒屏蔽
      payload = CODE_OFF; len = sizeof(CODE_OFF); payload[10] = offByte10; payload[18] = offByte18; 
      lastOffCmdTime = millis(); 
  }
  BLE.stopAdvertise();
  BLEAdvertisingData advData;
  advData.setManufacturerData(COMPANY_ID, payload, len);
  BLE.setAdvertisingData(advData);
  BLE.advertise();
  delay(25); BLE.stopAdvertise();
  preferences.begin("lamp_v37", false);
  if (type == 1) { onByte10++; onByte18--; preferences.putUChar("on10", onByte10); preferences.putUChar("on18", onByte18); }
  else { offByte10++; offByte18--; preferences.putUChar("off10", offByte10); preferences.putUChar("off18", offByte18); }
  preferences.end();
}

void drawMarioSprite(int x, int y) {
  for(int i=0; i<16; i++) {
    for(int j=0; j<12; j++) {
      uint8_t pixelType = pgm_read_byte(&MARIO_SPRITE[i][j]);
      uint16_t color = 0;
      if(pixelType == 1) color = COLOR_MARIO_R;
      else if(pixelType == 2) color = COLOR_MARIO_S;
      else if(pixelType == 3) color = COLOR_MARIO_B;
      else if(pixelType == 4) color = COLOR_MARIO_H;
      if(pixelType != 0) dma_display->drawPixel(x+j, y+i, color);
    }
  }
}

void drawBrick(int x, int y) {
  dma_display->fillRect(x, y, 16, 16, COLOR_BRICK); 
  dma_display->drawRect(x, y, 16, 16, COLOR_SHADOW); 
}

void updateMarioDisplay(unsigned long now) {
  static unsigned long lastDisp = 0;
  if(now - lastDisp < 30) return; 
  lastDisp = now;
  dma_display->fillScreen(0); 
  if (isLampRealOn || hasPerson || systemStateOn) {
    dma_display->fillScreen(COLOR_SKY); 
    dma_display->fillRect(0, 56, 64, 8, COLOR_GROUND);
    drawBrick(14, 20); drawBrick(34, 20);
    drawMarioSprite(26, 40);
    if (systemStateOn && !hasPerson) { 
        long remaining = (DELAY_TIME - (now - lastTriggerTime)) / 1000;
        if(remaining < 0) remaining = 0;
        dma_display->setTextColor(0xFFFF);      
        dma_display->setCursor(19, 24); dma_display->print(remaining/10);
        dma_display->setCursor(39, 24); dma_display->print(remaining%10);
    } else {
        struct tm timeinfo;
        if(getLocalTime(&timeinfo)){
            dma_display->setTextColor(0xFFFF);
            dma_display->setCursor(16, 24); dma_display->printf("%02d", timeinfo.tm_hour);
            dma_display->setCursor(36, 24); dma_display->printf("%02d", timeinfo.tm_min);
        }
    }
  }
  if (now - lastFlashTime < 300) dma_display->fillCircle(32, 10, 2, 0x07FF); 
  if (digitalRead(SENSOR_PIN) == HIGH) dma_display->drawPixel(63, 63, 0xF800); 
  dma_display->flipDMABuffer();
}