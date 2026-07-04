// Unified variables for old/new board.
// เลือกบอร์ดด้วย OldBoard ตัวเดียว:
//  - OldBoard == 1 : ค่าเดิมจาก varableOldbord.h
//  - OldBoard == 0 : ค่าเดิมจาก varable.h (บอร์ดใหม่)

#pragma once

#define OldBoard 0 // กำหนดว่าเป็นบอร์ดเก่าหรือใหม่ ถ้าเป็นบอร์ดเก่าให้ใส่ 1 ถ้าเป็นบอร์ดใหม่ให้ใส่ 0
#define WiFi_TIMEOUT_MS 20000

// ======================= Board-specific pins & hardware =======================

// tm1637, coin, EN, LDR pins, relay, switches, wifi SSID/pass, ids

#if OldBoard == 1

// ====== บอร์ดเก่า (ค่าจาก varableOldbord.h) ======
//tm 1637
#define CLK 16  // ขาต่อกับ CLK ของ TM1637
#define DIO 17  // ขาต่อกับ DIO ของ TM1637
//coin
#define SIG_PIN 22  // กำหนดขาที่ต่อ SIG (RX)
#define SIG_PIN2 23 // กำหนดขาที่ต่อ SIG (RX)
#define EN_PIN 2    // กำหนดขาที่ต่อ EN

#define wifiLed 2

//ldr
#define LDR1_PIN 34
#define LDR2_PIN 35
int ldrPin = LDR1_PIN;

// relay & switches
// int relay_pin[] = { 14, 13, 26, 12, 27, 21, 27 }; // old dry board andaman
int relay_pin[] = {27, 13, 26, 12, 14, 21, 14};
int sw_pin[] = {15, 4, 5, 18, 19};
const char *sw_name[] = {"BACK", "DOWN", "UP/TEST", "SETTING", "RESET"};

// coin slot
int pinSlot = SIG_PIN;
int count = 0;
bool count_update_flag = false;
int coinPulse = 30;

// WiFi setup (old board defaults)
// String ssidStr = "man4460base_2.4G";
// String passStr = "Man0815418771";
String ssidStr = "melody";
String passStr = "0815418771";
// String ssidStr = "ELe2.4G";
// String passStr = "E2722023";
// String ssidStr = "Andaman";
// String passStr = "0632352964";
// String ssidStr = "LUCKY 1234_2.4G";
// String passStr = "adithep1234";
// String ssidStr = "SP_2.4G";
// String passStr = "49494949";
// String ssidStr = "Time machine_2.4G";
// String passStr = "safe8788578";
// String ssidStr = "Wash_2.4G";
// String passStr = "#88889999";
// String ssidStr = "Yindee_2.4GHz";
// String passStr = "safe8788578";
// String ssidStr = "SIRI WIFI_2.4G";
// String passStr = "20040925";
// String ssidStr = "Sakmai_2.4G";
// String passStr = "sakmaiubon";

// machine id / mode (old)
int gid = 99;
String IDserver = "94"; // ID for sharepoint

int CodeMachine = 0;
int Mode = 2;
String userID = "ai_old";
// String userID = "ai_old_hier";
String Noserial = "65M000000";
// String Noserial = "67M370337";

// sensor thresholds (old)
int ldr_set = 1500;
int ldrMinus = 500;

// mqtt port start (old)
int mqtt_port1 = 4741; // broker หลัก Melody (4741) — ลอง 4742–4744 เมื่อ fail

// fw info (old)
const char *fwversion[] = {"Current Firmware\r\n", "Version 3.65\r\n"};
// const char *fwversion[] = {"Current Firmware\r\n", "Version 9.99\r\n"};

#elif OldBoard == 0

// ====== บอร์ดใหม่ OldBoard 0 — ESP32 classic + TM1637 (env: esp32dev, OTA ai_new) ======
//tm 1637
#define CLK 22  // ขาต่อกับ CLK ของ TM1637
#define DIO 21  // ขาต่อกับ DIO ของ TM1637
//coin
#define SIG_PIN 27  // กำหนดขาที่ต่อ SIG (RX)
#define SIG_PIN2 26 // กำหนดขาที่ต่อ SIG (RX)
#define EN_PIN 15   // กำหนดขาที่ต่อ EN

#define wifiLed 2

//ldr
#define LDR1_PIN 36
#define LDR2_PIN 39
int ldrPin = LDR1_PIN;

// relay & switches
int relay_pin[] = {4, 16, 17, 5, 18, 19, 23};
int sw_pin[] = {34, 35, 32, 33, 25};
const char *sw_name[] = {"BACK", "DOWN", "UP/TEST", "SETTING", "RESET"};

// coin slot
int pinSlot = SIG_PIN;
int count = 0;
bool count_update_flag = false;
int coinPulse = 30;

