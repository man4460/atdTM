// Unified variables for old/new board.
// เลือกบอร์ดด้วย OldBoard ตัวเดียว:
//  - OldBoard == 1 : บอร์ดเก่า (OTA ai_old)
//  - OldBoard == 0 : บอร์ดใหม่ (OTA ai_new)

#pragma once

#define WiFi_TIMEOUT_MS 20000

// =============================================================================
//  DEPLOY CONFIG — แก้เฉพาะบล็อกนี้ก่อน pio run / flash
// =============================================================================

// --- 1) บอร์ด & เวอร์ชัน firmware ---
#define OldBoard 1 // 0 = บอร์ดใหม่ (ai_new) | 1 = บอร์ดเก่า (ai_old)

const char *fwversion[] = {"Current Firmware\r\n", "Version 3.91\r\n"};

// --- 2) ตัวเครื่อง / Melody (ต้องตรงกับหน้าแก้ไขเครื่องในเว็บ) ---
int gid = 99;
String Noserial = "65M000000";
String IDserver = "94"; // SharePoint list item ID
int CodeMachine = 0;
int Mode = 2;

#if OldBoard == 1
String userID = "ai_old"; // OTA → fw/ai_old/
#else
String userID = "ai_new"; // OTA → fw/ai_new/
#endif

// --- 3) WiFi เริ่มต้น (แก้ได้จาก Melody config ภายหลัง) ---
String ssidStr = "melody";
String passStr = "0815418771";
// String ssidStr = "man4460base_2.4G";
// String passStr = "Man0815418771";

// --- 4) MQTT — v4 WSS (Melody machines.version=4) หรือ TCP เก่า (v1–v3) ---
#define MELODY_PROTOCOL_VERSION 4
#define MQTT_USE_WEBSOCKET 1 // 1 = WSS | 0 = TCP mawell.thddns.net:4741–4744

#if MQTT_USE_WEBSOCKET
// WSS ผ่าน Cloudflare
#define MQTT_WS_USE_SSL 1 // 1 = wss:// :443 | 0 = ws:// LAN ตรง PC :9001
const char *mqtt_ws_host = "melodymqtt.ma-well.com";
const int mqtt_ws_port = 443;
const char *mqtt_ws_path = "/";
// LAN ทดสอบ: MQTT_WS_USE_SSL 0, mqtt_ws_host "192.168.1.125", mqtt_ws_port 9001
int mqtt_port1 = 443; // แสดงใน log เท่านั้นเมื่อใช้ WSS
#else
// TCP — fleet เก่า / production
const char *mqtt_server1 = "mawell.thddns.net";
const char *mqtt_server2 = "broker.mqtt.cool";
#if OldBoard == 1
int mqtt_port1 = 4741; // หมุน 4742–4744 เมื่อ fail
#else
int mqtt_port1 = 4741;
#endif
int mqtt_port2 = 1883;
#endif

const char *mqtt_username = "mawell";
const char *mqtt_password = "4460";
int mqttStatus = 1; // 1 = broker หลัก | 2 = broker สำรอง

// --- 5) LDR (ปรับตามบอร์ด/สภาพแสง) ---
#if OldBoard == 1
int ldr_set = 1500;
int ldrMinus = 500;
#else
int ldr_set = 3000;
int ldrMinus = 1000;
#endif

// =============================================================================
//  HARDWARE — pin ตามบอร์ด (แก้เมื่อเปลี่ยนบอร์ดเท่านั้น)
// =============================================================================

#if OldBoard == 1

#define CLK 16
#define DIO 17
#define SIG_PIN 22
#define SIG_PIN2 23
#define EN_PIN 2
#define wifiLed 2
#define LDR1_PIN 34
#define LDR2_PIN 35
int ldrPin = LDR1_PIN;

int relay_pin[] = {27, 13, 26, 12, 14, 21, 14};
int sw_pin[] = {15, 4, 5, 18, 19};
const char *sw_name[] = {"BACK", "DOWN", "UP/TEST", "SETTING", "RESET"};

#elif OldBoard == 0

#define CLK 22
#define DIO 21
#define SIG_PIN 27
#define SIG_PIN2 26
#define EN_PIN 15
#define wifiLed 2
#define LDR1_PIN 36
#define LDR2_PIN 39
int ldrPin = LDR1_PIN;

int relay_pin[] = {4, 16, 17, 5, 18, 19, 23};
int sw_pin[] = {34, 35, 32, 33, 25};
const char *sw_name[] = {"BACK", "DOWN", "UP/TEST", "SETTING", "RESET"};

#endif

// coin slot (เหมือนทั้งสองบอร์ด)
int pinSlot = SIG_PIN;
int count = 0;
bool count_update_flag = false;
int coinPulse = 30;

// =============================================================================
//  RUNTIME / SHARED (ไม่ต้องแก้ตอน deploy ทั่วไป)
// =============================================================================

