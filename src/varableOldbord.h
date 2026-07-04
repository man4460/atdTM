
#define OldBoard 1 // กำหนดว่าเป็นบอร์ดเก่าหรือใหม่ ถ้าเป็นบอร์ดเก่าให้ใส่ 1 ถ้าเป็นบอร์ดใหม่ให้ใส่ 0
#define WiFi_TIMEOUT_MS 20000
//tm 1637
#define CLK  16  // ขาต่อกับ CLK ของ TM1637
#define DIO  17  // ขาต่อกับ DIO ของ TM1637
//coin
#define SIG_PIN    22 // กำหนดขาที่ต่อ SIG (RX)
#define SIG_PIN2   23 // กำหนดขาที่ต่อ SIG (RX)
#define EN_PIN     2 // กำหนดขาที่ต่อ EN
#define COIN_VALUE 10 // กำหนดเหรียญที่ใส่ในเครื่อง เพื่อให้คำนวณยอดเงินถูกต้อง


#define wifiLed 2
// #define TX_PIN     40 // กำหนดขา TX

//relay
// #define IO_ADDR (0x21)

//ldr Old Board
#define LDR1_PIN    34
#define LDR2_PIN    35
int ldrPin = LDR1_PIN;

// int relay_pin[] = { 14, 13, 26, 12, 27, 21, 27 }; // old dry board andaman
int relay_pin[] = { 27, 13, 26, 12, 14, 21, 14 };
int sw_pin[] = { 15, 4, 5, 18, 19 };
const char * sw_name[] = { "BACK", "DOWN", "UP/TEST", "SETTING", "RESET" };

int pinSlot = SIG_PIN ;
int count = 0; // ตัวแปรนับ Pulse ที่เครื่องรับเหรียญส่งเข้ามา 
bool count_update_flag = false; // ตัวแปรเก็บค่าว่า count อัพเดทแล้ว
int coinPulse = 30;

//wifi setup
// String ssidStr = "man4460base_2.4G";
// String passStr = "Man0815418771";
// String ssidStr = "melody";
// String passStr = "0815418771";
String ssidStr = "ELe2.4G";
String passStr = "E2722023";
// String ssidStr = "Andaman";
// String passStr = "0632352964";
// String ssidStr = "LUCKY 1234_2.4G";
// String passStr = "adithep1234";

//esp32time
const char* ntpServer = "1.th.pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;
const int   daylightOffset_sec = 0 * 3600;
// ESP32Time rtc(0);  // offset in seconds GMT
// struct tm timeinfo;
// String mch_order_no = "";
// String mch_order_no_set ="";


//price
// volatile bool pulse_trigger_flag = false;
int total_money = 0;
int price[] = {30,40,50};
int pricePro[] = {40,50,60};
int PriceShow[] = {30,40,50};
int item_price = 0;
int chanelPay = 0; 
// bool chanelcoinStatus = false;
int minn_countdown_wait = 0;
int second_countdown_wait = 0;
bool status_countdown_wait = false;
bool status_machine_run = false;
bool status_machine_prepare = false;
int program = 0;
int dateState[] = {0, 0, 0, 0, 0, 0, 0};
int TimeStart[] = {0, 0};
int TimeEnd[] = {0, 0};
int value_str1_int;

//mqtt
const char* mqtt_server;
int mqtt_port;
const char* mqtt_server1 = "mawell.thddns.net"; // server
const char* mqtt_server2 = "broker.mqtt.cool"; // server
const char* mqtt_username = ""; // replace with your Username
const char* mqtt_password = ""; // replace with your Password
int mqtt_port1 = 4741; // broker หลัก Melody (4741)
int mqtt_port2 = 1883; // เลข port

int gid = 14;
String IDserver = "144";//ID for sharepoint
int CodeMachine = 1;
int Mode = 6;

String topic = "V" + String(gid);
// String userID = "ATD_Tm1637_V3_Hier_New";
String userID = "Tm1637_V4_Old";
String Noserial = "66M140139";
// String Noserial = "65M000000";


//qr payment
bool statusqr = true;
String cm;
String value_str1;
String value_str2;
String idSql;
// lv_obj_t * qrcode;
// int value_int;

//mills()
unsigned long timerstanby;

//var global
int chanel = 0;
// int Mode = 2;
// int Cost[] = {50,60,70};
// int hrs; int minn;
String StatusControl;
int Screen = 1;
int mqttStatus = 1;

//var timer
int hrs = 0; int minn = 0; int second = 0;
int step = 0;
bool pause_timer = false;
int count_minn_pass = 0;