// WiFi setup (new board defaults)
// String ssidStr = "man4460base_2.4G";
// String passStr = "Man0815418771";
String ssidStr = "melody";
String passStr = "0815418771";
// String ssidStr = "ELe2.4G";
// String passStr = "E2722023";
// String ssidStr = "Niko wash&dry";
// String passStr = "0641615445";
// String ssidStr = "Andaman";
// String passStr = "0632352964";
// String ssidStr = "TP-Link_EF0E";
// String passStr = "86008464";
// String ssidStr = "Sissy Laundry";
// String passStr = "0843363459";
// String ssidStr = "Time machine_2.4G";
// String passStr = "safe8788578";

// machine id / mode (new)
int gid = 99;
String IDserver = "94"; // ID for sharepoint
int CodeMachine = 0;
int Mode = 2;
String userID = "ai_new";  // OTA → fw/ai_new/
// String userID = "ai_new_hier";
String Noserial = "65M000000";


// sensor thresholds (new)
int ldr_set = 3000;
int ldrMinus = 1000;

// mqtt port start (new)
int mqtt_port1 = 4741; // เลข port

// fw info (new)
const char *fwversion[] = {"Current Firmware\r\n", "Version 3.65\r\n"};
// const char *fwversion[] = {"Current Firmware\r\n", "Version 3.00\r\n"};

#endif

// ======================= Shared configuration (same both boards) =======================

// Legacy EEPROM layout (firmware เก่า — ย้าย ID ครั้งแรกเมื่อ NVS ยังไม่มี Noserial)
#define EEPROM_SIZE 155
#define EEPROM_ID_ADDR 70
#define EEPROM_SSID_ADDR 82
#define EEPROM_PASS_ADDR (EEPROM_SSID_ADDR + 24)
#define EEPROM_IDSHAREPOINT_ADDR (EEPROM_PASS_ADDR + 24)
#define EEPROM_GID_ADDR (EEPROM_IDSHAREPOINT_ADDR + 8)

// ค่าหนึ่งเหรียญ (บาท) — แก้จากแดชบอร์ดหรือ config MQTT ได้
int coinValue = 10;
/** นาทีที่เพิ่มต่อ 1 pulse ต่อเวลาอบ (1 pulse = coinValue บาท) — ตรง count_update minn+=10 */
#define DRY_EXTEND_MIN_PER_COIN 10

// esp32time
const char *ntpServer = "1.th.pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0 * 3600;

// price
// volatile bool pulse_trigger_flag = false;
int total_money = 0;
int price[] = {30, 40, 50};
int pricePro[] = {40, 50, 60};
int PriceShow[] = {30, 40, 50};
int item_price = 0;
int pendingBalance = 0;   // ยอดรายรับที่ยังส่งขึ้นเซิร์ฟเวอร์ไม่สำเร็จ

// โปรโมชั่นแบบหลายช่วงเวลา/หลายวัน
struct PromoSlot {
  uint8_t day;       // 0=Sun..6=Sat (ตาม rtc.getDayofWeek())
  uint8_t startHour; // 0-23
  uint8_t startMin;  // 0-59
  uint8_t endHour;   // 0-23
  uint8_t endMin;    // 0-59
  int pricePro[3];   // ราคาโปร 3 โปรแกรมเฉพาะช่วงนี้
};

const int MAX_PROMO_SLOTS = 10;
PromoSlot promoSlots[MAX_PROMO_SLOTS];
int promoSlotCount = 0;
int chanelPay = 0;
// bool chanelcoinStatus = false;
int minn_countdown_wait = 0;
int second_countdown_wait = 0;
bool status_countdown_wait = false;
bool status_machine_run = false;
bool status_machine_prepare = false;
int program = 0;
int value_str1_int;

// mqtt
const char *mqtt_server;
int mqtt_port;
const char *mqtt_server1 = "mawell.thddns.net";  // server
const char *mqtt_server2 = "broker.mqtt.cool";   // server
const char *mqtt_username = "mawell";                  // replace with your Username
const char *mqtt_password = "4460";                  // replace with your Password
int mqtt_port2 = 1883;                           // เลข port

// topic depends on gid (already set per-board above)
String topic = "V" + String(gid);

// qr payment
bool statusqr = true;
String cm;
String value_str1;
String value_str2;
String idSql;

// mills()
unsigned long timerstanby;

// var global
int chanel = 0;
String StatusControl;
int Screen = 1;
int mqttStatus = 1;

// var timer
int hrs = 0;
int minn = 0;
int second = 0;
int step = 0;
bool pause_timer = false;
int count_minn_pass = 0;

// var program // lg 11kg
int program1[] = {3, 1, 0}; //step1
int program2[] = {3, 0, 0}; //step1
int program3[] = {4, 2, 1}; //step1
int rinStep2[] = {5, 1};    //rin step2
int rincommand[] = {6, 1};  //rin command
int spin = 1;               //step3
int spinHier = 4;           //step3