// Legacy EEPROM layout
#define EEPROM_SIZE 155
#define EEPROM_ID_ADDR 70
#define EEPROM_SSID_ADDR 82
#define EEPROM_PASS_ADDR (EEPROM_SSID_ADDR + 24)
#define EEPROM_IDSHAREPOINT_ADDR (EEPROM_PASS_ADDR + 24)
#define EEPROM_GID_ADDR (EEPROM_IDSHAREPOINT_ADDR + 8)

int coinValue = 10;
#define DRY_EXTEND_MIN_PER_COIN 10

const char *ntpServer = "1.th.pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0 * 3600;

int total_money = 0;
int price[] = {30, 40, 50};
int pricePro[] = {40, 50, 60};
int PriceShow[] = {30, 40, 50};
int item_price = 0;
int pendingBalance = 0;
int revSendingAmount = 0;
String revSendingTxn = "";
unsigned long revTxnSeq = 0;

struct PromoSlot {
  uint8_t day;
  uint8_t startHour;
  uint8_t startMin;
  uint8_t endHour;
  uint8_t endMin;
  int pricePro[3];
};

const int MAX_PROMO_SLOTS = 10;
PromoSlot promoSlots[MAX_PROMO_SLOTS];
int promoSlotCount = 0;
int chanelPay = 0;
int minn_countdown_wait = 0;
int second_countdown_wait = 0;
bool status_countdown_wait = false;
bool status_machine_run = false;
bool status_machine_prepare = false;
int program = 0;
int value_str1_int;

// mqtt runtime (mqtt_server1 / mqtt_port1 ตั้งใน DEPLOY ด้านบน)
const char *mqtt_server;
int mqtt_port;
#if !MQTT_USE_WEBSOCKET
// mqtt_server1, mqtt_server2, mqtt_port1, mqtt_port2 — อยู่ใน DEPLOY
#else
const char *mqtt_server1 = "192.168.1.125"; // ไม่ใช้เมื่อ WSS — คงไว้กัน compile
const char *mqtt_server2 = "broker.mqtt.cool";
int mqtt_port2 = 1883;
#endif

String topic = "V" + String(gid);

bool statusqr = true;
String cm;
String value_str1;
String value_str2;
String idSql;

unsigned long timerstanby;

int chanel = 0;
String StatusControl;
int Screen = 1;

int hrs = 0;
int minn = 0;
int second = 0;
int step = 0;
bool pause_timer = false;
int count_minn_pass = 0;

int program1[] = {3, 1, 0};
int program2[] = {3, 0, 0};
int program3[] = {4, 2, 1};
int rinStep2[] = {5, 1};
int rincommand[] = {6, 1};
int spin = 1;
int spinHier = 4;
int drum[] = {7, 0, 0};
int check_runing_time[] = {21, 13, 5};
int TimeCountdowndrum[] = {2, 07};
int TimeCountdown1[] = {0, 31};
int TimeCountdown2[] = {0, 31};
int TimeCountdown3[] = {0, 31};

int TimerA;
int timerDry[] = {30, 40, 50};

bool endProgram = false;
int state_error = 0;

String passAdmin = "4460";
bool stateChange_passAdmin = false;

int BT = 0;
int indexSet = 0;
int Mode1 = 0;
int Mode2 = 0;
int Mode3 = 0;
int Price;
int R[] = {0, 0, 0};
int T[] = {0, 0};
int SetupData = 0;
int StateShutdown = 0;

static bool state_step2 = false;
static bool state_step3 = false;

bool firstGetdata = false;
bool statewifi = false;

bool UpdateFw = false;
String Path_GetVersion = "/ota/version";
String Path_OTI = "/ota/download";
String Path_MqttReport = "/public/machines/mqtt-report";
String Path_DeviceAck = "/public/machines/device-ack";
String Path_UpdateState = "/public/machines/update-state";
String Path_DeviceRevenue = "/public/machines/device-revenue";
String api_key = "cf860590807a21db3be15ae3f99f706b";
String server = "backend.ma-well.com";
String host = server;
int port = 80;
long contentLength = 0;
bool isValidContentType = false;
bool stateGetdata = 0;
bool stateSetupdata = 0;
bool stateUpdateState = 0;
bool stateSentPriceServer = 0;
String TimeSent;
String mqttPayloadBuffer;
bool stateSendConfigMqtt = false;
bool stateSendVarjson = false;
unsigned long getdataDisplayUntil = 0;
int statusReportIntervalMinutes = 5;

unsigned long previousMillis = 0;
const long interval = 500;
int textIndex = 0;
uint8_t buffer[4] = {0, 0, 0, 0};
int runCount = 0;
int statedisplaystandby = 0;
bool scrolling = false;

bool test = false;
bool stateReset = false;
String data;
bool statePriceShow = false;

bool drain_water = false;
int stepHier = 0;

bool state_wifi_on = true;
static unsigned long timerSetMode = millis();
int chanelCommand = 0;
bool stateLdrOpen = false;
bool stateWhile = false;
bool stateCheckLdr1 = false;
bool stateCheckLdr2 = false;
int chanelLdrCheck = 0;
int stepLdrCheck = 0;
int displaystandbyLdrCheck = 0;
bool stateUpdateBalanceDry = false;