//var program // lg 11kg
int program1[] = {3,1,0}; //step1
int program2[] = {3,0,0}; //step1
int program3[] = {4,2,1}; //step1
int rinStep2[] = {5,1}; //rin step2
int rincommand[] = {6,1}; //rin command
int spin = 1; //step3
int spinHier = 4; //step3

int drum[] = {7, 0, 0}; // drum only

int check_runing_time[] = {21,13,5}; // Time off Quick Program

int TimeCountdowndrum[] = {2,07};
int TimeCountdown1[] = {0,31};
int TimeCountdown2[] = {0,31};
int TimeCountdown3[] = {0,31}; 

//var dry
int TimerA;
int timerDry[] = {30, 40, 50};

// sensor
int ldr_set = 1500;

// var reset
bool endProgram = false;

//var error
int state_error = 0;

//admin
// const char * passAdmin  = "4460";
String passAdmin  = "4460";
bool stateChange_passAdmin  = false;

//setting
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
bool statewifiOff = true; 

//var oti
const char* fwversion[] = {"Current Firmware\r\n", "Version 4.02\r\n"};
bool UpdateFw = false;
String Path_GetVersion = "/ota/version";
String Path_OTI = "/ota/download";
String api_key = "cf860590807a21db3be15ae3f99f706b";
String server = "mawell.thddns.net";
String host = server;
int port = 4746;
long contentLength = 0;
bool isValidContentType = false;
String ServerSetupdataV3 = "https://prod-62.southeastasia.logic.azure.com:443/workflows/b7610eed8c0c4e95a2e747abd42fc621/triggers/manual/paths/invoke?api-version=2016-06-01&sp=%2Ftriggers%2Fmanual%2Frun&sv=1.0&sig=-UDl1Sy0eXa23lV3pt73QpjoiEQ7j3-lYQZBU86nfLg";
String ServerGetdataV3 = "https://prod-12.southeastasia.logic.azure.com:443/workflows/a312f033c1494458b9b43fd4ff687bd2/triggers/manual/paths/invoke?api-version=2016-06-01&sp=%2Ftriggers%2Fmanual%2Frun&sv=1.0&sig=FN54Z_WWPQL9tymcKHtaA8dJWFyyco6y7pLHX4tfq8c";
String ServerSentBalanceV3 = "https://prod-10.southeastasia.logic.azure.com:443/workflows/e2fb5b61149c46bf93c39badf52cfacf/triggers/manual/paths/invoke?api-version=2016-06-01&sp=%2Ftriggers%2Fmanual%2Frun&sv=1.0&sig=MneSpDjmFs8LZpsz0DhH0A-M3uDP9XXrjZA83Yb11j4";
bool stateGetdata = 0;
bool stateSetupdata = 0;
bool stateUpdateState = 0;
bool stateSentPriceServer = 0;
String TimeSent;

// int priceSentVerver = 0;

//tm1637 scroll text
unsigned long previousMillis = 0;
const long interval = 500;  // ระยะเวลาต่อการเลื่อนข้อความ (500ms)
int textIndex = 0;
uint8_t buffer[4] = {0, 0, 0, 0};
int runCount = 0;  // นับรอบการวิ่ง
int statedisplaystandby = 0;
bool scrolling = false;

//button test
bool test = false;
bool stateReset = false;
String data;
bool statePriceShow = false;

static bool drain_water = false;
static int stepHier = 0;
int ldrMinus = 500; // ตัวแปรเก็บค่า LDR ที่ใช้ในการลบจากค่า LDR หลัก

//state use wifi or not
bool state_wifi_on = true;
//timer set mode
static unsigned long timerSetMode = millis();
// static unsigned long timerIncreateDry = millis();
int chanelCommand = 0; // ตัวแปรเก็บคำสั่งที่ส่งไปยังเครื่อง
bool stateLdrOpen = false; // ตัวแปรเก็บสถานะการเปิด LDR
bool stateWhile = false;
bool stateCheckLdr1 = false; // ตัวแปรเก็บสถานะการตรวจสอบ LDR
bool stateCheckLdr2 = false; // ตัวแปรเก็บสถานะการตรวจสอบ LDR
int chanelLdrCheck = 0; // ตัวแปรเก็บช่อง LDR ที่ต้องตรวจสอบ
int stepLdrCheck = 0; // ตัวแปรเก็บค่า LDR ที่ต้องตรวจสอบ
int displaystandbyLdrCheck = 0; // ตัวแปรเก็บสถานะการแสดงผล LDR
bool stateUpdateBalanceDry = false;