int drum[] = {7, 0, 0}; // drum only

int check_runing_time[] = {21, 13, 5}; // Time off Quick Program

int TimeCountdowndrum[] = {2, 07};
int TimeCountdown1[] = {0, 31};
int TimeCountdown2[] = {0, 31};
int TimeCountdown3[] = {0, 31};

// var dry
int TimerA;
int timerDry[] = {30, 40, 50};

// var reset
bool endProgram = false;

// var error
int state_error = 0;

// admin
// const char * passAdmin  = "4460";
String passAdmin = "4460";
bool stateChange_passAdmin = false;

// setting
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
// bool statewifiOff = true; // เคยใช้ในบอร์ดเก่า ปัจจุบันไม่ได้ใช้โดยตรงในโค้ด

// var oti (shared) — ชี้ไป MelodyWebapp backend (ดู docs/OTA-FIRMWARE.md)
// server/port อ่านจาก Preferences หรือ configResponse; ค่าเริ่มต้นด้านล่าง (แบบที่ 1 Cloudflare / แบบที่ 2 เซิร์ฟเวอร์เดิม)
bool UpdateFw = false;
String Path_GetVersion = "/ota/version";
String Path_OTI = "/ota/download";
/** MelodyWebapp — ESP poll เมื่อ MQTT ล่ม (รายงาน + รับคำสั่ง OTA) */
String Path_MqttReport = "/public/machines/mqtt-report";
String Path_DeviceAck = "/public/machines/device-ack";
String Path_UpdateState = "/public/machines/update-state";
String api_key = "cf860590807a21db3be15ae3f99f706b";
String server = "backend.ma-well.com"; // Melody OTA (Cloudflare → backend)
String host = server;
int port = 80;
long contentLength = 0;
bool isValidContentType = false;
// ดึง config ใช้ MQTT (configRequest/configResponse) เท่านั้น ถ้าไม่มีข้อมูลใช้ค่าจากโรงงาน
String ServerSentBalanceV3 = "https://prod-10.southeastasia.logic.azure.com:443/workflows/e2fb5b61149c46bf93c39badf52cfacf/triggers/manual/paths/invoke?api-version=2016-06-01&sp=%2Ftriggers%2Fmanual%2Frun&sv=1.0&sig=MneSpDjmFs8LZpsz0DhH0A-M3uDP9XXrjZA83Yb11j4";
bool stateGetdata = 0;
bool stateSetupdata = 0;
bool stateUpdateState = 0;
bool stateSentPriceServer = 0;
String TimeSent;
// รับ/ส่ง config ผ่าน MQTT (MelodyWebapp)
String mqttPayloadBuffer;       // เก็บ payload จาก MQTT เมื่อรับ setup
bool stateSendConfigMqtt = false;  // เมื่อรับ getdata ให้ส่ง config กลับทาง MQTT (topic getdataResponse)
unsigned long getdataDisplayUntil = 0;  // แสดง "PC" บนจอเมื่อแอดมินส่ง getdata (จนถึง millis นี้)
// ส่ง status (UpdateState) ทุก N นาที ขณะเครื่องทำงาน — ปรับได้จากฟอร์ม ESP32 (ค่าเริ่มต้น 5)
int statusReportIntervalMinutes = 5;

// tm1637 scroll text
unsigned long previousMillis = 0;
const long interval = 500; // ระยะเวลาต่อการเลื่อนข้อความ (500ms)
int textIndex = 0;
uint8_t buffer[4] = {0, 0, 0, 0};
int runCount = 0; // นับรอบการวิ่ง
int statedisplaystandby = 0;
bool scrolling = false;

// button test
bool test = false;
bool stateReset = false;
String data;
bool statePriceShow = false;

bool drain_water = false;
int stepHier = 0;

// state use wifi or not
bool state_wifi_on = true;
// timer set mode
static unsigned long timerSetMode = millis();
// static unsigned long timerIncreateDry = millis();
int chanelCommand = 0;           // ตัวแปรเก็บคำสั่งที่ส่งไปยังเครื่อง
bool stateLdrOpen = false;       // ตัวแปรเก็บสถานะการเปิด LDR
bool stateWhile = false;
bool stateCheckLdr1 = false;     // ตัวแปรเก็บสถานะการตรวจสอบ LDR
bool stateCheckLdr2 = false;     // ตัวแปรเก็บสถานะการตรวจสอบ LDR
int chanelLdrCheck = 0;          // ตัวแปรเก็บช่อง LDR ที่ต้องตรวจสอบ
int stepLdrCheck = 0;            // ตัวแปรเก็บค่า LDR ที่ต้องตรวจสอบ
int displaystandbyLdrCheck = 0;  // ตัวแปรเก็บสถานะการแสดงผล LDR
bool stateUpdateBalanceDry = false;

