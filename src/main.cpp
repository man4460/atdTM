#include <main.h>
#include <Arduino.h>
// #include <ATD3.5-S3.h>
#include <ESP32Time.h>
#include <WiFi.h>
#include "esp_pm.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
// #include <Wire.h>
// unified variables for both old/new boards
#include "varable.h"
#include "ldr_sampler.h"
#include "run_session.h"
#include <EEPROM.h>
#include <Update.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TM1637Display.h>
#include "tm1637.h"
#include <freertos/task.h>

static TaskHandle_t taskProgram_handle = NULL;
static TaskHandle_t taskDisplay_handle = NULL;
static TaskHandle_t taskWifiMqtt_handle = NULL;

// --- Task-hang watchdog: แต่ละ task อัปเดต heartbeat; loop() รีบูทถ้า task ค้างนานเกิน ---
static volatile unsigned long hbDisplayMs = 0;
static volatile unsigned long hbProgramMs = 0;
static volatile unsigned long hbWifiMs = 0;
static volatile bool otaInProgress = false;  // true ระหว่างดาวน์โหลด OTA (task wifi บล็อกได้นาน)
static const unsigned long TASK_HANG_TIMEOUT_MS = 120000UL;       // display/program ค้าง > 2 นาที = รีบูท
static const unsigned long WIFI_TASK_HANG_TIMEOUT_MS = 360000UL;  // wifi/mqtt ค้าง > 6 นาที (เผื่อ OTA)
static const unsigned long BOOT_HANG_GRACE_MS = 60000UL;          // งดเช็ค hang 60s หลัง boot (WiFi connect)
static const uint32_t LOW_HEAP_CRITICAL_BYTES = 8000UL;           // free heap ต่ำกว่านี้ = ใกล้หมด
static const unsigned long LOW_HEAP_REBOOT_MS = 60000UL;          // ต่ำต่อเนื่อง 1 นาที = รีบูทกันค้าง/crash

/** กัน false positive เมื่อ now < since (millis skew / race ตอน boot) */
static bool hangElapsedMs(unsigned long now, unsigned long since, unsigned long limitMs)
{
  if (since == 0 || now < since)
    return false;
  return (unsigned long)(now - since) > limitMs;
}

// Object
WiFiClient client;
PubSubClient mqclient(client);
/** ล็อก lwIP — ห้าม HTTP กับ MQTT พร้อมกัน (กัน assert pbuf_free บน ESP32) */
static SemaphoreHandle_t gNetMutex = nullptr;

static bool netLockEnter()
{
  if (!gNetMutex)
    return true;
  return xSemaphoreTakeRecursive(gNetMutex, pdMS_TO_TICKS(12000)) == pdTRUE;
}

static void netLockLeave()
{
  if (gNetMutex)
    xSemaphoreGiveRecursive(gNetMutex);
}

/** เรียกเมื่อถือ net lock อยู่แล้ว — รวมถึงจากใน MQTT callback */
static void mqttPumpLoopLocked(int rounds = 3)
{
  if (!mqclient.connected())
    return;
  for (int i = 0; i < rounds; i++)
  {
    mqclient.loop();
    vTaskDelay(1);
  }
}

// สถานะ MQTT online แบบ cache — อัปเดตเฉพาะใน taskWifiMqtt
// task อื่น (จอ) อ่านค่านี้แทนการเรียก mqclient.connected() ตรง ๆ
// เพราะ connected() เรียก recv() บน socket -> ถ้าชนกับ loop() ใน taskWifiMqtt = pbuf double-free crash
volatile bool g_mqttOnline = false;

WebServer wifiServer(80);
DNSServer dnsServer;

ESP32Time rtc(0); // offset in seconds GMT
struct tm timeinfo;

Preferences preferences;
TM1637Display display(CLK, DIO);

void setPriceShow();  // forward declaration (defined later)
void writePreferencesfirst();  // forward declaration (defined later)
static bool tryLoadIdentityFromEeprom();  // forward declaration (defined later)
/** คืนค่ารหัสที่แปลงจาก Noserial สำหรับแสดงบน AP/จอ (reversible ด้วย secret เดียวกับ backend เพื่อค้นหา id ได้) */
String getDisplayCodeFromNoserial();

/** แจ้ง Melody รหัสปัญหา 00/01/02 — ส่งผ่าน topic UpdateState (หรือ HTTP fallback) */
static void reportEspFaultToMelody(const char *code) {
  runSessionClear();
  StatusControl = code;
  stateUpdateState = 1;
  Serial.print(F("[Melody] fault -> UpdateState Status="));
  Serial.println(code);
}
/** ส่งสถานะ OTA ไป MQTT topic OtaStatus (phase, percent, message) — กำหนดไว้หลังในไฟล์ */
void sendOtaStatusMqtt(const char* phase, int percent, const char* message);
void PublishConfigViaMqtt();
void normalizeOtaServer();
void suspendMachineTasksForOta();
void restoreMachineTasksAfterOta();
void taskDisplay(void *parameter);
void taskProgram(void *parameter);
/** MQTT presence/LWT — topic presence/{Noserial} สำหรับ MelodyWebapp */
bool publishPresenceOnline();
void publishPresenceOfflineGraceful();
bool pollMelodyDeviceHttp();

// relay — ใช้ vTaskDelay แทน delay() เพื่อให้ task อื่น (WiFi/MQTT) ได้รัน ไม่ค้าง
void Power()
{
  digitalWrite(relay_pin[0], HIGH); // On
  if (Mode == 3 || Mode == 4)
  {
    vTaskDelay(pdMS_TO_TICKS(2500));
  }
  else
  {
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
  digitalWrite(relay_pin[0], LOW); // Off
  vTaskDelay(pdMS_TO_TICKS(500));
}
//setup hier
// void Power()
// {
//   digitalWrite(relay_pin[0], HIGH); // On
//   digitalWrite(relay_pin[5], HIGH); // On
//   vTaskDelay(pdMS_TO_TICKS(5000)); //setup
//   digitalWrite(relay_pin[0], LOW); // Off
//   digitalWrite(relay_pin[5], LOW); // Off
//   vTaskDelay(pdMS_TO_TICKS(500));
// }

void Start()
{
  digitalWrite(relay_pin[1], HIGH); // On
  vTaskDelay(pdMS_TO_TICKS(2000));
  digitalWrite(relay_pin[1], LOW); // Off
  vTaskDelay(pdMS_TO_TICKS(500));
}
void Temp()
{
  digitalWrite(relay_pin[4], HIGH); // On
  vTaskDelay(pdMS_TO_TICKS(700));
  digitalWrite(relay_pin[4], LOW); // Off
  vTaskDelay(pdMS_TO_TICKS(500));
}
void TempSpin()
{
  digitalWrite(relay_pin[4], HIGH); // On
  vTaskDelay(pdMS_TO_TICKS(5000));
  digitalWrite(relay_pin[4], LOW); // Off
  vTaskDelay(pdMS_TO_TICKS(500));
}
static bool state_jok = false;
void Jok()
{
  if ((Mode == 1 && CodeMachine == 4) || Mode == 3)
  {
    if (!state_jok)
    {
      digitalWrite(relay_pin[2], HIGH); // On
      vTaskDelay(pdMS_TO_TICKS(400));
      digitalWrite(relay_pin[3], HIGH); // On
      vTaskDelay(pdMS_TO_TICKS(500));
      state_jok = true;
    }
    else
    {
      digitalWrite(relay_pin[2], LOW); // Off
      vTaskDelay(pdMS_TO_TICKS(400));
      digitalWrite(relay_pin[3], LOW); // Off
      vTaskDelay(pdMS_TO_TICKS(500));
      state_jok = false;
    }
  }
  else
  {
    digitalWrite(relay_pin[2], HIGH); // On
    vTaskDelay(pdMS_TO_TICKS(400));
    digitalWrite(relay_pin[3], HIGH); // On
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(relay_pin[2], LOW); // Off
    vTaskDelay(pdMS_TO_TICKS(400));
    digitalWrite(relay_pin[3], LOW); // Off
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void JokBack()
{
  if ((Mode == 1 && CodeMachine == 4) || Mode == 3)
  {
    if (!state_jok)
    {
      digitalWrite(relay_pin[3], HIGH); // On
      vTaskDelay(pdMS_TO_TICKS(400));
      digitalWrite(relay_pin[2], HIGH); // On
      vTaskDelay(pdMS_TO_TICKS(500));
      state_jok = true;
    }
    else
    {
      digitalWrite(relay_pin[3], LOW); // Off
      vTaskDelay(pdMS_TO_TICKS(400));
      digitalWrite(relay_pin[2], LOW); // Off
      vTaskDelay(pdMS_TO_TICKS(500));
      state_jok = false;
    }
  }
  else
  {
    digitalWrite(relay_pin[3], HIGH); // On
    vTaskDelay(pdMS_TO_TICKS(400));
    digitalWrite(relay_pin[2], HIGH); // On
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(relay_pin[3], LOW); // Off
    vTaskDelay(pdMS_TO_TICKS(400));
    digitalWrite(relay_pin[2], LOW); // Off
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void Spin()
{
  digitalWrite(relay_pin[5], HIGH); // On
  vTaskDelay(pdMS_TO_TICKS(700));
  digitalWrite(relay_pin[5], LOW); // Off
  vTaskDelay(pdMS_TO_TICKS(500));
}
void DrumWash()
{
  digitalWrite(relay_pin[6], HIGH); // On
  vTaskDelay(pdMS_TO_TICKS(700));
  digitalWrite(relay_pin[6], LOW); // Off
  vTaskDelay(pdMS_TO_TICKS(500));
}
void Dry(bool state)
{
  if (state)
  {
    digitalWrite(relay_pin[6], HIGH);
  }
  else
  {
    digitalWrite(relay_pin[6], LOW);
  }
}
void Slot(bool state)
{
  if (state)
  {
    digitalWrite(EN_PIN, HIGH);
  }
  else
  {
    digitalWrite(EN_PIN, LOW);
  }
}
void ClearJok()
{
  digitalWrite(relay_pin[2], LOW); // On
  digitalWrite(relay_pin[3], LOW); // On
  state_jok = false;
}

void writePreferences()
{
  // บันทึกค่าตัวแปรลง Preferences
  // preferences.begin("config", false);  // สร้างพื้นที่เก็บข้อมูลชื่อ "config"

  // เปิด Preferences ในโหมดอ่านและเขียน
  if (!preferences.begin("config", false))
  {
    Serial.println("Failed to open preferences");
    return;
  }
  preferences.putInt("Mode", Mode);
  preferences.putInt("pinSlot", pinSlot);
  preferences.putInt("mqttStatus", mqttStatus);
  preferences.putString("IDserver", IDserver);
  // preferences.putInt("gid", gid);
  // Serial.println(" ===== put gid : " + String(gid) + " ======");
  preferences.putInt("CodeMachine", CodeMachine);
  yield();  // feed watchdog ขณะเขียน NVS (ป้องกัน TG1WDT)

  preferences.putInt("price[0]", price[0]);
  preferences.putInt("price[1]", price[1]);
  preferences.putInt("price[2]", price[2]);

  preferences.putInt("program1[0]", program1[0]);
  preferences.putInt("program1[1]", program1[1]);
  preferences.putInt("program1[2]", program1[2]);

  preferences.putInt("program2[0]", program2[0]);
  preferences.putInt("program2[1]", program2[1]);
  preferences.putInt("program2[2]", program2[2]);

  preferences.putInt("program3[0]", program3[0]);
  preferences.putInt("program3[1]", program3[1]);
  preferences.putInt("program3[2]", program3[2]);

  preferences.putInt("rinStep2[0]", rinStep2[0]);
  preferences.putInt("rinStep2[1]", rinStep2[1]);

  preferences.putInt("rincommand[0]", rincommand[0]);
  preferences.putInt("rincommand[1]", rincommand[1]);

  preferences.putInt("spin", spin);

  preferences.putInt("drum[0]", drum[0]);
  preferences.putInt("drum[1]", drum[1]);
  preferences.putInt("drum[2]", drum[2]);

  preferences.putInt("check_time[0]", check_runing_time[0]);
  preferences.putInt("check_time[1]", check_runing_time[1]);
  preferences.putInt("check_time[2]", check_runing_time[2]);
  yield();  // feed watchdog ขณะเขียน NVS

  preferences.putInt("Timedrum[0]", TimeCountdowndrum[0]);
  preferences.putInt("Timedrum[1]", TimeCountdowndrum[1]);

  preferences.putInt("Timedown1[0]", TimeCountdown1[0]);
  preferences.putInt("Timedown1[1]", TimeCountdown1[1]);

  preferences.putInt("Timedown2[0]", TimeCountdown2[0]);
  preferences.putInt("Timedown2[1]", TimeCountdown2[1]);

  preferences.putInt("Timedown3[0]", TimeCountdown3[0]);
  preferences.putInt("Timedown3[1]", TimeCountdown3[1]);

  preferences.putInt("timerDry[0]", timerDry[0]);
  preferences.putInt("timerDry[1]", timerDry[1]);
  preferences.putInt("timerDry[2]", timerDry[2]);

  preferences.putInt("ldr_set", ldr_set / 100);
  preferences.putInt("StateShutdown", StateShutdown);
  preferences.putInt("SetupData", SetupData);
  yield();  // feed watchdog ขณะเขียน NVS

  preferences.putInt("pricePro[0]", pricePro[0]);
  preferences.putInt("pricePro[1]", pricePro[1]);
  preferences.putInt("pricePro[2]", pricePro[2]);
  preferences.putInt("coinValue", coinValue);
  preferences.putString("otaServer", server);
  preferences.putInt("otaPort", port);

  // โปรโมชั่นหลายช่วง (setPromoSlots) — บันทึกเพื่อไม่หายหลังรีบูต
  preferences.putInt("psCnt", promoSlotCount);
  for (int i = 0; i < MAX_PROMO_SLOTS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ps%dd", i);
    preferences.putInt(key, promoSlots[i].day);
    snprintf(key, sizeof(key), "ps%dsH", i);
    preferences.putInt(key, (int)promoSlots[i].startHour);
    snprintf(key, sizeof(key), "ps%dsm", i);
    preferences.putInt(key, (int)promoSlots[i].startMin);
    snprintf(key, sizeof(key), "ps%deH", i);
    preferences.putInt(key, (int)promoSlots[i].endHour);
    snprintf(key, sizeof(key), "ps%dem", i);
    preferences.putInt(key, (int)promoSlots[i].endMin);
    snprintf(key, sizeof(key), "ps%dp0", i);
    preferences.putInt(key, promoSlots[i].pricePro[0]);
    snprintf(key, sizeof(key), "ps%dp1", i);
    preferences.putInt(key, promoSlots[i].pricePro[1]);
    snprintf(key, sizeof(key), "ps%dp2", i);
    preferences.putInt(key, promoSlots[i].pricePro[2]);
  }

  // preferences.putBool("statewifiOff", statewifiOff);
  preferences.putBool("state_wifi_on", state_wifi_on);

  Serial.println("✅ บันทึกข้อมูลลง Preferences หลักสำเร็จ!");

  preferences.end(); // ปิด Preferences
}

/** ตั้งค่าตัวแปร config เป็นค่าจากโรงงาน (เทียบเท่า varable.h) แล้วบันทึก — ใช้เมื่อขอ config ทาง MQTT แล้วไม่มีข้อมูล */
void applyFactoryDefaultsConfig() {
  price[0] = 30; price[1] = 40; price[2] = 50;
  pricePro[0] = 40; pricePro[1] = 50; pricePro[2] = 60;
  timerDry[0] = 30; timerDry[1] = 40; timerDry[2] = 50;
  program1[0] = 3; program1[1] = 1; program1[2] = 0;
  program2[0] = 3; program2[1] = 0; program2[2] = 0;
  program3[0] = 4; program3[1] = 2; program3[2] = 1;
  rinStep2[0] = 5; rinStep2[1] = 1;
  rincommand[0] = 6; rincommand[1] = 1;
  spin = 1;
  drum[0] = 7; drum[1] = 0; drum[2] = 0;
  check_runing_time[0] = 21; check_runing_time[1] = 13; check_runing_time[2] = 5;
  TimeCountdowndrum[0] = 2; TimeCountdowndrum[1] = 7;
  TimeCountdown1[0] = 0; TimeCountdown1[1] = 31;
  TimeCountdown2[0] = 0; TimeCountdown2[1] = 31;
  TimeCountdown3[0] = 0; TimeCountdown3[1] = 31;
  promoSlotCount = 0;
  coinValue = 10;
  mqttStatus = 1;
  Mode = 1;
  CodeMachine = 1;
  setRelayType();
  writePreferences();
  setPriceShow();
  Serial.println("✅ ใช้ค่าจากโรงงาน (ไม่มี config จากระบบ)");
}

/** หลัง boot countdown "0000" — สลับไป standby ทันที (กันจอค้าง 0000) */
static void primeBootStandbyDisplay()
{
  statedisplaystandby = 0;
  display.setSegments(SEG_STANBY_1);
}

// ใช้ตอน setup ก่อนอ่าน Preferences: กดปุ่ม SETTING = คืนค่าโรงงาน (เขียนค่าเริ่มต้นจาก varable.h ลง Preferences)
// ถ้าเครื่องลงทะเบียนแล้ว (มี Noserial ใน NVS) เก็บ ID/WiFi/gid ไว้ — กัน OTA จากส่วนกลางแล้วเผลอกด SETTING ตอน boot
void setupWaitAdminRestoreFactory()
{
  const unsigned long windowMs = 3000;
  const int btnRestore = 3;  // sw_pin[3] = SETTING
  Serial.println();
  Serial.println(">>> กดปุ่ม SETTING ใน 3 วินาที เพื่อคืนค่าโรงงาน (ข้ามได้)");
  display.showNumberDec(0, true, 4, 0);
  unsigned long t0 = millis();
  while (millis() - t0 < windowMs)
  {
    if (digitalRead(sw_pin[btnRestore]) == LOW)
    {
      while (digitalRead(sw_pin[btnRestore]) == LOW)
        vTaskDelay(pdMS_TO_TICKS(20));

      String savedNoserial, savedSsid, savedPass, savedIDserver;
      int savedGid = gid;
      bool keepIdentity = false;
      if (preferences.begin("config", true))
      {
        if (preferences.isKey("Noserial"))
        {
          savedNoserial = preferences.getString("Noserial", "");
          keepIdentity = savedNoserial.length() > 0;
        }
        if (keepIdentity)
        {
          savedSsid = preferences.getString("ssid", ssidStr);
          savedPass = preferences.getString("password", passStr);
          savedGid = preferences.getInt("gid", gid);
          savedIDserver = preferences.getString("IDserver", IDserver);
        }
        preferences.end();
      }

      if (keepIdentity)
      {
        // ใส่ตัวตนเดิมใน memory ก่อน write — ไม่ให้ putString("Noserial") เขียนทับด้วยค่า varable.h
        Noserial = savedNoserial;
        ssidStr = savedSsid;
        passStr = savedPass;
        gid = savedGid;
        IDserver = savedIDserver;
      }

      // คืนค่าโรงงาน (ราคา/โปรแกรม ฯลฯ) — Noserial ใน memory ถูกต้องแล้วถ้า keepIdentity
      writePreferencesfirst();
      yield();
      writePreferences();
      runSessionClear();

      if (keepIdentity)
      {
        Serial.println(">>> ✅ คืนค่าโรงงานแล้ว (เก็บ ID/WiFi/gid เดิม: " + Noserial + ")");
      }
      else
      {
        Serial.println(">>> ✅ คืนค่าโรงงานแล้ว (Preferences หลัก + first)");
      }
      display.setSegments((const uint8_t *)"\x6d\x77\x00\x00", 4, 0);  // "SA" = บันทึกแล้ว
      vTaskDelay(pdMS_TO_TICKS(800));
      display.setSegments(off);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(80));
  }
  Serial.println(">>> ไม่กดปุ่ม บรรทัดถัดไป");
  primeBootStandbyDisplay();
}

void readPreferences()
{
  // เริ่มต้น Preferences ในโหมดอ่านอย่างเดียว
  // preferences.begin("config", true);

  // เปิด Preferences ในโหมดอ่านและเขียน
  if (!preferences.begin("config", true))
  {
    Serial.println("Failed to open preferences");
    return;
  }

  // อ่านค่าตัวแปรจาก Preferences
  Mode = preferences.getInt("Mode", Mode);
  pinSlot = preferences.getInt("pinSlot", pinSlot);
  mqttStatus = preferences.getInt("mqttStatus", mqttStatus);
  IDserver = preferences.getString("IDserver", IDserver);
  // gid = preferences.getInt("gid", gid);
  Serial.println(" ===== get gid : " + String(gid) + " ======");
  CodeMachine = preferences.getInt("CodeMachine", CodeMachine);

  price[0] = preferences.getInt("price[0]", price[0]);
  price[1] = preferences.getInt("price[1]", price[1]);
  price[2] = preferences.getInt("price[2]", price[2]);
  program1[0] = preferences.getInt("program1[0]", program1[0]);
  program1[1] = preferences.getInt("program1[1]", program1[1]);
  program1[2] = preferences.getInt("program1[2]", program1[2]);
  program2[0] = preferences.getInt("program2[0]", program2[0]);
  program2[1] = preferences.getInt("program2[1]", program2[1]);
  program2[2] = preferences.getInt("program2[2]", program2[2]);
  program3[0] = preferences.getInt("program3[0]", program3[0]);
  program3[1] = preferences.getInt("program3[1]", program3[1]);
  program3[2] = preferences.getInt("program3[2]", program3[2]);
  rinStep2[0] = preferences.getInt("rinStep2[0]", rinStep2[0]);
  rinStep2[1] = preferences.getInt("rinStep2[1]", rinStep2[1]);
  rincommand[0] = preferences.getInt("rincommand[0]", rincommand[0]);
  rincommand[1] = preferences.getInt("rincommand[1]", rincommand[1]);
  spin = preferences.getInt("spin", spin);
  drum[0] = preferences.getInt("drum[0]", drum[0]);
  drum[1] = preferences.getInt("drum[1]", drum[1]);
  drum[2] = preferences.getInt("drum[2]", drum[2]);
  check_runing_time[0] = preferences.getInt("check_time[0]", check_runing_time[0]);
  check_runing_time[1] = preferences.getInt("check_time[1]", check_runing_time[1]);
  check_runing_time[2] = preferences.getInt("check_time[2]", check_runing_time[2]);
  TimeCountdowndrum[0] = preferences.getInt("Timedrum[0]", TimeCountdowndrum[0]);
  TimeCountdowndrum[1] = preferences.getInt("Timedrum[1]", TimeCountdowndrum[1]);
  TimeCountdown1[0] = preferences.getInt("Timedown1[0]", TimeCountdown1[0]);
  TimeCountdown1[1] = preferences.getInt("Timedown1[1]", TimeCountdown1[1]);
  TimeCountdown2[0] = preferences.getInt("Timedown2[0]", TimeCountdown2[0]);
  TimeCountdown2[1] = preferences.getInt("Timedown2[1]", TimeCountdown2[1]);
  TimeCountdown3[0] = preferences.getInt("Timedown3[0]", TimeCountdown3[0]);
  TimeCountdown3[1] = preferences.getInt("Timedown3[1]", TimeCountdown3[1]);
  timerDry[0] = preferences.getInt("timerDry[0]", timerDry[0]);
  timerDry[1] = preferences.getInt("timerDry[1]", timerDry[1]);
  timerDry[2] = preferences.getInt("timerDry[2]", timerDry[2]);
  ldr_set = preferences.getInt("ldr_set", ldr_set) * 100; // แปลงกลับเป็นหน่วยที่ต้องการ
  StateShutdown = preferences.getInt("StateShutdown", StateShutdown);
  SetupData = preferences.getInt("SetupData", SetupData);
  pricePro[0] = preferences.getInt("pricePro[0]", pricePro[0]);
  pricePro[1] = preferences.getInt("pricePro[1]", pricePro[1]);
  pricePro[2] = preferences.getInt("pricePro[2]", pricePro[2]);
  coinValue = preferences.getInt("coinValue", 10);
  if (coinValue < 1) coinValue = 10;
  server = preferences.getString("otaServer", server);
  port = preferences.getInt("otaPort", port);
  normalizeOtaServer();
  host = server;

  // โปรโมชั่นหลายช่วง (setPromoSlots) — โหลดจาก Preferences หลังรีบูต
  promoSlotCount = preferences.getInt("psCnt", 0);
  if (promoSlotCount > MAX_PROMO_SLOTS) promoSlotCount = MAX_PROMO_SLOTS;
  for (int i = 0; i < MAX_PROMO_SLOTS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ps%dd", i);
    promoSlots[i].day = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%dsH", i);
    promoSlots[i].startHour = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%dsm", i);
    promoSlots[i].startMin = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%deH", i);
    promoSlots[i].endHour = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%dem", i);
    promoSlots[i].endMin = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%dp0", i);
    promoSlots[i].pricePro[0] = preferences.getInt(key, pricePro[0]);
    snprintf(key, sizeof(key), "ps%dp1", i);
    promoSlots[i].pricePro[1] = preferences.getInt(key, pricePro[1]);
    snprintf(key, sizeof(key), "ps%dp2", i);
    promoSlots[i].pricePro[2] = preferences.getInt(key, pricePro[2]);
  }

  state_wifi_on = preferences.getBool("state_wifi_on", state_wifi_on);

  Serial.println("✅ อ่านข้อมูลจาก Preferences ** หลักสำเร็จ!");

  // ปิด Preferences
  preferences.end();
}
static String readStrFromEEPROM(int addr)
{
  String str = "";
  const int endbuf = addr + 24;
  char ch = 0;
  while (ch != '\n' && addr < endbuf)
  {
    ch = (char)EEPROM.read(addr);
    if (ch == '\0')
      break;
    str += ch;
    addr++;
  }
  str.trim();
  return str;
}

static bool eepromSlotLooksValid(int addr)
{
  const uint8_t b = EEPROM.read(addr);
  return b != 0xFF && b != 0x00 && b != '\n';
}

/** อ่าน Noserial/ssid/pass/gid จาก EEPROM แบบ firmware เก่า — คืน true ถ้ามี Noserial */
static bool tryLoadIdentityFromEeprom()
{
  if (!eepromSlotLooksValid(EEPROM_ID_ADDR))
    return false;

  const String id = readStrFromEEPROM(EEPROM_ID_ADDR);
  if (id.length() == 0)
    return false;

  Noserial = id;
  if (eepromSlotLooksValid(EEPROM_SSID_ADDR))
  {
    const String s = readStrFromEEPROM(EEPROM_SSID_ADDR);
    if (s.length() > 0)
      ssidStr = s;
  }
  if (eepromSlotLooksValid(EEPROM_PASS_ADDR))
  {
    const String p = readStrFromEEPROM(EEPROM_PASS_ADDR);
    if (p.length() > 0)
      passStr = p;
  }
  if (eepromSlotLooksValid(EEPROM_GID_ADDR))
  {
    const String g = readStrFromEEPROM(EEPROM_GID_ADDR);
    if (g.length() > 0)
      gid = g.toInt();
  }
  return true;
}

void writePreferencesfirst()
{
  // บันทึกค่าตัวแปรลง Preferences
  // preferences.begin("config", false);  // สร้างพื้นที่เก็บข้อมูลชื่อ "config"

  // เปิด Preferences ในโหมดอ่านและเขียน
  if (!preferences.begin("config", false))
  {
    Serial.println("Failed to open preferences");
    return;
  }

  preferences.putString("Noserial", Noserial);
  preferences.putString("ssid", ssidStr);
  preferences.putString("password", passStr);
  preferences.putInt("gid", gid);
  Serial.println("✅ บันทึกข้อมูลลง Preferencesfirst สำเร็จ!");

  preferences.end(); // ปิด Preferences
}
void readPreferencesfirst()
{
  // preferences.begin("config", true);  // เปิด Preferences ในโหมดอ่านอย่างเดียว

  // เปิด Preferences ในโหมดอ่านและเขียน
  if (!preferences.begin("config", true))
  {
    Serial.println("Failed to open preferences");
    return;
  }

  Noserial = preferences.getString("Noserial", Noserial);
  ssidStr = preferences.getString("ssid", ssidStr);
  passStr = preferences.getString("password", passStr);
  gid = preferences.getInt("gid", gid);
  // Noserial = preferences.getString("Noserial");
  // ssidStr  = preferences.getString("ssid");
  // passStr  = preferences.getString("password");
  // gid = preferences.getInt("gid");
  Serial.println(" ===== Noserial : " + Noserial + " ====== ");
  Serial.println(" ===== ssid : " + ssidStr + " ====== ");
  Serial.println(" ===== password : " + passStr + " ====== ");
  Serial.println(" ===== gid : " + String(gid) + " ====== ");
  Serial.println("✅ อ่านข้อมูลจาก Preferencesfirst ** สำเร็จ!");
  preferences.end(); // ปิด Preferences
}

void setProgram()
{
  shootTemp();
  if (program == 1)
  {
    Serial.println("program 1 is running");
    for (int i = 0; i < program1[0]; i++)
    { // select program machine
      if (program1[2] == 1)
      {
        JokBack();
      }
      else
      {
        Jok();
      }
    }
    for (int i = 0; i < program1[1]; i++)
    { // select temp
      Temp();
    }
    hrs = TimeCountdown1[0];
    minn = TimeCountdown1[1];
    second = 0;
    state_step2 = false;
    state_step3 = false;
  }
  else if (program == 2)
  {
    Serial.println("program 2 is running");
    for (int i = 0; i < program2[0]; i++)
    { // select program machine
      if (program2[2] == 1)
      {
        JokBack();
      }
      else
      {
        Jok();
      }
    }
    for (int i = 0; i < program2[1]; i++)
    { // select temp
      Temp();
    }
    hrs = TimeCountdown2[0];
    minn = TimeCountdown2[1];
    second = 0;
    state_step2 = false;
    state_step3 = false;
  }
  else if (program == 3)
  {
    Serial.println("program 3 is running");
    for (int i = 0; i < program3[0]; i++)
    { // select program machine
      if (program3[2] == 1)
      {
        JokBack();
      }
      else
      {
        Jok();
      }
    }
    for (int i = 0; i < program3[1]; i++)
    { // select temp
      Temp();
    }
    hrs = TimeCountdown3[0];
    minn = TimeCountdown3[1];
    second = 0;
    state_step2 = false;
    state_step3 = false;
  }
  else if (program == 4)
  { // drum wash
    Serial.println("drum wash is running");
    for (int i = 0; i < drum[0]; i++)
    { // select program machine
      if (Mode == 3 || Mode == 4)
      {
        JokBack();
      }
      else
      {
        if (CodeMachine == 4)
        {
          DrumWash();
        }
        else
        {
          Jok();
        }
      }
    }
    state_step2 = true;
    state_step3 = true;
    hrs = TimeCountdowndrum[0];
    minn = TimeCountdowndrum[1];
    second = 0;
  }
  else if (program == 5)
  { // rin command
    Serial.println("program rin is running");
    if (rincommand[1] == 1)
    {
      for (int i = 0; i < rincommand[0]; i++)
      { // select program machine
        if (Mode == 3 || Mode == 4)
        { // for Hier
          Jok();
        }
        else
        {
          if (CodeMachine == 4)
          { // for 15kgNew
            JokBack();
          }
          else
          {
            Jok();
          }
        }
      }
      for (int i = 0; i < spin; i++)
      { // select program machine
        Spin();
      }
      state_step2 = true;
      state_step3 = true;
    }
    else if (rincommand[1] == 2)
    {
      for (int i = 0; i < rincommand[0]; i++)
      { // select program machine
        if (Mode == 3 || Mode == 4)
        { // for Hier
          Jok();
        }
        else
        {
          if (CodeMachine == 4)
          { // for 15kgNew
            JokBack();
          }
          else
          {
            Jok();
          }
        }
      }
      state_step2 = true;
      state_step3 = false;
    }
    hrs = 0;
    minn = check_runing_time[0];
    second = 0;
    step = 2;
  }
  else if (program == 6)
  { // spin command
    Serial.println("program spin is running");
    if (Mode == 3 || Mode == 4)
    { // for Hier
      for (int i = 0; i < spinHier; i++)
      { // select program machine
        JokBack();
      }
    }
    else
    {
      if (CodeMachine == 4)
      { // for 15kgNew
        TempSpin();
      }
    }
    for (int i = 0; i < spin; i++)
    { // select program machine
      Spin();
    }
    hrs = 0;
    minn = check_runing_time[1];
    second = 0;
    state_step2 = true;
    state_step3 = true;
    step = 3;
  }

  stateUpdateState = 1;
}

// ISR ต้องสั้นมาก ไม่ block ไม่ใช้ millis() — ถ้ารอใน ISR จะทำให้ Interrupt WDT timeout / stack เสีย
static volatile bool s_pulse_falling_seen = false;

void IRAM_ATTR pulse_in_cb()
{
  s_pulse_falling_seen = true;
}

// วัดความกว้าง pulse (เวลาที่ pin อยู่ LOW) ทำใน loop หลัก ไม่ทำใน ISR
static void checkPulseDuration()
{
  if (!s_pulse_falling_seen)
    return;
  s_pulse_falling_seen = false;
  unsigned long t0 = millis();
  const unsigned long timeoutMs = 80;
  while (!digitalRead(pinSlot) && (millis() - t0 < timeoutMs))
    vTaskDelay(pdMS_TO_TICKS(1));
  unsigned long duration = millis() - t0;
  if (duration >= (unsigned long)coinPulse)
    count_update_flag = true;
}

static void addDryMinutes(int minutes)
{
  if (minutes <= 0)
    return;
  minn += minutes;
  while (minn >= 60)
  {
    hrs++;
    minn -= 60;
  }
}

void setStartMachine(int dryFirstPaymentBaht = 0)
{
  if (Mode == 2)
  {
    Serial.println("Dry is runing..");
    if (program == 1)
    {
      minn = timerDry[0]+1;
    }
    else if (program == 2)
    {
      minn = timerDry[1]+1;
    }
    else if (program == 3)
    {
      minn = timerDry[2]+1;
    }
    second = 0;
    if (minn >= 60)
    {
      hrs++;
      minn = minn - 60;
    }
    if (dryFirstPaymentBaht > 0 && program >= 1 && program <= 3)
    {
      const int tierPrice = PriceShow[program - 1];
      if (tierPrice > 0 && dryFirstPaymentBaht > tierPrice && coinValue > 0)
      {
        const int overpay = dryFirstPaymentBaht - tierPrice;
        const int extraMin = (overpay / coinValue) * DRY_EXTEND_MIN_PER_COIN;
        addDryMinutes(extraMin);
        Serial.println("Dry overpay +" + String(extraMin) + " min (" + String(overpay) + " baht over tier " + String(tierPrice) + ")");
      }
    }
  }
  else
  {
    Serial.println("Wash is runing..");
    if (Mode == 3 || Mode == 4)
    {
      stepHier = 1; // first step Hier
    }
    // ตั้งเวลารวมตามโปรแกรมล่วงหน้า เพื่อให้ ack แรกแสดงเวลาพร้อม status (ตรงกับ setProgram)
    // นับถอยหลังจริงเริ่มเมื่อ status_machine_run = true เท่านั้น (machineRuning) จึงไม่ลดระหว่างเตรียม
    if (program == 1) { hrs = TimeCountdown1[0]; minn = TimeCountdown1[1]; second = 0; }
    else if (program == 2) { hrs = TimeCountdown2[0]; minn = TimeCountdown2[1]; second = 0; }
    else if (program == 3) { hrs = TimeCountdown3[0]; minn = TimeCountdown3[1]; second = 0; }
    else if (program == 4) { hrs = TimeCountdowndrum[0]; minn = TimeCountdowndrum[1]; second = 0; }
    else if (program == 5) { hrs = 0; minn = check_runing_time[0]; second = 0; }
    else if (program == 6) { hrs = 0; minn = check_runing_time[1]; second = 0; }
    // display.showNumberDecEx(0, 0b01000000, true, 4, 0);  // แสดง 00:00 (มีจุด)
  }
  statedisplaystandby = 3;
  chanel = 0;   // ออกจาก setting (BT4 → chanel 12) — อย่าให้ modeSetting ทับจอ timer
  step = 0;
  indexSet = 0;
  delay(100);
  display.showNumberDecEx(0, 0b01000000, true, 4, 0); // 3.32: โชว์ 00:00 (มีจุด) ทั้งอบ/ซัก
  status_machine_prepare = true;
  // ส่ง ack ทันทีที่รับคำสั่ง (Status=running, Time อาจเป็น 00:00 สำหรับเครื่องซัก)
  // เพื่อให้ Melody ขึ้น "กำลังทำงาน" เร็ว ไม่ต้องรอ setProgram (หลัง standby + power check)
  // เวลานับถอยหลังจริงจะตามมากับ UpdateState รอบสองตอน setProgram — ตรงกับ ATD35_Melody_V2
  stateUpdateState = true;
  timerstanby = millis();
  runSessionSavePhase(Mode == 2 ? RS_DRY_PREPARE : RS_WASH_PREPARE);
  // Serial.println("--------------------- : " + String(chanel));
}
void prepareRunMachine()     
{
  if (status_machine_prepare)
  {
    // Serial.println("*********************** : " + String(chanel));
    if (millis() - timerstanby >= 2000)
    {
      Serial.println("status_machine_prepare is true");
      status_countdown_wait = false;
      status_machine_prepare = false;
      // chanelcoinStatus = false;
      if (Mode == 2)
      {
        // state_error = 4; chanel = 11;
        status_machine_run = true;
        Dry(1);
        runSessionSavePhase(RS_DRY_RUNNING);
        // digitalWrite(EN_PIN, HIGH);
        if (OldBoard == 1)
        {
          Slot(0);
        }
        else
        {
          Slot(1);
        }
        // Slot(1);
      }
      else
      {
        chanel = 1;
        runSessionSavePhase(RS_WASH_STARTUP);
        display.setSegments(SEG_Mode);
        if (OldBoard == 1)
        {
          Slot(1);
        }
        else
        {
          Slot(0);
        }
        // Slot(0);
      }
    }
  }
}
void updateBalanceIncreateDry()
{
  if (stateUpdateBalanceDry){
    if(millis() - timerstanby >= 10000)
    {
      stateUpdateBalanceDry = false;
      // เพิ่มยอดรายรับจากการต่อเวลาอบเข้า buffer
      pendingBalance += item_price;
      stateSentPriceServer = 1; // ขอให้ taskWifiMqtt ส่งเมื่อออนไลน์
      item_price = 0;           // เคลียร์ยอดแสดงผล
    }
  }
}
void checkpriceprogram()
{
  // static unsigned long timeCheckmoney = millis();
  static bool stateCheckmoney = false;
  if (millis() - timerstanby >= 10000)
  {
    
    if ((item_price >= PriceShow[2] && PriceShow[2] != 0))
    {
      program = 3;
      stateCheckmoney = true;
        if (OldBoard == 1)
      {
        Slot(1);
      }
      else
      {
        Slot(0);
      }
    }
    else if ((item_price >= PriceShow[1] && PriceShow[1] != 0))
    {
      program = 2;
      stateCheckmoney = true;
      if (OldBoard == 1)
      {
        Slot(1);
      }
      else
      {
        Slot(0);
      }
    }
    else if ((item_price >= PriceShow[0] && PriceShow[0] != 0))
    {
      program = 1;
      stateCheckmoney = true;
      if (OldBoard == 1)
      {
        Slot(1);
      }
      else
      {
        Slot(0);
      }
    }

    if (stateCheckmoney)
    {
      stateCheckmoney = false;
      const int paidBaht = item_price;
      setStartMachine((Mode == 2 && program >= 1 && program <= 3) ? paidBaht : 0);
      if (!test)
      {
        // สะสมยอดรายรับไว้ในหน่วยความจำ ก่อนส่งขึ้นเซิร์ฟเวอร์
        pendingBalance += paidBaht;
        stateSentPriceServer = 1; // ขอให้ taskWifiMqtt ส่งเมื่อออนไลน์
        item_price = 0;           // เคลียร์ยอดแสดงผลต่อหน้าเครื่อง
      }
    }
  }
}
void count_update()
{
  checkPulseDuration();  // วัด pulse จาก flag ที่ ISR ตั้ง (ไม่ block ใน ISR)
  if (count_update_flag && !status_machine_run)
  {
    count_update_flag = false;
    test = false;
    count++; // เพิ่มค่าในตัวแปร count ขึ้น 1 ค่า
    item_price = item_price + (count * coinValue);
    // display.showNumberDec(item_price);
    statedisplaystandby = 1;
    Serial.println("item price = " + String(item_price));
    count = 0;
    timerstanby = millis();
  }
  else if (Mode == 2 && status_machine_run && count_update_flag)
  {
    count_update_flag = false;
    test = false;
    count++; // เพิ่มค่าในตัวแปร count ขึ้น 1 ค่า
    item_price = item_price + (count * coinValue);
    minn = minn + DRY_EXTEND_MIN_PER_COIN;
    if (minn >= 60)
    {
      hrs++;
      minn = minn - 60;
    }
    stateUpdateBalanceDry = true;
    // timerIncreateDry = millis();
    Serial.println("increat " + String(item_price) +" bath to " + String(hrs) + " : " + String(minn));
    count = 0;
    timerstanby = millis();
  }
  // delay(100);
}

void setupTime()
{
  // ตั้งค่าเวลาโดยใช้ NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  rtc.setTimeStruct(timeinfo);
  Serial.printf("Time sync OK: %04d-%02d-%02d %02d:%02d:%02d\n",
                rtc.getYear(), rtc.getMonth() + 1, rtc.getDay(),
                rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  delay(100);
}
void displayDateTime()
{
  // อัปเดตเวลาและแสดงผล
  Serial.println("Time is : " + String(rtc.getYear()) + "-" + String(rtc.getMonth() + 1) + "-" + String(rtc.getDay()) + " " + String(rtc.getHour(true)) + ":" + String(rtc.getMinute()) + ":" + String(rtc.getSecond()));
  // Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  // lv_label_set_text_fmt(ui_lb_datetime, "%04d-%02d-%02d %02d:%02d:%02d",
  //                       rtc.getYear(), rtc.getMonth()+1, rtc.getDay(),
  //                       rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
}
void secondMillis()
{
  static unsigned long timerdatetime = millis();
  if (millis() - timerdatetime >= 1000)
  {
    displayDateTime();
    timerdatetime = millis();
  }
  else if (millis() < timerdatetime)
  {
    timerdatetime = millis();
  }
}
void standbyDisplay()
{
  // เมื่อแอดมินส่ง getdata มา แสดง "PC" บนจอสัก 2.5 วินาที
  if (getdataDisplayUntil != 0 && millis() < getdataDisplayUntil) {
    display.setSegments(SEG_PC);
    return;
  }

  // stanby mode
  static unsigned long timeShowReady = millis();
  static bool showTMtype = false;
  static int showstanby = 0;
  static int indexCost = 0;
  if (millis() - timeShowReady >= 200)
  {
    if (showstanby <= 10)
    {
      if (showTMtype)
      {
        display.setSegments(SEG_STANBY_1);
        showTMtype = false;
      }
      else
      {
        display.setSegments(SEG_STANBY_2);
        showTMtype = true;
      }
      showstanby++;
    }
    else
    {
      // if(Mode == 2){
      //   display.showNumberDec(PriceShow[0],false,3,0);
      //   display.setSegments(SEG_Bath,1,3);
      //   showstanby++;
      // }else{
      if (indexCost >= 2 && PriceShow[2] != 0)
      {
        display.showNumberDec(PriceShow[2], false, 3, 0);
        display.setSegments(SEG_Bath, 1, 3);
        showstanby++;
      }
      else if (indexCost >= 1 && PriceShow[1] != 0)
      {
        display.showNumberDec(PriceShow[1], false, 3, 0);
        display.setSegments(SEG_Bath, 1, 3);
        showstanby++;
      }
      else if (indexCost >= 0)
      {
        display.showNumberDec(PriceShow[0], false, 3, 0);
        display.setSegments(SEG_Bath, 1, 3);
        showstanby++;
      }
      // }
      if (showstanby >= 20)
      {
        showstanby = 0;
        indexCost++;
        if (indexCost >= 3)
        {
          indexCost = 0;
        }
        if (PriceShow[0] == 0 && indexCost == 0)
        {
          indexCost++;
        }
        if (PriceShow[1] == 0 && indexCost == 1)
        {
          indexCost++;
        }
        if (PriceShow[2] == 0 && indexCost == 2)
        {
          indexCost = 0;
        }
        //   String data = Noserial +" gid : "+gid+" ==> update complete";
        //   mqclient.publish("completed",data.c_str()); // ชื่อ topic ที่ต้องการติดตาม
      }
    }
    timeShowReady = millis();
  }
  if (millis() < timeShowReady)
  {
    timeShowReady = millis();
  }
}

void setMc_no()
{
  String m = String(rtc.getMinute());
  String h = String(rtc.getHour(true));
  String d = String(rtc.getDay());
  String M = String(rtc.getMonth());
  String y = String(rtc.getYear());
  String msg2;
  if (m.length() < 2)
  {
    m = "0" + m;
  }
  if (h.length() < 2)
  {
    h = "0" + h;
  }
  if (d.length() < 2)
  {
    d = "0" + d;
  }
  if (M.length() < 2)
  {
    M = "0" + M;
  }
  if (y.length() < 2)
  {
    y = "0" + y;
  }
  msg2 = y + M + d + h + m;
}

bool shouldUpdate(unsigned long interval, unsigned long &lastTime) {
  unsigned long now = millis();
  if (now < lastTime || now - lastTime >= interval) {
    lastTime = now;
    return true;
  }
  return false;
}

/** แสดงเวลา hh:mm บน TM1637 (pos 0–1 = ชม., 2–3 = นาที) */
void updateWiFiIcon()
{
  static unsigned long timeIcon = 0;
  static bool toggle = false;

  if (WiFi.isConnected())
  {
    if (g_mqttOnline)  // อ่าน cache — ห้ามเรียก mqclient.connected() ใน task จอ (recv ซ้อน -> pbuf crash)
    {
      if (OldBoard == 0) {
        digitalWrite(wifiLed, HIGH);
      }
      display.setBrightness(7);
    }
    else
    {
      if (shouldUpdate(300, timeIcon))
      {
        if (OldBoard == 0) {
          digitalWrite(wifiLed, !digitalRead(wifiLed));
        }
        display.setBrightness(toggle ? 0 : 7);
        toggle = !toggle;
      }
    }
  }
  else
  {
    if (shouldUpdate(1000, timeIcon))
    {
      if (OldBoard == 0) {
        digitalWrite(wifiLed, !digitalRead(wifiLed));
      }
      display.setBrightness(toggle ? 0 : 7);
      toggle = !toggle;
    }
  }
}

void machineRuning()
{
  static unsigned long lastStatusReportMs = 0;
  if (!status_machine_run) {
    lastStatusReportMs = 0;
    return;
  }
  runSessionMaybeDryAutosave();
  {
    static unsigned long machine_runing_time = millis();
    if ((machine_runing_time == 0) || ((millis() < machine_runing_time) || ((millis() - machine_runing_time) > 1000)))
    {
      second--;
      if (second <= -1)
      {
        if (!pause_timer)
        {
          minn--; // timer is pause
        }

        second = 59;

        if (Mode == 2)
        {
          if (minn <= -1)
          {
            if (hrs >= 1)
            {
              hrs--;
              minn = 59;
            }
            else
            {
              // go to screen 1
              minn_countdown_wait = 1;
              second_countdown_wait = 0;
              status_countdown_wait = true;
              status_machine_run = false;
              endProgram = true;
              chanel = 10;
            }
          }
        }
        else
        {
          if (minn <= 1 && hrs == 0)
          {
            minn = 1;
            count_minn_pass++;
            if (count_minn_pass == 5)
            {
              if (CodeMachine == 4 || CodeMachine == 3)
              {
                Start();
                delay(1500);
                Power();
                delay(1000);
                StatusControl = "off";
                // stateUpdateState = true;
                endProgram = false;
                chanel = 10;
              }
            }
            if (count_minn_pass == 15)
            {
              display.setSegments(SEG_01);
              reportEspFaultToMelody("01");
            }
            else if (count_minn_pass >= 20)
            {
              endProgram = false;
              chanel = 10;
            }
          }
          else if (minn <= -1 && hrs >= 1)
          {
            hrs--;
            minn = 59;
          }
        }
        // ส่ง status ไป UpdateState ทุก statusReportIntervalMinutes นาที (ค่าเริ่มต้น 5)
        // แต่ 5 นาทีสุดท้าย (hrs==0 && minn<=5) ส่งทุก 1 นาที ให้เวลา ESP↔server ตรงกันมากที่สุด
        if (lastStatusReportMs == 0) lastStatusReportMs = millis();
        const bool lastFiveMinutes = (hrs == 0 && minn <= 5);
        const unsigned long reportIntervalMs =
            lastFiveMinutes ? 60UL * 1000
                            : (unsigned long)statusReportIntervalMinutes * 60 * 1000;
        if ((unsigned long)(millis() - lastStatusReportMs) >= reportIntervalMs) {
          stateUpdateState = true;
          lastStatusReportMs = millis();
        }
        // Serial.println("chanel => " + String(chanel) + " :: " + String(millis()) + " : " + String(timerstanby) + " step : " + String(step));
        // Serial.println("state => " + StatusControl + " :: " + String(hrs) + " : " + String(minn) + " : " + String(second));
      }

      static bool dot = false;
      if (!dot)
      {
        display.showNumberDecEx(hrs, 0b11100000, true, 2, 0);
        display.showNumberDec(minn, true, 2, 2);
        dot = true;
      }
      else
      {
        display.showNumberDec(hrs, true, 2, 0);
        display.showNumberDec(minn, true, 2, 2);
        dot = false;
      }

      machine_runing_time = millis();
    }
  }
}

enum WifiState {
  WIFI_IDLE = 0,
  WIFI_DISCONNECTING,
  WIFI_CONNECTING
};

static unsigned long wifiConnectedSinceMs = 0;
static unsigned long wifiReconnectBackoffMs = 0;  // 0 = พยายามต่อครั้งแรกทันทีหลัง boot
static unsigned long wifiAssocDownSinceMs = 0;
static const unsigned long WIFI_STABLE_WINDOW_MS = 2000;       // v3.58: ลดจาก 4s — ทน WiFi กระพริบกว่า v3.37+
static const unsigned long WIFI_STABLE_MACHINE_MS = 1000;     // warmup สั้นเมื่อเครื่องทำงาน
static const unsigned long WIFI_DOWN_HYSTERESIS_MS = 2500;   // อย่าตัด MQTT ทันทีเมื่อ WiFi สะดุดสั้น
static const unsigned long WIFI_RETRY_BACKOFF_MAX_MS = 30000;
static const unsigned long MQTT_RETRY_BACKOFF_MAX_MS = 60000;
static const unsigned long MQTT_RETRY_BACKOFF_RUN_MS = 15000;  // cap backoff สั้นกว่าเมื่อรันโปรแกรม

static bool espWifiAssociated()
{
  return WiFi.status() == WL_CONNECTED;
}

static bool espWifiDownConfirmed(unsigned long now)
{
  if (espWifiAssociated())
  {
    wifiAssocDownSinceMs = 0;
    return false;
  }
  if (wifiAssocDownSinceMs == 0)
    wifiAssocDownSinceMs = now;
  return (unsigned long)(now - wifiAssocDownSinceMs) >= WIFI_DOWN_HYSTERESIS_MS;
}

static unsigned long wifiStableWindowMs()
{
  if (status_machine_run || status_machine_prepare)
    return WIFI_STABLE_MACHINE_MS;
  return WIFI_STABLE_WINDOW_MS;
}

static bool wifiLinkUsable()
{
  return espWifiAssociated() &&
         wifiConnectedSinceMs != 0 &&
         (unsigned long)(millis() - wifiConnectedSinceMs) >= wifiStableWindowMs();
}

/** เรียกเมื่อ WiFi กลับมา WL_CONNECTED หลังเคยหลุด — เริ่มนับ warmup ก่อน MQTT/HTTP */
static void noteWifiLinkUp(bool syncTime)
{
  if (wifiConnectedSinceMs != 0)
    return;
  wifiConnectedSinceMs = millis();
  wifiReconnectBackoffMs = 1000;
  Serial.println(F("[WiFi] link up — stable warmup window started"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
  Serial.println("WiFi stable warmup(ms): " + String(wifiStableWindowMs()));
  if (syncTime)
    setupTime();
}

void connectwifi() {
  static unsigned long startAttemptTime = millis();
  static WifiState wifiState = WIFI_IDLE;
  static bool wifiBootConnectLogged = false;

  if (!espWifiAssociated()) {
    unsigned long now = millis();
    if (!espWifiDownConfirmed(now))
      return;

    wifiConnectedSinceMs = 0;

    switch (wifiState) {
      case WIFI_IDLE:
        if (now - startAttemptTime >= wifiReconnectBackoffMs) {
          Serial.println(F("[WiFi] disconnect (clean) before connect/retry"));
          WiFi.disconnect(true);
          wifiState = WIFI_DISCONNECTING;
          startAttemptTime = now;
        }
        break;

      case WIFI_DISCONNECTING:
        if (now - startAttemptTime >= 1000) {
          if (!wifiBootConnectLogged) {
            wifiBootConnectLogged = true;
            Serial.println();
            Serial.print(F("[WiFi] Connecting to "));
            Serial.println(ssidStr);
          } else {
            Serial.println(F("[WiFi] Reconnecting..."));
          }
          WiFi.begin(ssidStr.c_str(), passStr.c_str());
          wifiState = WIFI_CONNECTING;
          startAttemptTime = now;
        }
        break;

      case WIFI_CONNECTING:
        if (now - startAttemptTime >= WiFi_TIMEOUT_MS && WiFi.status() != WL_CONNECTED) {
          Serial.println(F("[WiFi] connect failed — reset stack"));
          WiFi.disconnect(true);
          wifiState = WIFI_IDLE;
          startAttemptTime = now;
          wifiReconnectBackoffMs = min(max(wifiReconnectBackoffMs, 1000UL) * 2, WIFI_RETRY_BACKOFF_MAX_MS);
          Serial.println("WiFi retry backoff(ms): " + String(wifiReconnectBackoffMs));
        }
        break;
    }
  } else {
    wifiAssocDownSinceMs = 0;
    if (wifiState == WIFI_CONNECTING) {
      wifiState = WIFI_IDLE;
      Serial.println("\nWiFi connected");
      startAttemptTime = millis();
      noteWifiLinkUp(true);
    } else if (wifiConnectedSinceMs == 0) {
      // auto-reconnect / ต่อกลับขณะ state machine ไม่ใช่ CONNECTING
      noteWifiLinkUp(false);
    }
  }
}

/** Secret ใช้ร่วมกับ backend สำหรับ encode/decode รหัสแสดง (ต้องตรงกัน) */
static const char DISPLAY_CODE_KEY[] = "MelodyDisplayKey";

/** คืนค่ารหัสที่แปลงจาก Noserial สำหรับแสดงบน AP/จอ (XOR + hex; backend decode ได้เพื่อค้นหา controllerId) */
String getDisplayCodeFromNoserial() {
  const size_t keyLen = sizeof(DISPLAY_CODE_KEY) - 1;
  if (keyLen == 0 || Noserial.length() == 0) return String("");
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(Noserial.length() * 2);
  for (size_t i = 0; i < Noserial.length(); i++) {
    uint8_t b = (uint8_t)Noserial.charAt(i) ^ (uint8_t)DISPLAY_CODE_KEY[i % keyLen];
    out += hex[(b >> 4) & 0x0F];
    out += hex[b & 0x0F];
  }
  return out;
}

// --- MQTT presence / LWT (MelodyWebapp subscribe presence/+) ---
static char presenceTopicBuf[64];
static char presencePayloadBuf[320];
static char mqttDiagTopicBuf[64];
static int lastMqttFailRc = 0;
static int lastMqttFailPort = 0;
static int mqttConnectFailStreak = 0;
static char configResponseTopicBuf[80];
/** ห้าม publish/loop ซ้อนใน MQTT callback — คิวไป taskWifiMqtt */
static bool pendingCommandBackPublish = false;
static char pendingCommandBackBuf[256];
static bool pendingPresenceAfterMqttConnect = false;
static bool pendingPresenceHeartbeat = false;
static bool pendingUpdateStatePublish = false;
static char pendingUpdateStateBuf[220];
static bool pendingMqttDiagAfterConnect = false;
static int pendingMqttDiagRc = 0;
static int pendingMqttDiagFails = 0;

/** ตัด MQTT/TCP เมื่อ WiFi หลุด — กัน publish บน socket ค้าง (assert pbuf_free) */
static void teardownMqttOnWifiDown()
{
  pendingPresenceAfterMqttConnect = false;
  pendingPresenceHeartbeat = false;
  pendingUpdateStatePublish = false;
  pendingUpdateStateBuf[0] = '\0';
  pendingMqttDiagAfterConnect = false;
  pendingCommandBackPublish = false;
  pendingCommandBackBuf[0] = '\0';
  if (!netLockEnter())
    return;
  // ปิดครั้งเดียว — disconnect() มี client.stop() ในตัว; อย่า stop() ซ้ำ (lwIP pbuf assert)
  if (mqclient.connected())
    mqclient.disconnect();
  else
    client.stop();
  netLockLeave();
}

static void buildPresenceTopicStr()
{
  snprintf(presenceTopicBuf, sizeof(presenceTopicBuf), "presence/%s", Noserial.c_str());
}

static void buildMqttDiagTopicStr()
{
  snprintf(mqttDiagTopicBuf, sizeof(mqttDiagTopicBuf), "mqttDiag/%s", Noserial.c_str());
}

static void buildPresencePayload(const char *state, bool withConnInfo = false)
{
  StaticJsonDocument<384> doc;
  doc["cm"] = "presence";
  doc["id"] = Noserial;
  doc["value_str1"] = gid;
  doc["value_str2"] = state;
  // รายงานเวอร์ชัน firmware + โปรโตคอล Melody (v3) — backend เก็บไว้เตือน version mismatch
  String fwStr = String(fwversion[1]);
  fwStr.replace("Version ", "");
  fwStr.trim();
  doc["fw"] = fwStr;
  doc["mv"] = 3;
  if (withConnInfo && mqtt_server != nullptr)
  {
    doc["broker"] = mqtt_server;
    doc["port"] = mqtt_port;
    doc["mqttStatus"] = mqttStatus;
    if (lastMqttFailRc != 0)
    {
      doc["fail_rc"] = lastMqttFailRc;
      if (lastMqttFailPort > 0)
        doc["fail_port"] = lastMqttFailPort;
    }
  }
  presencePayloadBuf[0] = '\0';
  serializeJson(doc, presencePayloadBuf, sizeof(presencePayloadBuf));
}

// throttle การ report ตอน MQTT ล่มจริง (ทุก 1 นาที — ให้คำสั่ง diag/set_broker มาถึงไว)
static unsigned long lastMqttHttpReportMs = 0;
static const unsigned long MQTT_HTTP_REPORT_INTERVAL_MS = 60UL * 1000;
static String otaFolderOverride;
static String otaFilenameOverride;

// MQTT ถือว่า "ล่มจริง" เมื่อ fail ติดกันเกิน 20 ครั้ง — ก่อนหน้านั้นให้ retry MQTT อย่างเดียว
static const int MQTT_FAIL_STREAK_FALLBACK = 20;
static bool mqttDownForFallback()
{
  return !mqclient.connected() && mqttConnectFailStreak > MQTT_FAIL_STREAK_FALLBACK;
}

// ช่วง boot/setup: ยังไม่ poll HTTP / ไม่รับ reboot|OTA|set_broker — กันรีบูทวนซ้ำตอนตั้งค่า WiFi/MQTT/ดึง config
static unsigned long melodyBootMs = 0;
static const unsigned long MELODY_BOOT_SETUP_GRACE_MS = 180UL * 1000;

static bool isMelodyBootSetupPhase()
{
  if (melodyBootMs == 0)
    return true;
  // MQTT ล่มจริงแล้ว — ออกจาก boot/setup เพื่อให้ HTTP poll / fallback ทำงาน
  if (mqttDownForFallback())
    return false;
  if (firstGetdata || stateGetdata || stateSetupdata)
    return true;
  return (unsigned long)(millis() - melodyBootMs) < MELODY_BOOT_SETUP_GRACE_MS;
}

// กัน config/promo ซ้ำตอน boot — รอ debounce แล้วใช้ชุดสุดท้ายจาก Melody (ไม่ใช่ชุดแรกที่อาจเป็น retained/เก่า)
static unsigned long bootMelodySyncLastMs = 0;
static bool bootMelodyConfigPending = false;
static bool bootMelodyPromoPending = false;
static String bootMelodyPromoPayload;
static int bootMelodyConfigCount = 0;
static const unsigned long BOOT_MELODY_SYNC_DEBOUNCE_MS = 2500UL;

static bool bootMelodySyncQuietReady()
{
  return bootMelodySyncLastMs > 0 &&
         (unsigned long)(millis() - bootMelodySyncLastMs) >= BOOT_MELODY_SYNC_DEBOUNCE_MS;
}

static void applyPromoSlotsPayload(const String &payloadJson);

static String melodyHttpUrl(const String &path)
{
  if (port == 443)
    return "https://" + server + path;
  if (port == 80)
    return "http://" + server + path;
  return "http://" + server + ":" + String(port) + path;
}

// result (ถ้ามี) = ผลคำสั่ง เช่น diag JSON
static bool ackMelodyHttpCommand(const char *ack, const char *status, const String &result = "")
{
  if (!netLockEnter())
    return false;
  HTTPClient http;
  http.begin(melodyHttpUrl(Path_DeviceAck));
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<768> doc;
  doc["api_key"] = api_key;
  doc["controller_id"] = Noserial;
  doc["ack"] = ack;
  doc["status"] = status;
  if (result.length() > 0)
    doc["result"] = result;
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  netLockLeave();
  Serial.println(String("[HTTP] device-ack ") + ack + " code=" + code);
  return code == 200 || code == 201;
}

// สร้างผลวินิจฉัยตอบคำสั่ง diag — สถานะ MQTT/WiFi/ระบบ เพื่อให้แอดมินไล่ปัญหา
static String buildDiagResultJson()
{
  StaticJsonDocument<384> doc;
  doc["rc"] = lastMqttFailRc;
  doc["fail_count"] = mqttConnectFailStreak;
  if (mqtt_server != nullptr)
    doc["broker"] = mqtt_server;
  doc["port"] = lastMqttFailPort > 0 ? lastMqttFailPort : mqtt_port;
  doc["mqttStatus"] = mqttStatus;
  doc["rssi"] = WiFi.RSSI();
  doc["heap"] = (uint32_t)ESP.getFreeHeap();
  doc["uptime_s"] = (uint32_t)(millis() / 1000);
  doc["fw"] = fwversion[1];
  String out;
  serializeJson(doc, out);
  return out;
}

// ส่ง UpdateState ทาง HTTP — ใช้เมื่อ MQTT ล่มจริง (ลูกค้าใช้เครื่องระหว่าง MQTT ล่ม)
// เรียกหลัง SetStatusControl()/SetTimerSend() แล้ว (StatusControl/TimeSent พร้อมใช้)
static bool sendUpdateStateHttp()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  if (!netLockEnter())
    return false;
  HTTPClient http;
  http.begin(melodyHttpUrl(Path_UpdateState));
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<320> doc;
  doc["api_key"] = api_key;
  doc["controller_id"] = Noserial;
  doc["ID"] = IDserver;
  doc["Status"] = StatusControl;
  doc["Time"] = TimeSent;
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  netLockLeave();
  Serial.println(String("[HTTP] update-state code=") + code + " :: " + StatusControl + " :: " + TimeSent);
  return code == 200 || code == 201;
}

static void handleMelodyPollCommands(const String &responseJson)
{
  DynamicJsonDocument doc(768);
  if (deserializeJson(doc, responseJson))
  {
    Serial.println("[HTTP] poll response JSON parse error");
    return;
  }
  JsonArray cmds = doc["commands"].as<JsonArray>();
  if (cmds.isNull())
    return;
  const bool setupPhase = isMelodyBootSetupPhase();
  for (JsonObject c : cmds)
  {
    const char *cmd = c["cmd"];
    if (!cmd)
      continue;
    if (setupPhase && strcmp(cmd, "diag") != 0)
    {
      Serial.println(String("[HTTP] skip command during boot/setup: ") + cmd);
      continue;
    }
    if (strcmp(cmd, "ota") == 0)
    {
      otaFolderOverride = c["folder"].as<String>();
      otaFilenameOverride = c["filename"] | "firmware.bin";
      Serial.println("[HTTP] command OTA folder=" + otaFolderOverride + " file=" + otaFilenameOverride);
      ackMelodyHttpCommand("ota", "accepted");
      UpdateFw = true;
    }
    else if (strcmp(cmd, "diag") == 0)
    {
      // ตอบข้อมูลวินิจฉัยกลับทันที — backend เก็บใน httpDiagResult
      Serial.println("[HTTP] command diag");
      ackMelodyHttpCommand("diag", "ok", buildDiagResultJson());
    }
    else if (strcmp(cmd, "set_broker") == 0)
    {
      int newStatus = c["mqttStatus"] | 0;
      if (newStatus == 1 || newStatus == 2)
      {
        Serial.println("[HTTP] command set_broker -> mqttStatus=" + String(newStatus));
        mqttStatus = newStatus;
        // บันทึกถาวร — boot หน้าก็ใช้ broker ใหม่
        if (preferences.begin("config", false))
        {
          preferences.putInt("mqttStatus", mqttStatus);
          preferences.end();
        }
        ackMelodyHttpCommand("set_broker", "accepted");
        // reset ตัวนับ ให้ mqttreconnect ลอง broker ใหม่ทันที
        mqttConnectFailStreak = 0;
        lastMqttFailRc = 0;
        lastMqttFailPort = 0;
        if (netLockEnter())
        {
          mqclient.disconnect();
          netLockLeave();
        }
      }
      else
      {
        ackMelodyHttpCommand("set_broker", "failed");
      }
    }
    else if (strcmp(cmd, "reboot") == 0)
    {
      Serial.println("[HTTP] command reboot");
      ackMelodyHttpCommand("reboot", "accepted");
      delay(500);
      publishPresenceOfflineGraceful();
      ESP.restart();
    }
    else if (strcmp(cmd, "run") == 0)
    {
      // สั่งเริ่มโปรแกรมผ่าน HTTP เมื่อ MQTT ล่ม — ใช้เส้นทางเดียวกับคำสั่ง MQTT (commandApp)
      int prog = c["program"] | 0;
      if (prog >= 1 && prog <= 7 && !status_machine_prepare && !status_machine_run)
      {
        Serial.println("[HTTP] command run program=" + String(prog));
        cm = "cmProgram";
        value_str1 = String(gid);
        value_str2 = String(prog);
        commandApp();
        ackMelodyHttpCommand("run", "accepted");
      }
      else
      {
        Serial.println("[HTTP] command run rejected (busy/invalid) program=" + String(prog));
        ackMelodyHttpCommand("run", "failed");
      }
    }
    else if (strcmp(cmd, "stop") == 0)
    {
      // สั่งหยุด/รีเซ็ตผ่าน HTTP — value_str2=0 คือ reset (เหมือนคำสั่ง MQTT)
      Serial.println("[HTTP] command stop");
      cm = "cmProgram";
      value_str1 = String(gid);
      value_str2 = "0";
      commandApp();
      ackMelodyHttpCommand("stop", "accepted");
    }
  }
}

bool pollMelodyDeviceHttp()
{
  // เรียกเฉพาะเมื่อ MQTT ล่มจริง (fail > 20 ครั้ง) — ก่อนหน้านั้น MQTT-first
  if (isMelodyBootSetupPhase())
    return false;
  if (WiFi.status() != WL_CONNECTED || !mqttDownForFallback())
    return false;
  unsigned long nowMs = millis();
  if (lastMqttHttpReportMs != 0 &&
      (unsigned long)(nowMs - lastMqttHttpReportMs) < MQTT_HTTP_REPORT_INTERVAL_MS)
    return false;
  if (!netLockEnter())
    return false;

  HTTPClient http;
  http.begin(melodyHttpUrl(Path_MqttReport));
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<384> doc;
  doc["api_key"] = api_key;
  doc["controller_id"] = Noserial;
  doc["event"] = "mqtt_connect_fail";
  doc["rc"] = lastMqttFailRc;
  if (mqtt_server != nullptr)
    doc["broker"] = mqtt_server;
  doc["port"] = lastMqttFailPort > 0 ? lastMqttFailPort : mqtt_port;
  doc["mqttStatus"] = mqttStatus;
  doc["fail_count"] = mqttConnectFailStreak;
  doc["wifi_rssi"] = WiFi.RSSI();

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  String response = http.getString();
  http.end();
  netLockLeave();

  if (code == 200 || code == 201)
  {
    lastMqttHttpReportMs = nowMs;
    Serial.println(String("[HTTP] device-poll ok code=") + code);
    if (response.length() > 0)
      handleMelodyPollCommands(response);
    return true;
  }
  Serial.println(String("[HTTP] device-poll fail code=") + code);
  return false;
}

static void publishMqttDiag(const char *event, int rc, int failCount)
{
  if (!mqclient.connected())
    return;
  buildMqttDiagTopicStr();
  StaticJsonDocument<256> doc;
  doc["cm"] = "mqttDiag";
  doc["id"] = Noserial;
  doc["event"] = event;
  if (mqtt_server != nullptr)
    doc["broker"] = mqtt_server;
  doc["port"] = mqtt_port;
  doc["mqttStatus"] = mqttStatus;
  if (rc != 0)
    doc["rc"] = rc;
  if (failCount > 0)
    doc["fail_count"] = failCount;
  char buf[256];
  buf[0] = '\0';
  serializeJson(doc, buf, sizeof(buf));
  if (!netLockEnter())
    return;
  mqclient.publish(mqttDiagTopicBuf, buf, false);
  netLockLeave();
  Serial.println(String("[MQTT] mqttDiag ") + event + " -> " + mqttDiagTopicBuf);
}

/** งาน MQTT ที่ต้องทำนอก callback — publish รวมรอบเดียวแล้ว pump ครั้งเดียว (กัน lwIP pbuf crash) */
static void processDeferredMqttWork()
{
  if (!wifiLinkUsable() || !mqclient.connected())
    return;

  if (!netLockEnter())
    return;

  if (pendingCommandBackPublish && pendingCommandBackBuf[0] != '\0')
  {
    mqclient.publish("commandBack", pendingCommandBackBuf);
    pendingCommandBackPublish = false;
    pendingCommandBackBuf[0] = '\0';
  }

  if (pendingUpdateStatePublish && pendingUpdateStateBuf[0] != '\0')
  {
    mqclient.publish("UpdateState", pendingUpdateStateBuf);
    pendingUpdateStatePublish = false;
  }

  if (pendingPresenceAfterMqttConnect)
  {
    pendingPresenceAfterMqttConnect = false;
    buildPresenceTopicStr();
    buildPresencePayload("online", true);
    if (mqclient.publish(presenceTopicBuf, presencePayloadBuf, true))
      Serial.println(String("[MQTT] presence online -> ") + presenceTopicBuf);
  }

  if (pendingPresenceHeartbeat)
  {
    pendingPresenceHeartbeat = false;
    buildPresenceTopicStr();
    buildPresencePayload("online", true);
    if (mqclient.publish(presenceTopicBuf, presencePayloadBuf, true))
      Serial.println(String("[MQTT] presence heartbeat -> ") + presenceTopicBuf);
  }

  if (pendingMqttDiagAfterConnect)
  {
    pendingMqttDiagAfterConnect = false;
    buildMqttDiagTopicStr();
    StaticJsonDocument<256> doc;
    doc["cm"] = "mqttDiag";
    doc["id"] = Noserial;
    doc["event"] = "recovered";
    if (mqtt_server != nullptr)
      doc["broker"] = mqtt_server;
    doc["port"] = mqtt_port;
    doc["mqttStatus"] = mqttStatus;
    if (pendingMqttDiagRc != 0)
      doc["rc"] = pendingMqttDiagRc;
    if (pendingMqttDiagFails > 0)
      doc["fail_count"] = pendingMqttDiagFails;
    char buf[256];
    buf[0] = '\0';
    serializeJson(doc, buf, sizeof(buf));
    mqclient.publish(mqttDiagTopicBuf, buf, false);
    mqttConnectFailStreak = 0;
    lastMqttFailRc = 0;
    lastMqttFailPort = 0;
    Serial.println(String("[MQTT] mqttDiag recovered -> ") + mqttDiagTopicBuf);
  }

  mqttPumpLoopLocked(2);
  netLockLeave();
}

bool publishPresenceOnline()
{
  if (!mqclient.connected())
    return false;
  pendingPresenceHeartbeat = true;
  return true;
}

void publishPresenceOfflineGraceful()
{
  if (!mqclient.connected())
    return;
  if (!netLockEnter())
    return;
  buildPresenceTopicStr();
  buildPresencePayload("offline");
  mqclient.publish(presenceTopicBuf, presencePayloadBuf, true);
  for (int i = 0; i < 5; i++)
  {
    mqttPumpLoopLocked(1);
    delay(20);
  }
  Serial.println(String("[MQTT] presence offline (graceful) -> ") + presenceTopicBuf);
  mqclient.disconnect();
  netLockLeave();
}

void enterWifiConfigMode()
{
  Serial.println("\n=== Enter WiFi Setup Mode (SoftAP) ===");

  // หยุดการใช้งาน WiFi/MQTT ปกติ
  state_wifi_on = false;
  publishPresenceOfflineGraceful();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  const char *apSsid = "ESP-Setup";
  const char *apPass = "12345678";
  bool apOk = WiFi.softAP(apSsid, apPass);

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("SoftAP %s started: %s (ok=%d)\n", apSsid, ip.toString().c_str(), apOk);

  // แสดงข้อความง่าย ๆ บนจอ (เช่น 0.0. หรือไฟวิ่ง)
  display.showNumberDec(0, true, 4, 0);

  wifiServer.on("/", HTTP_GET, []()
                {
                  String html;
                  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
                  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
                  html += "<title>WiFi Setup</title>";
                  html += "<style>";
                  html += "body{font-family:sans-serif;margin:0;padding:0;background:#f4f4f7;color:#222;}";
                  html += ".wrap{max-width:420px;margin:24px auto;padding:20px 18px;background:#fff;border-radius:12px;box-shadow:0 4px 16px rgba(0,0,0,0.08);}";
                  html += "h2{margin-top:0;font-size:1.3rem;text-align:center;color:#222;}";
                  html += "label{display:block;margin:14px 0 6px;font-size:0.9rem;color:#555;}";
                  html += "input[type=text],input[type=password]{width:100%;padding:10px 12px;border:1px solid #ccc;border-radius:8px;font-size:0.95rem;box-sizing:border-box;}";
                  html += "input[type=text]:focus,input[type=password]:focus{outline:none;border-color:#007bff;box-shadow:0 0 0 2px rgba(0,123,255,0.15);}";
                  html += "button{width:100%;margin-top:18px;padding:11px 0;border:none;border-radius:999px;background:#007bff;color:#fff;font-size:1rem;font-weight:600;}";
                  html += "button:active{background:#005fcc;}";
                  html += "p{margin-top:14px;font-size:0.85rem;color:#666;text-align:center;line-height:1.4;}";
                  html += ".code{font-family:monospace;background:#f0f0f0;padding:6px 10px;border-radius:6px;word-break:break-all;}";
                  html += "</style></head><body>";
                  html += "<div class='wrap'>";
                  html += "<h2>ตั้งค่า WiFi เครื่องซักผ้า</h2>";
                  html += "<p style='margin-bottom:12px;'><strong>รหัสเครื่อง (สำหรับค้นหาในระบบ):</strong></p>";
                  html += "<p class='code' style='text-align:left;margin-top:0;'>" + getDisplayCodeFromNoserial() + "</p>";
                  html += "<form method='POST' action='/save'>";
                  html += "<label>ชื่อ WiFi (SSID)</label>";
                  html += "<input name='ssid' type='text' autocomplete='off' />";
                  html += "<label>รหัสผ่าน (Password)</label>";
                  html += "<input name='password' type='password' autocomplete='off' />";
                  html += "<button type='submit'>บันทึกการตั้งค่า</button>";
                  html += "</form>";
                  html += "<p>หลังบันทึก เครื่องจะรีสตาร์ตและลองเชื่อมต่อ WiFi ที่ตั้งค่าไว้โดยอัตโนมัติ</p>";
                  html += "</div></body></html>";
                  wifiServer.send(200, "text/html", html); });

  wifiServer.on("/save", HTTP_POST, []()
                {
                  String newSsid = wifiServer.arg("ssid");
                  String newPass = wifiServer.arg("password");

                  if (newSsid.length() == 0)
                  {
                    wifiServer.send(400, "text/plain", "SSID is required");
                    return;
                  }

                  ssidStr = newSsid;
                  passStr = newPass;

                  Serial.println("Saving new WiFi config:");
                  Serial.println("SSID: " + ssidStr);

                  writePreferencesfirst();

                  if (g_mqttOnline)  // web handler อาจคนละ task — อ่าน cache กัน recv ซ้อน
                    PublishConfigViaMqtt();

                  {
                    String okHtml;
                    okHtml += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
                    okHtml += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
                    okHtml += "<title>บันทึกสำเร็จ</title>";
                    okHtml += "<style>";
                    okHtml += "body{font-family:sans-serif;margin:0;padding:0;background:linear-gradient(160deg,#e8f5e9 0%,#f4f4f7 45%,#e3f2fd 100%);color:#222;min-height:100vh;display:flex;align-items:center;justify-content:center;}";
                    okHtml += ".wrap{max-width:420px;margin:20px;padding:28px 22px;background:#fff;border-radius:16px;box-shadow:0 8px 32px rgba(0,0,0,0.1);text-align:center;}";
                    okHtml += ".icon{width:72px;height:72px;margin:0 auto 16px;background:linear-gradient(135deg,#43a047,#2e7d32);border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:2.2rem;color:#fff;}";
                    okHtml += "h2{margin:0 0 10px;font-size:1.35rem;color:#1b5e20;}";
                    okHtml += "p{margin:0;font-size:0.95rem;color:#555;line-height:1.55;}";
                    okHtml += ".hint{margin-top:18px;padding:12px 14px;background:#f1f8e9;border-radius:10px;font-size:0.85rem;color:#33691e;border:1px solid #c5e1a5;}";
                    okHtml += ".pulse{display:inline-block;margin-top:14px;width:8px;height:8px;background:#43a047;border-radius:50%;animation:p 1.2s ease-in-out infinite;}";
                    okHtml += "@keyframes p{0%,100%{opacity:1;transform:scale(1);}50%{opacity:0.5;transform:scale(0.85);}}";
                    okHtml += "</style></head><body>";
                    okHtml += "<div class='wrap'>";
                    okHtml += "<div class='icon'>&#10003;</div>";
                    okHtml += "<h2>บันทึกสำเร็จ</h2>";
                    okHtml += "<p>เครื่องจะรีสตาร์ตและเชื่อมต่อ WiFi ที่ตั้งค่าไว้โดยอัตโนมัติ</p>";
                    okHtml += "<div class='hint'>กรุณารอสักครู่ อย่าปิดเครื่องระหว่างรีสตาร์ต</div>";
                    okHtml += "<span class='pulse'></span>";
                    okHtml += "</div></body></html>";
                    wifiServer.send(200, "text/html", okHtml);
                  }
                  delay(1000);
                  ESP.restart(); });

  // Captive portal: ให้มือถือ/แท็บเล็ตเปิดเบราว์เซอร์อัตโนมัติเมื่อต่อ WiFi "ESP-Setup"
  // DNS ชี้ทุกโดเมนมาที่ IP ของ AP → request เช่น captive.apple.com, connectivitycheck.gstatic.com จะมาที่ ESP
  dnsServer.start(53, "*", ip);
  wifiServer.onNotFound([]() {
    wifiServer.sendHeader("Location", "/");
    wifiServer.send(302, "text/plain", "");
  });

  wifiServer.begin();
  Serial.println("Captive portal: connect to WiFi 'ESP-Setup' then open browser (may open automatically)");

  // วนรอให้ผู้ใช้ตั้งค่า WiFi (โหมดนี้จะรีสตาร์ตเมื่อบันทึกเสร็จ)
  while (true)
  {
    dnsServer.processNextRequest();
    wifiServer.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // ตอบกลับจากระบบปัจจุบัน (configResponse/<Noserial>) — เก็บไว้ให้ GetSetupData() บันทึก
  String topicStr = String(topic);
  if (topicStr == "configResponse/" + Noserial) {
    if (isMelodyBootSetupPhase()) {
      bootMelodyConfigCount++;
      mqttPayloadBuffer = message;
      bootMelodyConfigPending = true;
      bootMelodySyncLastMs = millis();
      if (bootMelodyConfigCount > 1) {
        Serial.println(F("[MQTT] <<< รับ configResponse ชุดใหม่ — จะใช้ชุดสุดท้ายจาก Melody"));
      } else {
        Serial.println(F("[MQTT] <<< รับ configResponse สำเร็จ"));
      }
      Serial.println("       topic  = configResponse/" + Noserial);
      Serial.println("       ความยาว = " + String(message.length()) + " bytes");
      Serial.println(F("       -> รอ debounce แล้ว GetSetupData() จะบันทึกชุดสุดท้าย"));
      return;
    }
    mqttPayloadBuffer = message;
    stateSetupdata = true;
    Serial.println(F("[MQTT] <<< รับ configResponse สำเร็จ"));
    Serial.println("       topic  = configResponse/" + Noserial);
    Serial.println("       ความยาว = " + String(message.length()) + " bytes");
    if (message.length() > 0) {
      int previewLen = (message.length() > 180) ? 180 : message.length();
      Serial.println("       payload (ตัวอย่าง) = " + message.substring(0, previewLen) + (message.length() > 180 ? "..." : ""));
    }
    Serial.println(F("       -> GetSetupData() จะบันทึกลง NVS"));
    return;
  }

  // payload บน V<gid> อาจเป็น setup ขนาดใหญ่ (1–2 KB) ต้องใช้ buffer พอ — ให้เหมือน ATD35_Melody_V2
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  // ตรวจสอบ key ก่อนใช้งาน
  if (!doc.containsKey("cm") || !doc.containsKey("id")) {
    Serial.println("Missing required keys in JSON");
    return;
  }

  // const size_t capacity = JSON_OBJECT_SIZE(3) + 60;
  // DynamicJsonDocument doc(capacity);
  // // JsonDocument doc(1024);  // Adjust the size as needed
  // // StaticJsonDocument<1024*2> doc;  // Adjust the size as needed

  // DeserializationError error = deserializeJson(doc, payload, length);
  // if (error)
  // {
  //   Serial.print(F("deserializeJson() failed: "));
  //   Serial.println(error.f_str());
  //   return;
  // }

  
  String cm_buf = doc["cm"].as<String>();
  String id_buf = doc["id"].as<String>();
  // String value_str1_buf = doc["value_str1"] | ""; // ใช้ default ถ้าไม่มี
  // String value_str2_buf = doc["value_str2"] | "";
  String value_str1_buf = doc["value_str1"].as<String>();
  String value_str2_buf = doc["value_str2"].as<String>();

  cm = cm_buf;
  value_str1 = value_str1_buf;
  value_str2 = value_str2_buf;
  // Serial.println("Read value_str2 : " + value_str2);

  // แจ้งเมื่อแอดมินส่ง getdata มา (แสดงทั้งกรณี id ตรงและไม่ตรง เพื่อให้ ESP32 ทราบว่ามีคำถามเข้ามา)
  if (cm_buf == "getdata") {
    Serial.println(F("[MQTT] >>> getdata received (แอดมินสอบถาม) <<<"));
    Serial.print(F("  id in message = "));
    Serial.println(id_buf);
    Serial.print(F("  this machine Noserial = "));
    Serial.println(Noserial);
    Serial.println(id_buf == Noserial ? F("  => MATCH: will send config to getdataResponse") : F("  => id not match: ignore"));
  }

  if (id_buf == Noserial) {
    if (cm_buf == "getdata") {
      stateSendConfigMqtt = true;  // ส่ง config กลับทาง MQTT (topic getdataResponse)
      getdataDisplayUntil = millis() + 2500;  // แสดงบนจอ TM1637 สัก 2.5 วินาที
    } else if (cm_buf == "setup") {
      Serial.println("setup data ####### : " + cm_buf);
      mqttPayloadBuffer = message;  // เก็บ payload เพื่อให้ GetSetupData() แปลงและบันทึก
      stateSetupdata = true;
    } else if (cm_buf == "cmProgram" || cm_buf == "cmCommand") {
      Serial.println("commandApp ####### : " + cm_buf);
      commandApp();
    } else {
      Serial.println("cm: " + cm_buf);
      Serial.println("id: " + id_buf);
      Serial.println("value_str1: " + value_str1_buf);
      Serial.println("value_str2: " + value_str2_buf);
    }
  }
}

void mqttreconnect() {
  static unsigned long lastReconnectAttempt = 0;
  static unsigned long reconnectInterval = 5000;   // backoff เบา 5→15s
  static uint8_t mqttSamePortFails = 0;            // fail ติดกันในพอร์ตเดิม — ครบ 4 ค่อยเปลี่ยนพอร์ต

  // วินิจฉัยสาเหตุหลุด: log ตอน connected -> disconnected (edge) เพื่อรู้ rc + สถานะ WiFi
  // rc=-3 CONNECTION_LOST (TCP ถูกตัด) / rc=-4 CONNECTION_TIMEOUT (ping ไม่ตอบ/loop starve) / rc=-1 เราสั่ง disconnect
  static bool mqttWasConnected = false;
  bool mqttNowConnected = mqclient.connected();
  if (mqttWasConnected && !mqttNowConnected) {
    Serial.print(F("[MQTT] dropped rc="));
    Serial.print(mqclient.state());
    Serial.print(F(" wifi="));
    Serial.print(WiFi.status());
    Serial.print(F(" rssi="));
    Serial.println(WiFi.RSSI());
  }
  mqttWasConnected = mqttNowConnected;

  if (!wifiLinkUsable()) {
    return;
  }

  if (!mqclient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt >= reconnectInterval) {
      lastReconnectAttempt = now;
      if (!netLockEnter())
        return;

      // ปิด socket เก่าก่อนลองใหม่ "ครั้งเดียว" — กัน connect ค้างเมื่อ broker หลุดกลางทาง
      // อย่า double-close: mqclient.disconnect() เรียก client.stop() ในตัวอยู่แล้ว การ client.stop() ซ้ำ
      // ทำ lwIP pbuf ref พัง -> assert "pbuf_free: p->ref > 0" -> รีบูตกลางงาน (crash 3.58+; 3.32 ไม่มี pattern นี้)
      if (mqclient.connected())
        mqclient.disconnect();
      else
        client.stop();
      vTaskDelay(pdMS_TO_TICKS(50));
      yield();

      // ใช้ port ปัจจุบัน — หมุน port เฉพาะตอน connect fail (ไม่ ++ ทุกครั้ง)
      if (mqttStatus == 1) {
        mqtt_server = mqtt_server1;
        mqtt_port = mqtt_port1;
      } else {
        mqtt_server = "broker.mqtt.cool";
        mqtt_port = mqtt_port2;
      }

      Serial.print("MQTT server: ");
      Serial.print(mqtt_server);
      Serial.print(" port: ");
      Serial.println(mqtt_port);

      vTaskDelay(pdMS_TO_TICKS(1));  // yield ก่อน connect ยาว (กัน IDLE TWDT บน core 0 ถ้า pin ผิด)

      mqclient.setServer(mqtt_server, mqtt_port);
      mqclient.setKeepAlive(60);  // ตรงกับ 3.00 — keepAlive 15 พิสูจน์แล้วว่า drop ถี่ขึ้น + churn กระตุ้น crash
      mqclient.setCallback(callback);
      mqclient.setSocketTimeout(15);  // ตรงกับ 3.00 (default 15s) — 6s ตัด socket เร็วไปตอน WiFi jitter → หลุดทั้งที่ยังต่อ
      // getdataResponse JSON ใหญ่เกิน 256 bytes — ต้องขยาย buffer ก่อน connect
      mqclient.setBufferSize(2048);
      client.setTimeout(15000);

      topic = "V" + String(gid);
      buildPresenceTopicStr();
      buildPresencePayload("offline");
      snprintf(configResponseTopicBuf, sizeof(configResponseTopicBuf), "configResponse/%s", Noserial.c_str());

      if (mqclient.connect(
              Noserial.c_str(),
              mqtt_username,
              mqtt_password,
              presenceTopicBuf,
              1,
              true,
              presencePayloadBuf))
      {
        mqclient.subscribe(topic.c_str());
        mqclient.subscribe(configResponseTopicBuf);
        int prevRc = lastMqttFailRc;
        int prevFails = mqttConnectFailStreak;
        pendingPresenceAfterMqttConnect = true;
        // รายงานสถานะหลัง (re)connect — ข้ามช่วง boot/setup เพื่อไม่รบกวนตอนดึง config
        if (!isMelodyBootSetupPhase())
          stateUpdateState = 1;
        if (prevFails > 0)
        {
          pendingMqttDiagAfterConnect = true;
          pendingMqttDiagRc = prevRc;
          pendingMqttDiagFails = prevFails;
        }
        reconnectInterval = 5000;
        mqttSamePortFails = 0;
        Serial.println("MQTT connected : " + String(mqtt_port) + " subscribed : " + topic + " , " + String(configResponseTopicBuf));
      } else {
        lastMqttFailRc = mqclient.state();
        lastMqttFailPort = mqtt_port;
        mqttConnectFailStreak++;
        mqttSamePortFails++;
        // อยู่พอร์ตเดิมก่อน (broker หลักอาจแค่สะดุด) — fail ครบ 4 ครั้งในพอร์ตเดียวค่อยเปลี่ยนพอร์ต
        // ระหว่างนั้น backoff เบา 5→15s (ไม่ยาวถึง 60s); เปลี่ยนพอร์ตแล้วเริ่มใหม่ที่ 5s
        if (mqttStatus == 1 && mqttSamePortFails >= 4) {
          mqttSamePortFails = 0;
          mqtt_port1++;
          if (mqtt_port1 >= 4745)
            mqtt_port1 = 4741;
          reconnectInterval = 5000;
        } else {
          reconnectInterval = min(reconnectInterval * 2, 15000UL);
        }
        Serial.print("MQTT connection failed, rc=");
        Serial.println(lastMqttFailRc);
        Serial.println("Will try again in " + String(reconnectInterval / 1000) + "s, port: " + String(mqtt_port1) + " (fail " + String(mqttSamePortFails) + "/4)");
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      netLockLeave();
    }
  }
}

int checkLightStart(int countStateLight, bool stateWhileRead) {
  bool stateLight = false;
  unsigned long timerChecklight = millis();
  int sentReturn = 2; // 0=off, 1=blink, 2=on
  int ldrRead = 0;
  stateWhile = stateWhileRead;
  int LigthOn = 0;
  int LigthOff = 0;
  int ldrLigthCount = 0;
  bool ligth = false;

  while (stateWhile) {
    if (OldBoard == 1) {
      static unsigned long lastReadTime = 0;
      static LdrAvgSampler ldrSampler;
      static bool sampling = false;
      unsigned long now = millis();

      if (!sampling && (now - lastReadTime >= LDR_READ_INTERVAL_MS)) {
        ldrSampler.begin(ldrPin);
        sampling = true;
      }

      int val = 0;
      if (sampling && ldrSampler.tick(&val)) {
        sampling = false;
        lastReadTime = now;
        printLdrSummary("checkLightStart", (uint8_t)ldrPin, val);

        if (val > ldr_set && ligth) {
          ligth = false;
          LigthOn++;
          Serial.println("Light On");
          timerstanby = millis();
        } else if (val < ldr_set - ldrMinus && !ligth) {
          ligth = true;
          LigthOff++;
          Serial.println("Light Off");
          timerstanby = millis();
        } else if (val > ldr_set - ldrMinus) {
          ldrLigthCount++;
          Serial.println("Light On Count: " + String(ldrLigthCount));
          if (ldrLigthCount >= 7) {
            LigthOn = LigthOff = ldrLigthCount = 0;
            ligth = true;
            sentReturn = 2;
            stateWhile = false;
          }
          timerstanby = millis();
        }

        if (millis() - timerstanby >= 3000 && val < ldr_set - ldrMinus) {
          LigthOn = LigthOff = ldrLigthCount = 0;
          ligth = true;
          sentReturn = 0;
          stateWhile = false;
        }

        if (LigthOn >= 5 && LigthOff >= 5) {
          LigthOn = LigthOff = ldrLigthCount = 0;
          ligth = true;
          sentReturn = 1;
          stateWhile = false;
        }
      }
    } else {
      static unsigned long lastReadTime = 0;
      static LdrAvgSampler ldrSampler;
      static bool sampling = false;
      unsigned long now = millis();

      if (!sampling && (now - lastReadTime >= LDR_READ_INTERVAL_MS)) {
        ldrSampler.begin(ldrPin);
        sampling = true;
      }

      int ldrRead = 0;
      if (sampling && ldrSampler.tick(&ldrRead)) {
        sampling = false;
        lastReadTime = now;
        printLdrSummary("checkLightStart", (uint8_t)ldrPin, ldrRead);

        if (ldrRead <= ldr_set && !stateLight) {
          countStateLight++;
          stateLight = true;
          Serial.println("Blink On : " + String(ldrRead));
          timerChecklight = millis();
        } else if (ldrRead > ldr_set + ldrMinus && stateLight) {
          stateLight = false;
          Serial.println("Blink Off : " + String(ldrRead));
          timerChecklight = millis();
          if (countStateLight >= 5) {
            sentReturn = 1;
            Serial.println("Light Blink : " + String(ldrRead));
            stateWhile = false;
          }
        } else if (millis() - timerChecklight >= 3000) {  
          if (ldrRead > ldr_set + ldrMinus) {
            sentReturn = 0;
            Serial.println("Light Off : " + String(ldrRead));
          } else {
            sentReturn = 2;
            Serial.println("Light On : " + String(ldrRead));
          } 
          stateWhile = false;
        }
      }
    }

    // ปล่อย CPU ให้ task อื่นระหว่างรออ่าน LDR รอบถัดไป
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  Serial.println("Check light result: " + String(sentReturn));
  return sentReturn;
}

void shootTemp()
{
  if (program == 1)
  {
    display.setSegments(SEG_0Temp);
  }
  else if (program == 2)
  {
    display.setSegments(SEG_40Temp);
  }
  else if (program == 3)
  {
    if (Mode == 1 || Mode == 6)
    {
      display.showNumberDec(95, false, 2, 0);
      display.setSegments(SEG_Temp, 2, 2);
      // display.setSegments(SEG_95Temp);
    }
    else
    {
      display.showNumberDec(90, false, 2, 0);
      display.setSegments(SEG_Temp, 2, 2);
    }
  }
}
void checkLdr1(){
  if(stateCheckLdr1){
    static unsigned long timerCheckLDR = millis();
    static LdrAvgSampler ldrSampler;
    static bool sampling = false;

    if (millis() - timerCheckLDR >= 900)
    {
      if(OldBoard == 1)
      {
        if (!sampling) {
          ldrSampler.begin(LDR1_PIN);
          sampling = true;
        }
      }
      else
      {
        int val = readLDRAverage(LDR1_PIN, LDR_AVG_SAMPLES, "checkLdr1");
        display.showNumberDec(val);
        timerCheckLDR = millis();
      }
    }

    if (sampling && OldBoard == 1) {
      int val = 0;
      if (ldrSampler.tick(&val)) {
        sampling = false;
        display.showNumberDec(val);
        printLdrSummary("checkLdr1", LDR1_PIN, val);
        timerCheckLDR = millis();
      }
    }
  }  
}
void checkLdr2(){
  if(stateCheckLdr2){
    static unsigned long timerCheckLDR = millis();
    static LdrAvgSampler ldrSampler;
    static bool sampling = false;

    if (millis() - timerCheckLDR >= 900)
    {
      if(OldBoard == 1)
      {
        if (!sampling) {
          ldrSampler.begin(LDR2_PIN);
          sampling = true;
        }
      }
      else
      {
        int val = readLDRAverage(LDR2_PIN, LDR_AVG_SAMPLES, "checkLdr2");
        display.showNumberDecEx(val, 0b01000000);
        timerCheckLDR = millis();
      }
    }

    if (sampling && OldBoard == 1) {
      int val = 0;
      if (ldrSampler.tick(&val)) {
        sampling = false;
        display.showNumberDecEx(val, 0b01000000);
        printLdrSummary("checkLdr2", LDR2_PIN, val);
        timerCheckLDR = millis();
      }
    }
  }  
}
void SetFirstHier()
{
  static int count_check_power = 0;
  static int count_check_power2 = 0;
  static LdrAvgSampler ldrSampler;
  static bool sampling = false;
  static unsigned long lastActionMs = 0;
  static const unsigned long actionGapMs = 500;

  unsigned long now = millis();
  if (!sampling && (now - lastActionMs) < actionGapMs) {
    return;
  }

  int val = 0;
  if (!sampling) {
    ldrSampler.begin(LDR2_PIN);
    sampling = true;
    return;
  }
  if (!ldrSampler.tick(&val)) {
    return;
  }
  sampling = false;
  lastActionMs = now;
  printLdrSummary("SetFirstHier check power", LDR2_PIN, val);

  if (OldBoard == 1)
  { // old board
    if (val > ldr_set)
    {
      if (stepHier == 1)
      {
        chanel = 3;
      }
      else if (stepHier == 2)
      {
        chanel = 7;
      }
      else if (stepHier == 3)
      {
        chanel = 8;
      }
      count_check_power = 0;
    }
    else
    {
      // Jok();
      JokBack();
      count_check_power++;
      if (count_check_power >= 15)
      {
        count_check_power2++;
        count_check_power = 0;
        if (count_check_power2 >= 5)
        {
          state_error = 0;
          chanel = 11; // send error
          count_check_power = 0;
          count_check_power2 = 0;
          reportEspFaultToMelody("00");
        }
        Power();
        // delay(2000);
      }
    }
  }
  else
  {
    if (val < ldr_set)
    {
      if (stepHier == 1)
      {
        chanel = 3;
      }
      else if (stepHier == 2)
      {
        chanel = 7;
      }
      else if (stepHier == 3)
      {
        chanel = 8;
      }
      count_check_power = 0;
    }
    else
    {
      Jok();
      count_check_power++;
      if (count_check_power >= 15)
      {
        count_check_power2++;
        count_check_power = 0;
        if (count_check_power2 >= 5)
        {
          state_error = 0;
          chanel = 11; // send error
          count_check_power = 0;
          count_check_power2 = 0;
          reportEspFaultToMelody("00");
        }
        Power();
        delay(2000);
      }
    }
  }
}

void taskDisplay(void *parameter)
{
  while (true)
  {
    hbDisplayMs = millis();  // heartbeat สำหรับ task-hang watchdog
    vTaskDelay(pdMS_TO_TICKS(1));  // yield ทันที ป้องกัน TG1WDT (watchdog)
    if (statePriceShow)
    {
      statePriceShow = false;
      setPriceShow();
    }
    checkLdr1();
    checkLdr2();
    updateBalanceIncreateDry();
    // Display.loop(); // Keep GUI work
    // secondMillis(); //update time
    updateWiFiIcon(); //update icon
    // countdownWait(); //countdown waiting doing
    count_update();      // coin active for runing
    prepareRunMachine(); // prepare runing
    machineRuning();     // maching runing
    CheckPromotion(); // check promotion
    // machingPrepare(); // wait for prepare machine
    buttonReset(); // reset machine
    setMode();     // set mode machine

    if (chanel == 0 && step == 0)
    {
      checkbuttonFirst();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS); // Check connection every 10 seconds
  }
}
void taskWifiMqtt(void *parameter)
{
  while (true)
  {
    hbWifiMs = millis();  // heartbeat สำหรับ task-hang watchdog
    vTaskDelay(pdMS_TO_TICKS(1));  // yield ทันที ป้องกัน TG1WDT (watchdog)
    g_mqttOnline = mqclient.connected();  // cache ให้ task จอ อ่าน (กัน recv ซ้อน -> pbuf crash)
    if (state_wifi_on)
    {
      otiUdate();

      const unsigned long wifiNow = millis();
      if (espWifiAssociated())
      {
        // หลัง WiFi กลับมา task นี้ไม่เรียก connectwifi() — ต้องเริ่ม warmup ที่นี่ด้วย
        if (wifiConnectedSinceMs == 0)
          noteWifiLinkUp(true);

        // ช่วง warmup: ยัง pump MQTT ถ้ายังต่ออยู่ (keepalive ระหว่างรอ stable)
        if (mqclient.connected() && !wifiLinkUsable())
        {
          if (netLockEnter())
          {
            mqttPumpLoopLocked(1);
            netLockLeave();
          }
        }

        statewifi = wifiLinkUsable();
        if (!statewifi)
        {
          static unsigned long lastWifiWarmupLogMs = 0;
          if (lastWifiWarmupLogMs == 0 ||
              (unsigned long)(wifiNow - lastWifiWarmupLogMs) >= 2000)
          {
            lastWifiWarmupLogMs = wifiNow;
            if (wifiConnectedSinceMs == 0) {
              Serial.println(F("[WiFi] connected but warmup not started; fixing next loop"));
            } else {
              unsigned long remain = wifiStableWindowMs();
              if ((unsigned long)(wifiNow - wifiConnectedSinceMs) < remain)
                remain -= (unsigned long)(wifiNow - wifiConnectedSinceMs);
              else
                remain = 0;
              Serial.print(F("[WiFi] warming up; postpone MQTT/HTTP ~"));
              Serial.print(remain);
              Serial.println(F("ms"));
            }
          }
        }
        else
        {
          mqttreconnect();  // เชื่อมต่อ MQTT ก่อน (ถ้ายังไม่ต่อ)
        }

        if (!mqclient.connected() && statewifi)
        {
          pollMelodyDeviceHttp();
        }

        static bool melodySetupPhaseEnded = false;
        if (!melodySetupPhaseEnded && !isMelodyBootSetupPhase())
        {
          melodySetupPhaseEnded = true;
          stateUpdateState = 1;
          Serial.println(F("[Melody] boot/setup complete — sync UpdateState"));
        }

        // หมดช่วง grace แล้วแต่ยังไม่เคยดึง config สำเร็จ — อย่าค้าง boot phase ตลอดไป
        if (firstGetdata &&
            melodyBootMs > 0 &&
            (unsigned long)(millis() - melodyBootMs) >= MELODY_BOOT_SETUP_GRACE_MS)
        {
          firstGetdata = false;
          Serial.println(F("[MQTT] boot grace ended — stop waiting for initial configRequest"));
        }

        // ส่ง configRequest เฉพาะเมื่อ MQTT เชื่อมต่อแล้ว และเป็นครั้งแรกหรือมีการขอ GetData
        if (stateGetdata || firstGetdata)
        {
          if (mqclient.connected())
          {
            stateGetdata = false;
            firstGetdata = false;
            GetData();  // ส่ง configRequest ครั้งเดียว แล้วรอ configResponse
          }
          // ถ้า MQTT ยังไม่ต่อ ยังไม่ล้าง firstGetdata จะรอรอบถัดไปจนกว่า MQTT จะเชื่อมต่อ
        }

        // boot: รอ config/promo ครบ debounce แล้วใช้ชุดสุดท้ายจาก Melody
        if (isMelodyBootSetupPhase() && bootMelodySyncQuietReady())
        {
          if (bootMelodyConfigPending)
          {
            bootMelodyConfigPending = false;
            Serial.println(F("[MQTT] ✅ ใช้ configResponse ชุดสุดท้ายจาก Melody -> GetSetupData()"));
            GetSetupData();
          }
          if (bootMelodyPromoPending)
          {
            bootMelodyPromoPending = false;
            Serial.println(F("[MQTT] ✅ ใช้ setPromoSlots ชุดสุดท้ายจาก Melody"));
            applyPromoSlotsPayload(bootMelodyPromoPayload);
            bootMelodyPromoPayload = "";
          }
        }

        if (stateSetupdata)
        {
          stateSetupdata = false;
          GetSetupData();
        }

        if (stateSendConfigMqtt && mqclient.connected() && wifiLinkUsable())
        {
          stateSendConfigMqtt = false;
          PublishConfigViaMqtt();
        }

        if (stateUpdateState)
        {
          if (mqclient.connected() && wifiLinkUsable())
          {
            // คิว UpdateState ไป processDeferredMqttWork — publish+pump ครั้งเดียวต่อรอบ (กัน lwIP pbuf crash)
            stateUpdateState = false;
            SetStatusControl();
            SetTimerSend();
            String msg = "{\"ID\":\"" + IDserver + "\",\"Title\":\"" + Noserial + "\",\"Status\":\"" + StatusControl + "\",\"Time\":\"" + TimeSent + "\"}";
            msg.toCharArray(pendingUpdateStateBuf, sizeof(pendingUpdateStateBuf));
            pendingUpdateStatePublish = true;
            Serial.println("update status and time ..!! :: " + StatusControl + " :: " + TimeSent + ": " + String(second) + " :: step : " + String(step));
          }
          else if (mqttDownForFallback() && !isMelodyBootSetupPhase())
          {
            // MQTT ล่มจริง (fail > 20 ครั้ง) → ส่งสถานะทาง HTTP แทน เพื่อให้ Melody เห็นลูกค้าใช้เครื่อง
            // ส่งสำเร็จค่อยเคลียร์ flag — ถ้า HTTP ก็ fail จะ retry ทุก 30 วิ (ไม่ทิ้งสถานะ)
            static unsigned long lastUpdateStateHttpMs = 0;
            if (lastUpdateStateHttpMs == 0 ||
                (unsigned long)(millis() - lastUpdateStateHttpMs) >= 30UL * 1000)
            {
              lastUpdateStateHttpMs = millis();
              SetStatusControl();
              SetTimerSend();
              if (sendUpdateStateHttp())
              {
                stateUpdateState = false;
              }
            }
          }
          // MQTT หลุดแต่ยังไม่เกิน 20 ครั้ง → คง flag ไว้ รอ mqttreconnect ก่อน (MQTT-first)
        }

        /*{
          "command": "1",
          "machineId": 91,
          "controllerId": "65M000000",
          "branchCode": "99"
        }*/

        if (stateSentPriceServer && pendingBalance > 0)
        {
          if (WiFi.status() == WL_CONNECTED && statewifi)
          {
            bool ok = false;
            if (mqclient.connected())
            {
              // MQTT ออนไลน์ — ส่งทาง MQTT เท่านั้น (ห้าม HTTP คู่กัน กัน lwIP pbuf crash)
              String msg = "{\"idEsp\":\"" + Noserial + "\",\"idUser\":\"" + String(gid) + "\",\"idBranch\":\"" + String(gid) + "\",\"price\":\"" + String(pendingBalance) + "\",\"typePay\":\"" + String("0") + "\"}";
              if (netLockEnter())
              {
                ok = mqclient.publish("postSQL", msg.c_str());
                mqttPumpLoopLocked(1);
                netLockLeave();
              }
            }
            else if (statewifi && mqttDownForFallback())
            {
              ok = UpdateBalanceV3(pendingBalance);
            }

            if (ok)
            {
              Serial.println("Send pending balance success. Clear buffer.");
              pendingBalance = 0;
              stateSentPriceServer = false;
            }
            else
            {
              Serial.println("Send pending balance failed. Will retry later.");
            }
          }
        }

        static bool midnight = false;
        if (rtc.getHour(true) == 0 && rtc.getMinute() == 0 && !midnight)
        {
          midnight = true;
          Serial.println("It's midnight! and setup time");
          setupTime();
        }
        else if (rtc.getHour(true) == 0 && rtc.getMinute() == 1 && midnight)
        {
          midnight = false;
          Serial.println("It's pass midnight! to ready check next time");
        }

        if (mqclient.connected() && statewifi)
        {
          static unsigned long lastPresenceHeartbeatMs = 0;
          if (lastPresenceHeartbeatMs == 0)
            lastPresenceHeartbeatMs = millis();
          if ((unsigned long)(millis() - lastPresenceHeartbeatMs) >= 5UL * 60 * 1000)
          {
            pendingPresenceHeartbeat = true;
            lastPresenceHeartbeatMs = millis();
          }
          processDeferredMqttWork();
        }
      }
      else if (espWifiDownConfirmed(wifiNow))
      {
        statewifi = false;
        if (wifiConnectedSinceMs != 0 || mqclient.connected())
          teardownMqttOnWifiDown();
        wifiConnectedSinceMs = 0;
        connectwifi();
      }
      else
      {
        // WiFi สะดุดสั้น (< hysteresis) — อย่าตัด MQTT; pump ถ้ายังต่ออยู่
        if (mqclient.connected())
        {
          if (netLockEnter())
          {
            mqttPumpLoopLocked(1);
            netLockLeave();
          }
        }
        connectwifi();
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
void taskProgram(void *parameter)
{
  // Serial.println("** task **");
  // printTocore();
  static unsigned long step3TimerEndMs = 0;
  auto handleRunSessionRecoveryChannel = []() {
    RunRecoveryResult r = runSessionTickRecovery();
    if (r.action == RUN_RECOVERY_CONTINUE) {
      return;
    }
    if (r.action == RUN_RECOVERY_ABORTED) {
      chanel = 0;
      statedisplaystandby = 0;
      return;
    }
    if (r.action == RUN_RECOVERY_RESUMED) {
      runSessionApplySnapshot(&r.snap);
      status_machine_run = true;
      status_machine_prepare = false;
      endProgram = false;
      statedisplaystandby = 3;  // งดวาด standby — ให้ machineRuning() โชว์ timer รอบที่กู้มา
      if (r.snap.mode == 2) {
        chanel = 0;             // อบ: machineRuning เดินเวลาโดยไม่ขึ้นกับ chanel
        Dry(1);
        if (OldBoard == 1) {
          Slot(0);
        } else {
          Slot(1);
        }
      } else {
        // ซัก: countdown อยู่ใน case 0 กับ step 1/2/3 (chanel 1/2/6/8/9 เป็น action ชั่วคราวแล้ววนกลับ case 0)
        // ถ้า reboot ช่วง startup (step ยังไม่ถูกตั้ง) ให้เริ่ม sequence ใหม่ที่ chanel 1 กัน step ค้างที่ 0
        chanel = (r.snap.step == 0) ? 1 : 0;
        if (OldBoard == 1) {
          Slot(1);
        } else {
          Slot(0);
        }
      }
      stateUpdateState = 1;
    }
  };
  while (true)
  {
    hbProgramMs = millis();  // heartbeat สำหรับ task-hang watchdog
    vTaskDelay(pdMS_TO_TICKS(1));  // yield ทันที ป้องกัน TG1WDT (watchdog)
    if (stateReset)
    {
      chanel = 10;
      endProgram = false;
    }

    //protect error
    if (Mode == 2 && !status_machine_run) {
      Dry(0); // stop dry machine
    } else if (Mode == 2 && status_machine_run) {
      Dry(1); // ค้าง relay ฮีตเตอร์ — ตรง ATD35
    }

    static LdrPeakWindow powerOnLdrPeak;
    static int count_check_power = 0;

    switch (chanel)
    {
    case CH_RECOVERY:
      handleRunSessionRecoveryChannel();
      if (!status_machine_run && !status_machine_prepare && step == 0)
      {
        if (statedisplaystandby == 3)
          statedisplaystandby = 0;
        if (statedisplaystandby == 0)
          standbyDisplay();
      }
      break;
    case 0: // statnby
      
      if (step == 0)
      { //
        if (statedisplaystandby == 0)
        {
          standbyDisplay();
          // checkbuttonFirst();
          // Serial.println("stanby display..");
          // displaystandby(); //display standby
        }
        else if (statedisplaystandby == 1)
        {
          display.showNumberDec(item_price, false, 3, 0);
          display.setSegments(SEG_Bath, 1, 3);
          // checkprice for runing
          checkpriceprogram();
          if (millis() - timerstanby >= 300000)
          {
            statedisplaystandby = 0;
            item_price = 0;
          }
        }
        else if (statedisplaystandby == 3)
        {
          // 3.32: not thing — state 3 คงไว้ตลอดตอนรัน/เตรียม ให้ machineRuning() โชว์ timer ที่เดียว
          // orphan จาก recovery จัดการที่ CH_RECOVERY / RUN_RECOVERY_ABORTED แล้ว (ไม่ปัด state ที่นี่ กัน race กับ taskDisplay)
        }
        else if (statedisplaystandby == 4)
        {
          display.setSegments(SEG_Up);
        }
        // check button first
        //  checkbuttonFirst();
      }
      else if (step == 1)
      { //

        if (!state_step2 && minn <= check_runing_time[0] && hrs == 0 && program != 4)
        {
          if (!drain_water)
          {
            drain_water = true;
            pause_timer = true;
            chanel = 6;
            runSessionSavePhase(RS_WASH_RUNNING);
          }
          else
          {
            if (millis() - timerstanby >= 75000)
            { // wait for drain
              state_step2 = true;
              drain_water = false;
              Start();
              delay(1500);

              if (CodeMachine == 4 || Mode == 3 || Mode == 4)
              { // closed
                Power();
                delay(2500);
                ClearJok();
              }

              if (Mode == 3 || Mode == 4)
              { // for Hier
                // Power();
                // delay(2500);
                chanel = 2;
                stepHier = 3; // for rin hier
              }
              else
              {
                chanel = 8;
              }
              runSessionSavePhase(RS_WASH_RUNNING);
            }
          }
        }
      }
      else if (step == 2)
      { //
        if (!state_step3 && minn <= check_runing_time[1] && hrs == 0)
        {
          state_step3 = true;
          if (rinStep2[1] == 2)
          {
            pause_timer = true;
            chanel = 9;
          }
          else if (rinStep2[1] == 1)
          {
            step = 3;
            timerstanby = millis();
            step3TimerEndMs = timerstanby;
          }
          runSessionSavePhase(RS_WASH_RUNNING);
        }
      }
      else if (step == 3)
      { //
        // state_step3 = false;
        if (minn <= check_runing_time[2])
        {
          // Serial.println("check ldr end state.. " + String(ldrEnd));
          if (step3TimerEndMs == 0) {
            step3TimerEndMs = millis();
          }
          if (millis() - timerstanby >= LDR_READ_INTERVAL_MS){
            static LdrAvgSampler endSampler;
            static bool endSampling = false;
            static bool endReady = false;
            static int valEnd = 0;

            if (!endSampling && !endReady) {
              endSampler.begin(ldrPin);
              endSampling = true;
            }
            if (endSampling && endSampler.tick(&valEnd)) {
              endSampling = false;
              endReady = true;
            }
            if (endReady) {
              endReady = false;
              printLdrSummary("check ldr end program", (uint8_t)ldrPin, valEnd);
              if (OldBoard == 1)
              { // old board
                if (valEnd < ldr_set - ldrMinus && millis() - step3TimerEndMs >= 3000)
                {
                  status_machine_run = false;
                  status_machine_prepare = false;
                  display.setSegments(SEG_DONE);
                  chanel = 10;
                  endProgram = true;
                }
                else if (valEnd > ldr_set)
                {
                  step3TimerEndMs = millis();
                }
              }
              else
              {
                if (valEnd > ldr_set + ldrMinus)
                {
                  if (millis() - step3TimerEndMs >= 3000)
                  {
                    status_machine_run = false;
                    status_machine_prepare = false;
                    display.setSegments(SEG_DONE);
                    chanel = 10;
                    endProgram = true;
                  }
                }
                else if (valEnd < ldr_set)
                {
                  step3TimerEndMs = millis();
                }
              }
              timerstanby = millis();
            }
          }
          
          // delay(600);
        }
      }
      break;
    case 1: // power
      Serial.println("Power is on..");
      powerOnLdrPeak.reset();
      Power();
      // delay(2500);
      chanel = 2;
      timerstanby = millis();
      break;
    case 2: // check power open?
      // Serial.println("check ldr power on.. " + String(analogRead(ldrPin)));
      if (Mode == 3 || Mode == 4)
      { // for Hier
        // delay(2000);
        if(stepHier == 3 && Mode == 4){
          // chanel = 8; //************************************************* */
          Power();
          delay(2500);
          for (int i = 0; i < 6; i++)
          {
            Jok();
          }
          for (int i = 0; i < spin; i++)
          {
            Spin();
          }
          delay(1000);
          Start();
          delay(1500);
          step = 2;
          chanel = 0;
          state_step3 = false;
          pause_timer = false; // timer is runing
          runSessionSavePhase(RS_WASH_RUNNING);
        }else if(stepHier == 3 && Mode == 3){
          Power();
          delay(2500);
          for (int i = 0; i < 7; i++)
          {
            Jok();
          }
          for (int i = 0; i < spin; i++)
          {
            Spin();
          }
          delay(1000);
          Start();
          delay(1500);
          step = 2;
          chanel = 0;
          state_step3 = false;
          pause_timer = false; // timer is runing
          runSessionSavePhase(RS_WASH_RUNNING);
        }
        else{
          SetFirstHier();
        }
      }
      else
      {
        if (OldBoard == 1)
        { // old board
          if (Mode == 1){
            // Mode 1: จับ peak ในหน้าต่างสั้น — ไฟเครื่องกระพริบ ค่าเฉลี่ยต่ำเกินไป แต่ peak ผ่านได้
            int val = analogRead(ldrPin);
            powerOnLdrPeak.push(val);
            int peak = powerOnLdrPeak.peak();
            static unsigned long lastPowerLdrLogMs = 0;
            if (lastPowerLdrLogMs == 0 || (unsigned long)(millis() - lastPowerLdrLogMs) >= 3000) {
              lastPowerLdrLogMs = millis();
              Serial.print("check ldr power on | LDR pin=");
              Serial.print(ldrPin);
              Serial.print(" now=");
              Serial.print(val);
              Serial.print(" peak=");
              Serial.println(peak);
            }
            if (val > ldr_set || peak > ldr_set){
              chanel = 3;
              count_check_power = 0;
            } else if(millis() - timerstanby >= 5000){
              chanel = 1;
              count_check_power++;
              if (count_check_power >= 5)
              {
                state_error = 0;
                chanel = 11; // send error
                count_check_power = 0;
                reportEspFaultToMelody("00");
              }
              timerstanby = millis();
            }
          }
          else
          {
            chanel = 3;
            count_check_power = 0;
          }
        }
        else
        { // new board
          if(Mode == 1){
            int val = analogRead(ldrPin);
            powerOnLdrPeak.push(val);
            int peak = powerOnLdrPeak.peak();
            static unsigned long lastPowerLdrLogNbMs = 0;
            if (lastPowerLdrLogNbMs == 0 || (unsigned long)(millis() - lastPowerLdrLogNbMs) >= 3000) {
              lastPowerLdrLogNbMs = millis();
              Serial.print("check ldr power on | LDR pin=");
              Serial.print(ldrPin);
              Serial.print(" now=");
              Serial.print(val);
              Serial.print(" peak=");
              Serial.println(peak);
            }
            if (val <= ldr_set || peak <= ldr_set)
            {
              chanel = 3;
              count_check_power = 0;
            }
            else if(millis() - timerstanby >= 5000){
              chanel = 1;
              count_check_power++;
              if (count_check_power >= 5)
              {
                state_error = 0;
                chanel = 11; // send error
                count_check_power = 0;
                reportEspFaultToMelody("00");
              }
              timerstanby = millis();
            }
          }else{
            chanel = 3;
            count_check_power = 0;
          }
        }  
      }
      break;
    case 3: // shoot the program
      Serial.println("setProgram..");
      setProgram();
      chanel = 4;
      break;
    case 4: // start
      Serial.println("start is runing..");
      Start();
      delay(1500);
      chanel = 5;
      timerstanby = millis();
      break;
    case 5: // check start runing
      // Serial.println("check ldr start runing.. " + String(analogRead(LDR1_PIN)));
      static int count_start = 0;
      if(Mode == 1){
        if (checkLightStart(0, true) == 2){
          chanel = 0;
          if (program == 5)
          {
            step = 2;
          }
          else if (program == 6)
          {
            step = 3;
          }
          else
          {
            step = 1;
          }
          count_start = 0;
          // state_step2 = false;
          status_machine_run = true;
          pause_timer = false;
          runSessionSavePhase(RS_WASH_RUNNING);
        }
        else
        {
          chanel = 4;
          count_start++;
          if (count_start >= 5)
          {
            count_start = 0;
            state_error = 2;
            chanel = 11; // send error
            reportEspFaultToMelody("02");
          }
        }
      }else{
        if (checkLightStart(0, true) == 2){
          chanel = 0;
          if (program == 5)
          {
            step = 2;
          }
          else if (program == 6)
          {
            step = 3;
          }
          else
          {
            step = 1;
          }
          count_start = 0;
          // state_step2 = false;
          status_machine_run = true;
          pause_timer = false;
          runSessionSavePhase(RS_WASH_RUNNING);
        }
        else
        {
          chanel = 1;
          count_start++;
          if (count_start >= 5)
          {
            count_start = 0;
            state_error = 0;
            chanel = 11; // send error
            reportEspFaultToMelody("00");
          }
        }
      }
      
      break;
    case 6: // closed machine and on power go to step2
      Serial.println("power off for step2..");
      Start();
      delay(1500);
      Power();
      delay(2500);
      if (CodeMachine == 4 || Mode == 3)
      {
        ClearJok();
      }
      Power();
      delay(2500);
      if (Mode == 3 || Mode == 4)
      { // for Hier or 15kgNew
        chanel = 2;
        stepHier = 2; // for drian
        runSessionSavePhase(RS_WASH_RUNNING);
      }
      else
      {
        chanel = 7;
      }
      break;
    case 7: // spin drain water
      Serial.println("drain water is runing..");
      if (Mode == 3 || Mode == 4)
      { // for Hier
        for (int i = 0; i < spinHier; i++)
        {
          JokBack();
        }
      }
      else
      {
        if (CodeMachine == 4)
        { // for 15kgNew
          TempSpin();
        }
        else
        {
          Spin();
        }
      }
      delay(1000);
      Start();
      delay(1500);
      chanel = 0;
      timerstanby = millis();
      break;
    case 8: // step 2 for rin
      Serial.println("rin is runing..");
      if (CodeMachine == 4 || Mode == 3 || Mode == 4)
      { // for 15kgNew && hier
        Power();
        delay(2500);
      }
      //------------------------------
      if (rinStep2[1] == 1)
      {
        for (int i = 0; i < rinStep2[0]; i++)
        {
          if (CodeMachine == 4)
          { // for 15kgNew
            JokBack();
          }
          else
          {
            Jok();
          }
        }
        for (int i = 0; i < spin; i++)
        {
          Spin();
        }
      }
      else if (rinStep2[1] == 2)
      {
        for (int i = 0; i < rinStep2[0]; i++)
        {
          if (CodeMachine == 4)
          { // for 15kgNew
            JokBack();
          }
          else
          {
            Jok();
          }
        }
      }
      delay(1000);
      Start();
      delay(1500);
      step = 2;
      chanel = 0;
      state_step3 = false;
      pause_timer = false; // timer is runing
      runSessionSavePhase(RS_WASH_RUNNING);
      break;
    case 9: // closed machine and on power go to step3
      Serial.println("power off for step3..");
      Start();
      delay(1500);
      Power();
      delay(2500);
      if (CodeMachine == 4)
      {
        ClearJok();
      }
      Power();
      delay(2500);
      if (CodeMachine == 4)
      { // for 15kgNew
        TempSpin();
      }
      for (int i = 0; i < spin; i++)
      { // select program machine
        Spin();
      }
      Start();
      chanel = 0;
      step = 3;
      pause_timer = false;
      timerstanby = millis();
      runSessionSavePhase(RS_WASH_RUNNING);
      break;
    case 10: // reset system
      runSessionClear();
      // display.setSegments(SEG_Mode);
      if (!endProgram & Mode != 2)
      {
        if (checkLightStart(0, true) != 0)
        {
          Start();
          delay(1500);
          Power();
        }
      }

      count = 0;
      step = 0;
      hrs = 0;
      minn = 0;
      program = 0;
      item_price = 0;
      statedisplaystandby = 0;
      count_minn_pass = 0;
      step3TimerEndMs = 0;
      // priceSentServer = 0;
      stateReset = false;
      chanel = 0;
      status_machine_run = false;
      status_machine_prepare = false;
      // digitalWrite(EN_PIN, LOW);
      if (CodeMachine == 4)
      {
        ClearJok();
      }
      if (OldBoard == 1)
      {
        Slot(0);
      }
      else
      {
        Slot(1);
      }
      // Slot(1); // ready inserch coin
      Dry(0);
      StatusControl = "off";
      stateUpdateState = 1;
      Serial.println("Reset and end program..!!");
      delay(1000);
      break;
    case 11: // command
      if (state_error == 0 || state_error == 1 || state_error == 2)
      {
        if (state_error == 0)
        { // not power
          display.setSegments(SEG_00);
        }
        else if (state_error == 1)
        { // not run spin
          display.setSegments(SEG_01);
        }
        else if (state_error == 2)
        { // door
          display.setSegments(SEG_02);
        }
      }
      else if (state_error == 3)
      {
        if (timerstanby == 0 || (millis() < timerstanby || millis() - timerstanby >= 500))
        {
          display.showNumberDec(readLDRAverage(ldrPin, LDR_AVG_SAMPLES, "state_error LDR display"));
          timerstanby = millis();
        }
      }
      else if (state_error == 4)
      {
        // for dryer
      }
      else if (state_error == 5)
      { // shutdown
        display.setSegments(off);
      }
      break;
    case 12: // setting
      Button();
      modeSetting();
      // delay(300);
      break;
    case 13: // seting Mode
      display.showNumberDec(Mode);
      if (digitalRead(sw_pin[1]) == LOW)
      { // sw2
        while (digitalRead(sw_pin[1]) == LOW)
        {
          vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
        }
        delay(300);
        Mode--;
        if (Mode < -1)
        {
          Mode = 7;
        }
        Serial.println("Mode : " + String(Mode));
      }
      else if (digitalRead(sw_pin[2]) == LOW)
      { // sw3
        while (digitalRead(sw_pin[2]) == LOW)
        {
          vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
        }
        delay(300);
        Mode++;
        if (Mode >= 8)
        {
          Mode = 0;
        }
        Serial.println("Mode : " + String(Mode));
      }
      else if (digitalRead(sw_pin[0]) == LOW)
      { // sw1
        while (digitalRead(sw_pin[0]) == LOW)
        {
          vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
        }
        delay(300);
        preferences.begin("config", false); // สร้างพื้นที่เก็บข้อมูลชื่อ "config"
        preferences.putInt("Mode", Mode);   // บันทึกค่า Mode ลงใน Preferences
        preferences.end();                  // ปิด Preferences
        chanel = 0;
      }
      break;
    }

    if (millis() < timerstanby)
    {
      timerstanby = millis();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); // Check connection every 10 seconds
  }
}

// WiFi ทั้งหมดใน taskWifiMqtt ผ่าน connectwifi() — ไม่บล็อกใน setup()
void setPriceShow()
{
  for (size_t i = 0; i < 3; i++)
  {
    PriceShow[i] = price[i];
  }
}
void setup()
{
  // ตั้งค่า WiFi ก่อน yield ใดๆ เพื่อให้ idle/PM เห็นสถานะที่รู้จัก (ลด crash ใน esp_pm_impl_waiti)
  WiFi.mode(WIFI_STA);

  Serial.begin(115200);
  Serial.print("[FW] ");
  Serial.print(fwversion[0]);
  Serial.print("[FW] ");
  Serial.println(fwversion[1]);
  Serial.print(F("[Chip] "));
  Serial.println(ESP.getChipModel());
  melodyBootMs = millis();

  // ปิด automatic light sleep — ESP32 classic เท่านั้น (esp_pm_config_esp32_t ไม่ใช้บน S3)
#if CONFIG_IDF_TARGET_ESP32
  esp_pm_config_esp32_t pm_cfg;
  pm_cfg.max_freq_mhz = 240;
  pm_cfg.min_freq_mhz = 10;
  pm_cfg.light_sleep_enable = false;
  (void)esp_pm_configure(&pm_cfg);  // ถ้า CONFIG_PM_ENABLE ปิด จะ return ESP_ERR_NOT_SUPPORTED
#endif

  pinMode(SIG_PIN, INPUT);  // กำหนดขา SIG เป็นอินพุต
  pinMode(SIG_PIN2, INPUT); // กำหนดขา SIG เป็นอินพุต
  // pinMode(TX_PIN, OUTPUT); // กำหนดขา TX เป็นเอาต์พุต
  // attachInterrupt(digitalPinToInterrupt(pinSlot), pulse_in_cb, FALLING); // เปิดใช้อินเตอร์รัพท์ภายนอก
  pinMode(EN_PIN, OUTPUT); // กำหนดขา EN เป็นอินพุต
  // digitalWrite(EN_PIN, HIGH); // สั่งให้ขา EN เป็นลอจิก 1 (HIGH) เพื่อให้รับเหรียญ (12V output enable)
  if(OldBoard == 0)
  {
    pinMode(wifiLed, OUTPUT);
  }
  for (int pin : relay_pin)
  {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  for (int pin : sw_pin)
  {
    pinMode(pin, INPUT_PULLUP);
  }
  setupLdrAdc(LDR1_PIN, LDR2_PIN);

  //===== Set TM1637 ============
  uint8_t data[] = {0xff, 0xff, 0xff, 0xff};
  uint8_t blank[] = {0x00, 0x00, 0x00, 0x00};
  display.setBrightness(7);

  preferences.begin("config", false);  // เปิด Preferences ในโหมดเขียนได้
    bool preSetupData = preferences.isKey("SetupData");
    bool preNoserial = preferences.isKey("Noserial");
  preferences.end();
  if (!preSetupData) {
      Serial.println("⚠️ SetupData ยังไม่มีข้อมูล -> บันทึกค่าเริ่มต้น");
      // preferences.putInt("SetupData", 0);
      writePreferences();
  }
  if (!preNoserial)
  {
    EEPROM.begin(EEPROM_SIZE);
    if (tryLoadIdentityFromEeprom())
      Serial.println("📦 ย้าย ID/WiFi/gid จาก EEPROM -> NVS: " + Noserial);
    else
      Serial.println("⚠️ Noserial ไม่พบใน NVS/EEPROM -> บันทึกค่าเริ่มต้นจากโค้ด");
    writePreferencesfirst();
    EEPROM.end();
  }
  delay(100);

  // SetupData = EEPROM.read(39);
  // Serial.println("******* SetupData : " + String(SetupData) + " *******");
  

  //****************************************************************** */
  // writePreferences();
  // writePreferencesfirst();
  //**************************************************************** */
  setupWaitAdminRestoreFactory();  // กดปุ่ม SETTING = คืนค่าโรงงาน (ก่อนอ่าน Preferences)
  readPreferencesfirst();
  yield();
  readPreferences();
  yield();
  if (SetupData == 1)
  {
    SetupData = 0;
    writePreferences();
    // PutEprom();
    delay(1000);
  }
  Serial.println("******* SetupData to : " + String(SetupData) + " *******");
  topic = "V" + String(gid);
  Serial.println("read from eeprom id : " + Noserial + ", ssid : " + ssidStr + ", pass : " + passStr + ", idsharepoint : " + IDserver + ", gid : " + topic);

  vTaskDelay(pdMS_TO_TICKS(50));  // feed watchdog ก่อนเขียน NVS (ป้องกัน TG1WDT)
  if (pinSlot != SIG_PIN && pinSlot != SIG_PIN2)
  {
    pinSlot = SIG_PIN;
    // EEPROM.write(1, pinSlot);//1
    // EEPROM.commit();
    writePreferences();
  }
  vTaskDelay(pdMS_TO_TICKS(20));  // feed watchdog หลังเขียน NVS

  attachInterrupt(digitalPinToInterrupt(pinSlot), pulse_in_cb, FALLING); // เปิดใช้อินเตอร์รัพท์ภายนอก

  yield();
  if (state_wifi_on)
  {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);           // ปิด WiFi sleep ลดโอกาสหลุด
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);         // ไม่เขียน config ทับ flash ทุกครั้ง
    client.setTimeout(4000);        // จำกัด TCP read/connect ตอน MQTT reconnect
    gNetMutex = xSemaphoreCreateRecursiveMutex();
    Serial.println(F("[WiFi] connect deferred to taskWifiMqtt (non-blocking boot)"));
  }
  delay(100);
  yield();
  // setupTime();

  chanel = 0;
  if (runSessionBeginRecovery(StateShutdown)) {
    chanel = CH_RECOVERY;
  }

  // Create a task for Display
  xTaskCreate(
      taskDisplay,        // Function to implement the task
      "taskDisplay",      // Name of the task
      1024 * 3,           // Stack size in words (เพิ่มเพื่อลดโอกาส stack overflow)
      NULL,               // Task input parameter
      1,                  // Priority of the task
      &taskDisplay_handle // Task handle
  );
  // Create a task for Program
  xTaskCreate(
      taskProgram,        // Function to implement the task
      "taskProgram",      // Name of the task
      1024 * 5,           // Stack size in words
      NULL,               // Task input parameter
      1,                  // Priority of the task
      &taskProgram_handle // Task handle
  );

  // check for shutdown mode
  if (StateShutdown == 1)
  {
    // go to shutdown: ลบ display + program, ไม่สร้าง task WiFi/MQTT
    suspendMachineTasksForOta();
    display.setSegments(off);
    if (OldBoard == 1)
    {
      Slot(1);
    }
    else
    {
      Slot(0);
    }
    // Slot(0);
    state_error = 5;
    chanel = 11;
  }
  else
  {
    // ready read slot + สร้าง task WiFi/MQTT เฉพาะเมื่อไม่ใช่โหมด shutdown
    if (OldBoard == 1)
    {
      Slot(0);
    }
    else
    {
      Slot(1);
    }
    xTaskCreatePinnedToCore(
        taskWifiMqtt,   // Function to implement the task
        "taskWifiMqtt", // Name of the task
        1024 * 10,      // Stack size in words (เพิ่มสำหรับ OTA/HTTP ขนาดใหญ่)
        NULL,           // Task input parameter
        1,              // Priority of the task
        &taskWifiMqtt_handle, // Task handle — ใช้เฝ้า hang watchdog
        1                    // Core 1 — MQTT/TCP block ห้ามอยู่ core 0 (IDLE0 TWDT; ATD35 ใช้ core 1)
    );
  }

  setPriceShow();
  if (state_wifi_on)
  {
    firstGetdata = true;
  }
  if (chanel != CH_RECOVERY && !status_machine_run && !status_machine_prepare)
    primeBootStandbyDisplay();
}
// เฝ้าดู task ค้าง (WiFi/MQTT/จอชนกัน หรือ bit-bang TM1637 ค้าง) — รีบูทกู้ตัวเอง
// run_session (v3.42+) จะกู้รอบซัก/อบต่อหลังรีบูท จึงปลอดภัยที่จะรีสตาร์ตแม้กำลังทำงาน
static void checkTaskHang()
{
  unsigned long now = millis();

  // ช่วง boot — ให้ WiFi/MQTT ต่อได้ก่อน อย่า false-trigger จาก millis/hb race
  if (melodyBootMs > 0 &&
      (unsigned long)(now - melodyBootMs) < BOOT_HANG_GRACE_MS)
  {
    return;
  }

  // ระหว่าง OTA: machine task ถูกลบ + wifi task ดาวน์โหลดบล็อกนาน -> reset heartbeat กัน false trigger
  if (otaInProgress || UpdateFw)
  {
    hbDisplayMs = hbProgramMs = hbWifiMs = now;
    return;
  }

  // เครื่องกำลังทำงาน/เตรียม -> ห้ามรีบูทเองเด็ดขาด (ตรงพฤติกรรม 3.00 ที่ไม่มี watchdog)
  // กันตัดกลางรอบซัก/อบจาก false-trigger (เช่น busy-loop checkLightStart, relay delay, MQTT block)
  // feed heartbeat ไว้ด้วย เพื่อไม่ให้ค้างสะสมแล้วรีบูททันทีหลังจบงาน
  if (status_machine_run || status_machine_prepare)
  {
    hbDisplayMs = hbProgramMs = hbWifiMs = now;
    return;
  }

  // heap ต่ำวิกฤติต่อเนื่อง (fragmentation ระยะยาว) -> รีบูทกัน alloc fail/crash
  static unsigned long lowHeapSinceMs = 0;
  if (ESP.getFreeHeap() < LOW_HEAP_CRITICAL_BYTES)
  {
    if (lowHeapSinceMs == 0)
      lowHeapSinceMs = now;
    else if (hangElapsedMs(now, lowHeapSinceMs, LOW_HEAP_REBOOT_MS))
    {
      Serial.print(F("[WDT] low heap -> restart, free="));
      Serial.println((uint32_t)ESP.getFreeHeap());
      Serial.flush();
      delay(100);
      ESP.restart();
    }
  }
  else
  {
    lowHeapSinceMs = 0;
  }

  const char *stuck = nullptr;
  if (taskDisplay_handle != NULL && hbDisplayMs != 0 &&
      hangElapsedMs(now, hbDisplayMs, TASK_HANG_TIMEOUT_MS))
    stuck = "taskDisplay";
  else if (taskProgram_handle != NULL && hbProgramMs != 0 &&
           hangElapsedMs(now, hbProgramMs, TASK_HANG_TIMEOUT_MS))
    stuck = "taskProgram";
  else if (taskWifiMqtt_handle != NULL && hbWifiMs != 0 &&
           hangElapsedMs(now, hbWifiMs, WIFI_TASK_HANG_TIMEOUT_MS))
    stuck = "taskWifiMqtt";

  if (stuck != nullptr)
  {
    Serial.print(F("[WDT] task hang detected -> restart: "));
    Serial.print(stuck);
    Serial.print(F(" now="));
    Serial.print(now);
    Serial.print(F(" hbDisplay="));
    Serial.print(hbDisplayMs);
    Serial.print(F(" hbProgram="));
    Serial.print(hbProgramMs);
    Serial.print(F(" hbWifi="));
    Serial.println(hbWifiMs);
    Serial.flush();
    delay(100);
    ESP.restart();
  }
}

void loop()
{
  // งานหลักของระบบอยู่ใน FreeRTOS tasks แล้ว — loop() ทำหน้าที่ watchdog เฝ้า task ค้าง
  // เช็คทุก 10 วิ (ไม่ต้องถี่ 1 วิ) ลดโอกาสรีบูทเอง; ตอนเครื่องทำงาน checkTaskHang จะไม่รีบูทอยู่แล้ว
  static unsigned long lastHangCheckMs = 0;
  unsigned long now = millis();
  if ((unsigned long)(now - lastHangCheckMs) >= 10000)
  {
    lastHangCheckMs = now;
    checkTaskHang();
  }
  delay(10);
}

void modeSetting()
{
  switch (indexSet)
  {
  case 0:
    settingMode1();
    break;
  case 1:
    settingMode2();
    break;
  case 2:
    settingMode3();
    break;
  case 3:
    Drysetting();
    break;
  case 4:
    Anothersetting();
    break;
  case 5:
    Test1();
    break;
  case 6:
    CommandProgram();
    break;
  }
}
void settingMode1()
{
  if (Mode1 == 0)
  {
    display.setSegments(SEG_Pr01);
  }
  else if (Mode1 == 1)
  {
    display.setSegments(SEG_Pr02);
  }
  else if (Mode1 == 2)
  {
    display.setSegments(SEG_Pr03);
  }
  else if (Mode1 == 3)
  {
    display.setSegments(SEG_Drum);
  }
  else if (Mode1 == 4)
  {
    display.setSegments(DRYE_TEXT);
  }
  else if (Mode1 == 5)
  {
    display.setSegments(ANOT_TEXT);
  }
  else if (Mode1 == 6)
  {
    display.setSegments(SEG_test);
  }
  else if (Mode1 == 7)
  {
    display.setSegments(SEG_Prun);
  }
  // Button();
  if (BT == 3)
  {
    BT = 0;
    Mode1++;
    if (Mode1 >= 8)
    {
      Mode1 = 0;
    }
  }
  else if (BT == 2)
  {
    BT = 0;
    Mode1--;
    if (Mode1 <= -1)
    {
      Mode1 = 7;
    }
  }
  else if (BT == 4)
  {
    BT = 0;
    if (Mode1 == 4)
    {
      indexSet = 3;
    }
    else if (Mode1 == 5)
    {
      indexSet = 4;
    }
    else if (Mode1 == 6)
    {
      indexSet = 5;
    }
    else if (Mode1 == 7)
    {
      indexSet = 6;
    }
    else
    {
      Serial.println("Mode1 => " + String(Mode1));
      indexSet = 1;
      Mode2 = 0;
    }
  }
  else if (BT == 1)
  {
    BT = 0;
    Mode1 = 0;
    chanel = 0;
  }
}
void settingMode2()
{
  if (Mode2 == 0)
  {
    display.setSegments(PRIC_TEXT);
  }
  else if (Mode2 == 1)
  {
    display.setSegments(PROG_TEXT);
  }
  else if (Mode2 == 2)
  {
    display.setSegments(TEMP_TEXT);
  }
  else if (Mode2 == 3)
  {
    display.setSegments(STAT_TEXT);
  }
  else if (Mode2 == 4)
  {
    display.setSegments(THRS_TEXT);
  }
  else if (Mode2 == 5)
  {
    display.setSegments(TMIN_TEXT);
  }

  // Button();
  if (BT == 3)
  {
    BT = 0;
    Mode2++;
    if (Mode2 >= 6)
    {
      Mode2 = 0;
    }
  }
  else if (BT == 2)
  {
    BT = 0;
    Mode2--;
    if (Mode2 <= -1)
    {
      Mode2 = 5;
    }
  }
  else if (BT == 4)
  {
    BT = 0;
    if (Mode1 == 0)
    {
      Price = price[0];
      for (int i = 0; i < 3; i++)
      {
        R[i] = program1[i];
      }
      for (int i = 0; i < 2; i++)
      {
        T[i] = TimeCountdown1[i];
      }
    }
    else if (Mode1 == 1)
    {
      Price = price[1];
      for (int i = 0; i < 3; i++)
      {
        R[i] = program2[i];
      }
      for (int i = 0; i < 2; i++)
      {
        T[i] = TimeCountdown2[i];
      }
    }
    else if (Mode1 == 2)
    {
      Price = price[2];
      for (int i = 0; i < 3; i++)
      {
        R[i] = program3[i];
      }
      for (int i = 0; i < 2; i++)
      {
        T[i] = TimeCountdown3[i];
      }
    }
    else if (Mode1 == 3)
    { // drum clean
      Price = 0;
      for (int i = 0; i < 3; i++)
      {
        R[i] = drum[i];
      }
      for (int i = 0; i < 2; i++)
      {
        T[i] = TimeCountdowndrum[i];
      }
    }
    indexSet = 2;
  }
  else if (BT == 1)
  {
    BT = 0;
    indexSet = 0;
  }
}
void settingMode3()
{
  if (Mode2 == 0)
  {
    display.showNumberDec(Price);
  }
  else if (Mode2 == 1)
  {
    display.showNumberDec(R[0]);
  }
  else if (Mode2 == 2)
  {
    ;
    display.showNumberDec(R[1]);
  }
  else if (Mode2 == 3)
  {
    display.showNumberDec(R[2]);
  }
  else if (Mode2 == 4)
  {
    display.showNumberDec(T[0]);
  }
  else if (Mode2 == 5)
  {
    display.showNumberDec(T[1]);
  }

  // Button();
  if (BT == 3)
  {
    BT = 0;
    if (Mode2 == 0)
    {
      Price = Price + 10;
      if (Price >= 110)
      {
        Price = 0;
      }
    }
    else if (Mode2 == 1)
    {
      R[Mode2 - 1]++;
      if (R[Mode2 - 1] >= 16)
      {
        R[Mode2 - 1] = 0;
      }
    }
    else if (Mode2 == 2)
    {
      R[Mode2 - 1]++;
      if (R[Mode2 - 1] >= 16)
      {
        R[Mode2 - 1] = 0;
      }
    }
    else if (Mode2 == 3)
    {
      R[Mode2 - 1]++;
      if (R[Mode2 - 1] >= 16)
      {
        R[Mode2 - 1] = 0;
      }
    }
    else if (Mode2 == 4)
    {
      T[0]++;
      if (T[0] >= 5)
      {
        T[0] = 0;
      }
    }
    else if (Mode2 == 5)
    {
      T[1] = T[1] + 5;
      if (T[1] >= 60)
      {
        T[1] = 0;
      }
    }
  }
  else if (BT == 2)
  {
    BT = 0;
    if (Mode2 == 0)
    {
      Price = Price - 10;
      if (Price <= -10)
      {
        Price = 100;
      }
    }
    else if (Mode2 == 1)
    {
      R[Mode2 - 1]--;
      if (R[Mode2 - 1] <= -1)
      {
        R[Mode2 - 1] = 15;
      }
    }
    else if (Mode2 == 2)
    {
      R[Mode2 - 1]--;
      if (R[Mode2 - 1] <= -1)
      {
        R[Mode2 - 1] = 15;
      }
    }
    else if (Mode2 == 3)
    {
      R[Mode2 - 1]--;
      if (R[Mode2 - 1] <= -1)
      {
        R[Mode2 - 1] = 15;
      }
    }
    else if (Mode2 == 4)
    {
      T[0]--;
      if (T[0] <= -1)
      {
        T[0] = 5;
      }
    }
    else if (Mode2 == 5)
    {
      T[1] = T[1] - 5;
      if (T[1] <= -5)
      {
        T[1] = 55;
      }
    }
  }
  else if (BT == 1)
  {
    BT = 0;
    if (Mode1 == 0)
    {
      price[0] = Price;
      for (int i = 0; i < 3; i++)
      {
        program1[i] = R[i];
      }
      for (int i = 0; i < 2; i++)
      {
        TimeCountdown1[i] = T[i];
      }
    }
    else if (Mode1 == 1)
    {
      price[1] = Price;
      for (int i = 0; i < 3; i++)
      {
        program2[i] = R[i];
      }
      for (int i = 0; i < 2; i++)
      {
        TimeCountdown2[i] = T[i];
      }
    }
    else if (Mode1 == 2)
    {
      price[2] = Price;
      for (int i = 0; i < 3; i++)
      {
        program3[i] = R[i];
      }
      for (int i = 0; i < 2; i++)
      {
        TimeCountdown3[i] = T[i];
      }
    }
    else if (Mode1 == 3)
    {
      // price[1] = Price;
      for (int i = 0; i < 3; i++)
      {
        drum[i] = R[i];
      }
      for (int i = 0; i < 2; i++)
      {
        TimeCountdowndrum[i] = T[i];
      }
    }
    // PutEprom(); // Save
    writePreferences();
    statePriceShow = true;
    indexSet = 1;
  }
}
void Anothersetting()
{
  if (Mode2 == 0)
  {
    display.setSegments(RS_TEXT, 2, 0);
    display.showNumberDec(rinStep2[0], false, 2, 2);
  }
  else if (Mode2 == 1)
  {
    display.setSegments(RS_TEXT, 2, 0);
    display.showNumberDec(rinStep2[1], false, 2, 2);
  }
  else if (Mode2 == 2)
  {
    display.setSegments(RC_TEXT, 2, 0);
    display.showNumberDec(rincommand[0], false, 2, 2);
  }
  else if (Mode2 == 3)
  {
    display.setSegments(RC_TEXT, 2, 0);
    display.showNumberDec(rincommand[1], false, 2, 2);
  }
  else if (Mode2 == 4)
  {
    display.setSegments(SP_TEXT, 2, 0);
    display.showNumberDec(spin, false, 2, 2);
  }
  else if (Mode2 == 5)
  {
    display.setSegments(SEG_SpinHier, 2, 0);
    display.showNumberDec(spinHier, false, 2, 2);
  }
  else if (Mode2 == 6)
  {
    display.setSegments(T1_TEXT, 2, 0);
    display.showNumberDec(check_runing_time[0], false, 2, 2);
  }
  else if (Mode2 == 7)
  {
    display.setSegments(T2_TEXT, 2, 0);
    display.showNumberDec(check_runing_time[1], false, 2, 2);
  }
  else if (Mode2 == 8)
  {
    display.setSegments(T3_TEXT, 2, 0);
    display.showNumberDec(check_runing_time[2], false, 2, 2);
  }
  else if (Mode2 == 9)
  {
    display.setSegments(LR_TEXT, 2, 0);
    display.showNumberDec(ldr_set / 100, false, 2, 2);
  }
  else if (Mode2 == 10)
  {
    display.setSegments(SEG_Lrmin, 2, 0);
    display.showNumberDec(ldrMinus / 100, false, 2, 2);
  }
  else if (Mode2 == 11)
  {
    display.setSegments(MQ_TEXT, 2, 0);
    display.showNumberDec(mqttStatus, false, 2, 2);
  }
  else if (Mode2 == 12)
  {
    display.setSegments(SEG_Cod, 3, 0);
    display.showNumberDec(CodeMachine, false, 1, 3);
  }
  else if (Mode2 == 13)
  {
    display.setSegments(MWiFi_TEXT, 2, 0);
    display.showNumberDec(state_wifi_on, false, 2, 2);
  }
  else if (Mode2 == 14)
  {
    display.setSegments(SEG_SlotPin, 2, 0);
    display.showNumberDec(pinSlot, false, 2, 2);
  }
  // update program
  else if (Mode2 == 15)
  {
    display.setSegments(SEG_Up);
  }
  else if (Mode2 == 16)
  {
    display.setSegments(SEG_it);
  }

  // Button();
  if (BT == 4)
  {
    BT = 0;
    Mode2++;
    if (Mode2 >= 17)
    {
      Mode2 = 0;
    }
  }
  else if (BT == 3)
  {
    BT = 0;
    if (Mode2 == 0)
    {
      rinStep2[0]++;
    }
    else if (Mode2 == 1)
    {
      rinStep2[1]++;
    }
    else if (Mode2 == 2)
    {
      rincommand[0]++;
    }
    else if (Mode2 == 3)
    {
      rincommand[1]++;
    }
    else if (Mode2 == 4)
    {
      spin++;
    }
    else if (Mode2 == 5)
    {
      spinHier++;
    }
    else if (Mode2 == 6)
    {
      check_runing_time[0]++;
    }
    else if (Mode2 == 7)
    {
      check_runing_time[1]++;
    }
    else if (Mode2 == 8)
    {
      check_runing_time[2]++;
    }
    else if (Mode2 == 9)
    {
      ldr_set = ldr_set + 100;
    }
    else if (Mode2 == 10)
    {
      mqttStatus++;
      if (mqttStatus > 2)
      {
        mqttStatus = 2;
      }
    }
    else if (Mode2 == 11)
    {
      ldrMinus = ldrMinus + 100;
    }
    else if (Mode2 == 12)
    {
      CodeMachine++;
      if (CodeMachine >= 9)
      {
        CodeMachine = 0;
      }
    }
    else if (Mode2 == 13)
    {
      if (state_wifi_on)
      {
        state_wifi_on = false;
      }
      else
      {
        state_wifi_on = true;
      }
    }
    else if (Mode2 == 14)
    {
      if (pinSlot == SIG_PIN)
      {
        pinSlot = SIG_PIN2;
      }
      else if (pinSlot == SIG_PIN2)
      {
        pinSlot = SIG_PIN;
      }
    }
    else if (Mode2 == 15)
    {
      if (!UpdateFw)
      {
        // digitalWrite(EN_PIN, LOW);
        if (OldBoard == 1)
        {
          Slot(1);
        }
        else
        {
          Slot(0);
        }
        // Slot(0);
        UpdateFw = true;
        delay(1000);
      }
    }
    else if (Mode2 == 16)
    {
      sentVarjson();
    }
  }
  else if (BT == 2)
  {
    BT = 0;
    if (Mode2 == 0)
    {
      rinStep2[0]--;
    }
    else if (Mode2 == 1)
    {
      rinStep2[1]--;
    }
    else if (Mode2 == 2)
    {
      rincommand[0]--;
    }
    else if (Mode2 == 3)
    {
      rincommand[1]--;
    }
    else if (Mode2 == 4)
    {
      spin--;
    }
    else if (Mode2 == 5)
    {
      spinHier--;
    }
    else if (Mode2 == 6)
    {
      check_runing_time[0]--;
    }
    else if (Mode2 == 7)
    {
      check_runing_time[1]--;
    }
    else if (Mode2 == 8)
    {
      check_runing_time[2]--;
    }
    else if (Mode2 == 9)
    {
      ldr_set = ldr_set - 100;
    }
    else if (Mode2 == 10)
    {
      mqttStatus--;
      if (mqttStatus < 1)
      {
        mqttStatus = 1;
      }
    }
    else if (Mode2 == 11)
    {
      ldrMinus = ldrMinus - 100;
    }
    else if (Mode2 == 12)
    {
      CodeMachine--;
      if (CodeMachine <= -1)
      {
        CodeMachine = 8;
      }
    }
    else if (Mode2 == 13)
    {
      if (state_wifi_on)
      {
        state_wifi_on = false;
      }
      else
      {
        state_wifi_on = true;
      }
    }
    else if (Mode2 == 14)
    {
      if (pinSlot == SIG_PIN)
      {
        pinSlot = SIG_PIN2;
      }
      else if (pinSlot == SIG_PIN2)
      {
        pinSlot = SIG_PIN;
      }
    }
  }
  else if (BT == 1)
  {
    BT = 0;
    // PutEprom(); // Save
    writePreferences();
    // setRelayType();
    indexSet = 0;
  }
}
void Test1()
{
  static int val;
  static unsigned long timeLdrTeast = millis();
  switch (Mode3)
  {
  case 0:
    display.setSegments(SEG_P);
    break;
  case 1:
    display.setSegments(SEG_J);
    break;
  case 2:
    display.setSegments(SEG_TEMP);
    break;
  case 3:
    display.setSegments(SEG_SPIN);
    break;
  case 4:
    display.setSegments(SEG_S);
    break;
  case 5:
    display.setSegments(DRYE_TEXT);
    break;
  case 6:
    stateCheckLdr1 = true;
    checkLdr1();
    break;
  case 7:
    // Show value with colon using showNumberDecEx
    stateCheckLdr2 = true;
    checkLdr2();
    break;
  }
  //************************ */
  if (BT == 3)
  {
    BT = 0;
    Mode3++;
    if (Mode3 >= 8)
    {
      Mode3 = 0;
    }
  }
  else if (BT == 2)
  {
    BT = 0;
    Mode3--;
    if (Mode3 <= -1)
    {
      Mode3 = 7;
    }
  }
  else if (BT == 4)
  {
    BT = 0;
    if (Mode3 == 0)
    {
      Power();
    }
    else if (Mode3 == 1)
    {
      Jok();
    }
    else if (Mode3 == 2)
    {
      Temp();
    }
    else if (Mode3 == 3)
    {
      Spin();
    }
    else if (Mode3 == 4)
    {
      Start();
    }
    else if (Mode3 == 5)
    {
      Dry(1);
      delay(700);
      Dry(0);
    }
  }
  else if (BT == 1)
  {
    BT = 0;
    stateCheckLdr1 = false;
    stateCheckLdr2 = false;
    indexSet = 0;
  }
}
void Drysetting()
{
  if (Mode2 == 0)
  {
    display.setSegments(P1_TEXT, 2, 0);
    display.showNumberDec(price[0], false, 2, 2);
  }
  else if (Mode2 == 1)
  {
    display.setSegments(P2_TEXT, 2, 0);
    display.showNumberDec(price[1], false, 2, 2);
  }
  else if (Mode2 == 2)
  {
    display.setSegments(P3_TEXT, 2, 0);
    display.showNumberDec(price[2], false, 2, 2);
  }
  else if (Mode2 == 3)
  {
    display.setSegments(D1_TEXT, 2, 0);
    display.showNumberDec(timerDry[0], false, 2, 2);
  }
  else if (Mode2 == 4)
  {
    display.setSegments(D2_TEXT, 2, 0);
    display.showNumberDec(timerDry[1], false, 2, 2);
  }
  else if (Mode2 == 5)
  {
    display.setSegments(D3_TEXT, 2, 0);
    display.showNumberDec(timerDry[2], false, 2, 2);
  }

  // Button();
  if (BT == 4)
  {
    BT = 0;
    Mode2++;
    if (Mode2 >= 6)
    {
      Mode2 = 0;
    }
  }
  else if (BT == 3)
  {
    BT = 0;
    if (Mode2 == 0)
    {
      price[0] = price[0] + 10;
    }
    else if (Mode2 == 1)
    {
      price[1] = price[1] + 10;
    }
    else if (Mode2 == 2)
    {
      price[2] = price[2] + 10;
    }
    else if (Mode2 == 3)
    {
      timerDry[0] = timerDry[0] + 5;
    }
    else if (Mode2 == 4)
    {
      timerDry[1] = timerDry[1] + 5;
    }
    else if (Mode2 == 5)
    {
      timerDry[2] = timerDry[2] + 5;
    }
  }
  else if (BT == 2)
  {
    BT = 0;
    if (Mode2 == 0)
    {
      price[0] = price[0] - 10;
    }
    else if (Mode2 == 1)
    {
      price[1] = price[1] - 10;
    }
    else if (Mode2 == 2)
    {
      price[2] = price[2] - 10;
    }
    else if (Mode2 == 3)
    {
      timerDry[0] = timerDry[0] - 5;
    }
    else if (Mode2 == 4)
    {
      timerDry[1] = timerDry[1] - 5;
    }
    else if (Mode2 == 5)
    {
      timerDry[2] = timerDry[2] - 5;
    }
  }
  else if (BT == 1)
  {
    BT = 0;
    // PutEprom(); // Save
    writePreferences();
    indexSet = 0;
  }
}
void CommandProgram()
{
  if (Mode2 == 0)
  {
    display.setSegments(SEG_Pr01);
  }
  else if (Mode2 == 1)
  {
    display.setSegments(SEG_Pr02);
  }
  else if (Mode2 == 2)
  {
    display.setSegments(SEG_Pr03);
  }
  else if (Mode2 == 3)
  {
    display.setSegments(SEG_Pr04);
  }
  else if (Mode2 == 4)
  {
    display.setSegments(SEG_Pr05);
  }
  else if (Mode2 == 5)
  {
    display.setSegments(SEG_Pr06);
  }
  // Button();
  if (BT == 4)
  {
    BT = 0;

    if (Mode2 == 0)
    {
      program = 1;
    }
    else if (Mode2 == 1)
    {
      program = 2;
    }
    else if (Mode2 == 2)
    {
      program = 3;
    }
    else if (Mode2 == 3)
    {
      program = 4;
    }
    else if (Mode2 == 4)
    {
      program = 5;
    }
    else if (Mode2 == 5)
    {
      program = 6;
    }

    chanel = 0;
    step = 0;
    statedisplaystandby = 3;
    display.setSegments(SEG_Mode);
    Serial.println("command run program : " + String(program));
    setStartMachine();
  }
  else if (BT == 3)
  {
    BT = 0;
    Mode2++;
    if (Mode2 >= 6)
    {
      Mode2 = 0;
    }
  }
  else if (BT == 2)
  {
    BT = 0;
    Mode2--;
    if (Mode2 <= -1)
    {
      Mode2 = 5;
    }
  }
  else if (BT == 1)
  {
    BT = 0;
    indexSet = 0;
  }
}

// Update Firmware OTI*********************************************
/** fw.ma-well.com / mawell.thddns.net:4746 ใช้ไม่ได้ — OTA อยู่ที่ backend.ma-well.com:80 */
void normalizeOtaServer()
{
  server.trim();
  if (server.length() > 0 && (uint8_t)server.charAt(0) > 127)
  {
    Serial.println(F("[OTA] strip invalid host prefix"));
    int idx = 0;
    while (idx < (int)server.length() && (uint8_t)server.charAt(idx) > 127)
      idx++;
    server = server.substring(idx);
    server.trim();
  }
  const bool legacyHost =
      server.length() == 0 ||
      server == "fw.ma-well.com" ||
      server.indexOf("fw.ma-well") >= 0 ||
      server == "mawell.thddns.net" ||
      server.indexOf("thddns.net") >= 0;
  if (legacyHost)
  {
    Serial.println(F("[OTA] normalize -> backend.ma-well.com:80"));
    server = "backend.ma-well.com";
    port = 80;
  }
  if (server == "backend.ma-well.com" && port != 80 && port != 443)
    port = 80;
  host = server;
}

void suspendMachineTasksForOta()
{
  if (taskDisplay_handle != NULL)
  {
    vTaskDelete(taskDisplay_handle);
    taskDisplay_handle = NULL;
  }
  if (taskProgram_handle != NULL)
  {
    vTaskDelete(taskProgram_handle);
    taskProgram_handle = NULL;
  }
}

void restoreMachineTasksAfterOta()
{
  if (taskDisplay_handle == NULL)
  {
    xTaskCreate(taskDisplay, "taskDisplay", 1024 * 3, NULL, 1, &taskDisplay_handle);
  }
  if (taskProgram_handle == NULL)
  {
    xTaskCreate(taskProgram, "taskProgram", 1024 * 5, NULL, 1, &taskProgram_handle);
  }
  statedisplaystandby = 0;
  chanel = 0;
  step = 0;
  setPriceShow();
}

/** หยุด MQTT ชั่วคราวก่อน OTA — client ถูกใช้ร่วมกับ PubSubClient */
static void pauseMqttForOta()
{
  if (netLockEnter())
  {
    if (mqclient.connected())
    {
      Serial.println(F("[OTA] pause MQTT"));
      mqclient.disconnect();  // client.stop() อยู่ในตัวแล้ว
    }
    else
    {
      client.stop();  // ปิดครั้งเดียว — อย่า double-close (lwIP pbuf assert)
    }
    netLockLeave();
  }
  delay(50);
  yield();
}

String postDataToServer(String server, String path, String postData)
{
  (void)server;
  if (!netLockEnter())
    return String("Can not connect to Server");
  HTTPClient http;
  const String url = melodyHttpUrl(path);
  Serial.println("[OTA] HTTP POST " + url);
  http.begin(url);
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Connection", "close");
  const int code = http.POST(postData);
  Serial.println(String("[OTA] HTTP code=") + code);
  if (code <= 0)
  {
    Serial.println(String("[OTA] HTTP error: ") + http.errorToString(code));
    http.end();
    netLockLeave();
    return (code == HTTPC_ERROR_READ_TIMEOUT) ? String(">>> Client Timeout !")
                                            : String("Can not connect to Server");
  }
  if (code != HTTP_CODE_OK)
  {
    const String errBody = http.getString();
    http.end();
    Serial.println("[OTA] HTTP body: " + errBody);
    netLockLeave();
    return String("Can not connect to Server");
  }
  String payload = http.getString();
  http.end();
  payload.trim();
  Serial.println("[OTA] version response: " + payload);
  netLockLeave();
  return payload;
}
/** อ่าน chip_id จาก ESP image header (byte 12) — ESP32=0, ESP32-S3=9 */
static int otaImageChipIdFromHeader(const uint8_t *hdr, size_t len)
{
  if (len < 13 || hdr[0] != 0xE9)
    return -1;
  return (int)hdr[12];
}

static int otaDeviceChipId()
{
#if CONFIG_IDF_TARGET_ESP32S3
  return 9;
#else
  return 0;
#endif
}

static const char *otaChipName(int chipId)
{
  if (chipId == 0)
    return "ESP32";
  if (chipId == 9)
    return "ESP32-S3";
  return "?";
}

/** อ่าน HTTP stream → flash พร้อม retry (กัน writeStream หยุดที่ 16 bytes เมื่อ stream ยังไม่พร้อม) */
static size_t otaWriteStreamWithRetry(Client &stream, size_t contentLength, size_t initialWritten)
{
  size_t written = initialWritten;
  uint8_t buf[1024];
  const unsigned long deadline = millis() + 300000UL;
  unsigned long lastProgressMs = millis();
  unsigned idleRounds = 0;

  while (written < contentLength && !Update.hasError() && (long)(millis() - deadline) < 0)
  {
    const size_t remaining = contentLength - written;
    int avail = stream.available();
    if (avail <= 0)
    {
      if (!stream.connected())
      {
        Serial.print(F("[OTA] stream closed at "));
        Serial.println(written);
        break;
      }
      if (++idleRounds > 500)
      {
        Serial.print(F("[OTA] stream idle timeout at "));
        Serial.println(written);
        break;
      }
      delay(10);
      yield();
      continue;
    }
    idleRounds = 0;

    size_t chunk = (size_t)avail;
    if (chunk > sizeof(buf))
      chunk = sizeof(buf);
    if (chunk > remaining)
      chunk = remaining;

    const size_t n = stream.readBytes(buf, chunk);
    if (n == 0)
    {
      delay(10);
      yield();
      continue;
    }

    const size_t w = Update.write(buf, n);
    if (w != n)
    {
      Serial.print(F("[OTA] flash write "));
      Serial.print(w);
      Serial.print(F("/"));
      Serial.println(n);
      break;
    }
    written += w;

    const unsigned long now = millis();
    if ((unsigned long)(now - lastProgressMs) >= 15000UL)
    {
      lastProgressMs = now;
      Serial.print(F("[OTA] "));
      Serial.print(written);
      Serial.print(F("/"));
      Serial.println(contentLength);
    }
    yield();
  }
  return written;
}

String getVersionByPOST(String filename)
{
  String path = Path_GetVersion;
  String uid = otaFolderOverride.length() > 0 ? otaFolderOverride : userID;
  String fn = otaFilenameOverride.length() > 0 ? otaFilenameOverride : filename;
  String verData = "user_id=" + uid + "&api_key=" + api_key + "&filename=" + fn;
  return postDataToServer(server, path, verData);
}
bool fwUpdate_OTI_POST(String filename)
{
  contentLength = 0;
  isValidContentType = false;
  String uid = otaFolderOverride.length() > 0 ? otaFolderOverride : userID;
  String fn = otaFilenameOverride.length() > 0 ? otaFilenameOverride : filename;
  String verData = "user_id=" + uid + "&api_key=" + api_key + "&filename=" + fn;

  HTTPClient http;
  const String url = melodyHttpUrl(Path_OTI);
  Serial.println("[OTA] download POST " + url);
  http.begin(url);
  http.setTimeout(300000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Connection", "close");
  const int code = http.POST(verData);
  Serial.println(String("[OTA] download HTTP=") + code);

  if (code != HTTP_CODE_OK)
  {
    const String errBody = http.getString();
    Serial.println("[OTA] download err: " + errBody);
    http.end();
    display.setSegments(eror);
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (Firmware not available)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "Firmware not available");
    delay(1000);
    return false;
  }

  contentLength = http.getSize();
  isValidContentType = true;
  Serial.println("Got " + String(contentLength) + " bytes from server");

  if (contentLength <= 0)
  {
    http.end();
    display.setSegments(eror);
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "No content");
    delay(1000);
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (!stream)
  {
    http.end();
    return false;
  }

  uint8_t imgHdr[16] = {};
  const size_t hdrRead = stream->readBytes(imgHdr, sizeof(imgHdr));
  const int imageChip = otaImageChipIdFromHeader(imgHdr, hdrRead);
  const int deviceChip = otaDeviceChipId();
  if (imageChip >= 0 && imageChip != deviceChip)
  {
    http.end();
    Serial.print(F("[OTA] chip mismatch: image="));
    Serial.print(otaChipName(imageChip));
    Serial.print(F(" device="));
    Serial.println(otaChipName(deviceChip));
    display.setSegments(eror);
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (chip ไม่ตรง)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "chip mismatch");
    delay(1000);
    return false;
  }

  if (!Update.begin(contentLength))
  {
    http.end();
    Serial.println("Not enough space to begin OTA");
    display.setSegments(eror);
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (พื้นที่ไม่พอ)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "พื้นที่ไม่พอ");
    delay(1000);
    return false;
  }

  Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
  size_t written = 0;
  if (hdrRead > 0)
    written = Update.write(imgHdr, hdrRead);
  written = otaWriteStreamWithRetry(*stream, (size_t)contentLength, written);
  http.end();

  if (written != (size_t)contentLength)
  {
    Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
    Serial.println("[OTA] Update error #: " + String(Update.getError()));
    display.setSegments(eror);
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (เขียนไม่ครบ)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "เขียนไม่ครบ");
    delay(1000);
    return false;
  }

  Serial.println("Written : " + String(written) + " successfully");

  if (Update.end() && Update.isFinished())
  {
    Serial.println("Update successfully completed. Rebooting.");
    display.setSegments(SEG_DONE);
    data = Noserial + " gid : " + gid + " ==> Update successful. !";
    sentDatatoAdmin();
    sendOtaStatusMqtt("success", 100, "");
    delay(1000);
    return true;
  }

  Serial.println("Error Occurred. Error #: " + String(Update.getError()));
  display.setSegments(eror);
  data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้";
  sentDatatoAdmin();
  sendOtaStatusMqtt("failed", 0, "Update error");
  delay(1000);
  return false;
}
String getField(String str, int fcount)
{
  int i = 0;
  char ch;
  int len = str.length();
  String str2 = "";
  while (fcount >= 1 and i < len)
  {
    ch = str[i];
    // str2+=ch;
    if (ch != ',' & ch != ' ')
    {
      str2 += ch;
    }
    else
    {
      fcount--;
      if (fcount > 0)
      {
        str2 = "";
      }
    }
    i++;
  }
  return (str2);
}
void otiUdate()
{
  if (UpdateFw)
  {
    otaInProgress = true;  // งดเช็ค task-hang watchdog ระหว่าง OTA
    suspendMachineTasksForOta();  // หยุด display/program ก่อนเขียน flash (OTA จาก Melody ไม่ผ่านเมนู update)
    if (!WiFi.isConnected())
    {
      Serial.println("OTI update requested but WiFi is not connected. Abort.");
      UpdateFw = false;
      display.setSegments(eror);
      data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (WiFi ไม่พร้อม)";
      sentDatatoAdmin();
      sendOtaStatusMqtt("failed", 0, "WiFi ไม่พร้อม");
      delay(1000);
      restoreMachineTasksAfterOta();
      otaInProgress = false;
      return;
    }

    UpdateFw = false;
    normalizeOtaServer();
    pauseMqttForOta();
    Serial.println("\nCheck New Firmware");
    Serial.println("OTA server: " + server + ":" + String(port));
    Serial.println("OTA folder: " + String(otaFolderOverride.length() > 0 ? otaFolderOverride : userID));
    if (otaFolderOverride.length() > 0)
      Serial.println("OTA folder override: " + otaFolderOverride);
    String str = getVersionByPOST("firmware.bin");
    Serial.println(str);

    // ถ้าเช็คเวอร์ชันไม่ผ่าน (timeout / เชื่อมต่อไม่ได้) -> แสดงอัพเดทไม่ได้ แล้วทำงานต่อ
    if (str.indexOf("Timeout") >= 0 || str.indexOf("Can not connect") >= 0 || str.length() < 2)
    {
      display.setSegments(eror);
      data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (เช็คเวอร์ชันไม่สำเร็จ)";
      sentDatatoAdmin();
      sendOtaStatusMqtt("failed", 0, "เช็คเวอร์ชันไม่สำเร็จ");
      Serial.println("OTA version check failed. Continue normal operation.");
      delay(1000);
      restoreMachineTasksAfterOta();
      otaFolderOverride = "";
      otaFilenameOverride = "";
      otaInProgress = false;
      return;
    }

    str = getField(str, 2);
    float curVer = str.toFloat();
    str = getField(fwversion[1], 2);
    float fwVersion = str.toFloat();

    if (curVer != fwVersion)
    {
      display.setSegments(SEG_Up);
      data = Noserial + " gid : " + gid + " ==> Updating firmware .. !";
      sentDatatoAdmin();
      sendOtaStatusMqtt("start", 0, "");
      pauseMqttForOta();  // ตัด MQTT ก่อนดาวน์โหลด — sendOtaStatus reconnect แล้ว อย่าให้แย่ง WiFiClient/lwIP
      Serial.println("Update Firmware");
      bool ok = fwUpdate_OTI_POST("firmware.bin");
      if (ok)
      {
        Serial.println("Restarting after OTA success...");
        publishPresenceOfflineGraceful();
        ESP.restart();
      }
      // ถ้าอัพเดทไม่สำเร็จ (timeout/error) fwUpdate_OTI_POST แสดง eror และส่งแอดมินแล้ว -> ทำงานตามปกติ ไม่รีสตาร์ต
      restoreMachineTasksAfterOta();
    }
    else
    {
      display.setSegments(eror);
      data = Noserial + " gid : " + gid + " ==> New firmware Same Current Version !";
      sentDatatoAdmin();
      Serial.println("New firmware Same Current Version ");
      sendOtaStatusMqtt("failed", 0, "เวอร์ชันตรงกัน");
      delay(1000);
      restoreMachineTasksAfterOta();
    }
    otaFolderOverride = "";
    otaFilenameOverride = "";
    otaInProgress = false;
  }
}
//*** end of OTI ****

void setRelayType()
{
  // var program
  if (CodeMachine == 0)
  { // LG10KgBlack
    program1[0] = 3;
    program1[1] = 2;
    program1[2] = 1;

    program2[0] = 3;
    program2[1] = 0;
    program2[2] = 1;

    program3[0] = 1;
    program3[1] = 2;
    program3[2] = 1;

    rinStep2[0] = 7;
    rinStep2[1] = 1;

    rincommand[0] = 8;
    rincommand[1] = 1;

    spin = 6; // step3

    drum[0] = 7;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 19;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 1;
    TimeCountdowndrum[1] = 31;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }
  else if (CodeMachine == 1)
  { // LG11KgWhite
    program1[0] = 3;
    program1[1] = 2;
    program1[2] = 1;

    program2[0] = 3;
    program2[1] = 0;
    program2[2] = 1;

    program3[0] = 1;
    program3[1] = 2;
    program3[2] = 1;

    rinStep2[0] = 7;
    rinStep2[1] = 1;

    rincommand[0] = 7;
    rincommand[1] = 1;

    spin = 6; // step3

    drum[0] = 6;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 19; // 19
    check_runing_time[1] = 13; // 13
    check_runing_time[2] = 5;  // 5

    TimeCountdowndrum[0] = 1;
    TimeCountdowndrum[1] = 31;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }
  else if (CodeMachine == 2)
  { // LG15KgBlack
    program1[0] = 5;
    program1[1] = 1;
    program1[2] = 0;

    program2[0] = 5;
    program2[1] = 0;
    program2[2] = 0;

    program3[0] = 4;
    program3[1] = 0;
    program3[2] = 1;

    rinStep2[0] = 5;
    rinStep2[1] = 1;

    rincommand[0] = 6;
    rincommand[1] = 1;

    spin = 1; // step3

    drum[0] = 6;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 20;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 1;
    TimeCountdowndrum[1] = 31;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }
  else if (CodeMachine == 3)
  { // LG24KgBlack
    program1[0] = 3;
    program1[1] = 1;
    program1[2] = 0;

    program2[0] = 3;
    program2[1] = 0;
    program2[2] = 0;

    program3[0] = 4;
    program3[1] = 2;
    program3[2] = 1;

    rinStep2[0] = 5;
    rinStep2[1] = 1;

    rincommand[0] = 6;
    rincommand[1] = 1;

    spin = 1; // step3

    drum[0] = 7;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 21;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 2;
    TimeCountdowndrum[1] = 07;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }
  else if (CodeMachine == 4)
  { // LG15KgNew
    program1[0] = 2;
    program1[1] = 1;
    program1[2] = 0;

    program2[0] = 2;
    program2[1] = 3;
    program2[2] = 0;

    program3[0] = 0;
    program3[1] = 4;
    program3[2] = 0;

    rinStep2[0] = 1;
    rinStep2[1] = 1;

    rincommand[0] = 1;
    rincommand[1] = 1;

    spin = 1; // step3

    drum[0] = 1;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 21;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 2;
    TimeCountdowndrum[1] = 07;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }
  else if (CodeMachine == 7)
  { // Hier 12
    program1[0] = 0;
    program1[1] = 5;
    program1[2] = 0;

    program2[0] = 0;
    program2[1] = 2;
    program2[2] = 0;

    program3[0] = 0;
    program3[1] = 4;
    program3[2] = 0;

    rinStep2[0] = 3;
    rinStep2[1] = 1;

    rincommand[0] = 3;
    rincommand[1] = 1;

    spin = 3;     // step3
    spinHier = 3; // step3

    drum[0] = 1;
    drum[1] = 0;
    drum[2] = 1;

    check_runing_time[0] = 19;
    check_runing_time[1] = 12;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 1;
    TimeCountdowndrum[1] = 05;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }
  else if (CodeMachine == 8)
  { // Hier 15
    program1[0] = 0;
    program1[1] = 4;
    program1[2] = 0;

    program2[0] = 0;
    program2[1] = 1;
    program2[2] = 0;

    program3[0] = 0;
    program3[1] = 3;
    program3[2] = 0;

    rinStep2[0] = 3;
    rinStep2[1] = 1;

    rincommand[0] = 3;
    rincommand[1] = 1;

    spin = 1;     // step3
    spinHier = 4; // step3

    drum[0] = 1;
    drum[1] = 0;
    drum[2] = 1;

    check_runing_time[0] = 19;
    check_runing_time[1] = 12;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 1;
    TimeCountdowndrum[1] = 05;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }
}

void sentDatatoAdmin()
{
  StaticJsonDocument<200> jsonDoc;
  // data = Noserial +" gid : "+gid+" ==> New firmware Same CurenVersion !";
  jsonDoc["status"] = data;
  String jsonString;
  serializeJson(jsonDoc, jsonString);
  Serial.println(jsonString);
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!netLockEnter())
      return;
    HTTPClient http;
    http.begin("http://mawell.thddns.net:4740/completed");
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonString);
    Serial.print(httpResponseCode);
    http.end();
    netLockLeave();
  }
}

// ส่งสถานะ OTA ไป topic OtaStatus เพื่อให้ server/แอดมินแสดงกำลังอัพเดท X% หรือเสร็จ/ล้มเหลว
// ถ้า MQTT หลุด (เช่น หลัง pauseMqttForOta) จะลอง reconnect หลายรอบก่อนส่ง
static bool ensureMqttForOtaStatus()
{
  if (mqclient.connected())
    return true;
  for (int attempt = 0; attempt < 5; attempt++)
  {
    mqttreconnect();
    for (int i = 0; i < 40; i++)
    {
      if (netLockEnter())
      {
        mqttPumpLoopLocked(1);
        netLockLeave();
      }
      delay(50);
      if (mqclient.connected())
        return true;
    }
    delay(200);
  }
  return false;
}

void sendOtaStatusMqtt(const char* phase, int percent, const char* message)
{
  if (!ensureMqttForOtaStatus()) {
    Serial.println("[MQTT] OtaStatus ข้ามส่ง (reconnect ไม่ได้) " + String(phase));
    return;
  }
  StaticJsonDocument<256> doc;
  doc["id"] = Noserial;
  doc["gid"] = gid;
  doc["phase"] = phase;
  doc["percent"] = percent;
  if (message && message[0] != '\0') doc["message"] = message;
  String out;
  serializeJson(doc, out);
  if (netLockEnter())
  {
    mqclient.publish("OtaStatus", out.c_str());
    Serial.println("[MQTT] OtaStatus " + String(phase) + " " + String(percent) + "%");
    for (int i = 0; i < 15; i++) {
      mqttPumpLoopLocked(1);
      delay(50);
    }
    netLockLeave();
  }
}

void GetData()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("HTTP GetStatus NotConnect Server");
    return;
  }

  // ดึงจากระบบปัจจุบันแบบ MQTT เท่านั้น (ส่ง configRequest รอ configResponse)
  if (mqclient.connected())
  {
    String reqPayload = "{\"controllerId\":\"" + Noserial + "\"}";
    Serial.println(F("[MQTT] >>> ส่ง configRequest"));
    Serial.println(F("       topic  = configRequest"));
    Serial.println("       payload = " + reqPayload);
    if (!netLockEnter())
      return;
    mqclient.publish("configRequest", reqPayload.c_str());
    mqttPumpLoopLocked(2);
    Serial.println(F("       (รอ configResponse จากระบบ สูงสุด 8 วินาที)"));
    const unsigned long waitMs = 8000;
    const unsigned long start = millis();
    while (millis() - start < waitMs)
    {
      mqttPumpLoopLocked(1);
      netLockLeave();
      vTaskDelay(pdMS_TO_TICKS(50));
      if (!netLockEnter())
        return;
      if (isMelodyBootSetupPhase())
      {
        if (bootMelodySyncQuietReady() && bootMelodyConfigPending)
        {
          netLockLeave();
          Serial.println(F("[MQTT] ✅ ได้ configResponse ครบ debounce — ชุดสุดท้ายจาก Melody"));
          return;
        }
      }
      else if (stateSetupdata)
      {
        netLockLeave();
        Serial.println(F("[MQTT] ✅ ได้ configResponse ภายในเวลา -> GetSetupData() จะบันทึก"));
        return;
      }
      yield();
    }
    netLockLeave();
    Serial.println(F("[MQTT] ไม่ได้รับ configResponse ภายใน 8 วินาที (timeout)"));
  }
  else
  {
    Serial.println(F("[MQTT] ข้าม configRequest เพราะ MQTT ยังไม่เชื่อมต่อ"));
  }

  // ไม่มีข้อมูลจากระบบ (ไม่ต่อ MQTT หรือ timeout) -> ใช้ค่าจากโรงงาน
  Serial.println(F("⚠️ ใช้ค่าจากโรงงาน (ไม่มี config จากระบบ)"));
  applyFactoryDefaultsConfig();
}
void GetSetupData()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  // รับข้อมูลจาก MQTT (setup) แล้วบันทึก — ไม่ต้องเรียก HTTP
  if (mqttPayloadBuffer.length() > 0)
  {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, mqttPayloadBuffer);
    mqttPayloadBuffer = "";
    if (!error && doc["id"].as<String>() == Noserial && doc["cm"].as<String>() == "setup")
    {
      JsonObject v2 = doc["value_str2"];
      if (!v2.isNull())
      {
        if (v2.containsKey("id"))
        {
          String newId = v2["id"].as<String>();
          if (Noserial != newId)
          {
            Serial.println("Edit Serial number for ESP32 => " + newId);
            Noserial = newId;
            writePreferencesfirst();
          }
        }
        if (v2.containsKey("gid"))
          gid = v2["gid"].as<int>();
        if (v2.containsKey("ssid"))
        {
          String s = v2["ssid"].as<String>();
          if (s.length() > 0) ssidStr = s;
        }
        else if (v2.containsKey("wifiname"))
        {
          String s = v2["wifiname"].as<String>();
          if (s.length() > 0) ssidStr = s;
        }
        if (v2.containsKey("password"))
        {
          String s = v2["password"].as<String>();
          if (s.length() > 0) passStr = s;
        }
        else if (v2.containsKey("wifipass"))
        {
          String s = v2["wifipass"].as<String>();
          if (s.length() > 0) passStr = s;
        }
        if (v2.containsKey("ModeSystem"))
          Mode = v2["ModeSystem"].as<int>();
        if (v2.containsKey("mqttStatus")) {
          mqttStatus = v2["mqttStatus"].as<int>();
          if (mqttStatus < 1 || mqttStatus > 2) mqttStatus = 1;
        }
        if (v2.containsKey("CodeMachine"))
          CodeMachine = v2["CodeMachine"].as<int>();
        if (v2.containsKey("Price1")) {
          price[0] = v2["Price1"].as<int>();
          price[1] = v2["Price2"].as<int>();
          price[2] = v2["Price3"].as<int>();
        }
        if (v2.containsKey("PricePro1")) {
          pricePro[0] = v2["PricePro1"].as<int>();
          pricePro[1] = v2["PricePro2"].as<int>();
          pricePro[2] = v2["PricePro3"].as<int>();
        }
        if (v2.containsKey("timedry1")) {
          timerDry[0] = v2["timedry1"].as<int>();
          timerDry[1] = v2["timedry2"].as<int>();
          timerDry[2] = v2["timedry3"].as<int>();
        }
        if (v2.containsKey("program1[0]"))
        {
          program1[0] = v2["program1[0]"];
          program1[1] = v2["program1[1]"];
          program1[2] = v2["program1[2]"];
        }
        if (v2.containsKey("program2[0]"))
        {
          program2[0] = v2["program2[0]"];
          program2[1] = v2["program2[1]"];
          program2[2] = v2["program2[2]"];
        }
        if (v2.containsKey("program3[0]"))
        {
          program3[0] = v2["program3[0]"];
          program3[1] = v2["program3[1]"];
          program3[2] = v2["program3[2]"];
        }
        if (v2.containsKey("rinStep2[0]"))
        {
          rinStep2[0] = v2["rinStep2[0]"];
          rinStep2[1] = v2["rinStep2[1]"];
        }
        if (v2.containsKey("rincommand[0]"))
        {
          rincommand[0] = v2["rincommand[0]"];
          rincommand[1] = v2["rincommand[1]"];
        }
        if (v2.containsKey("spin"))
          spin = v2["spin"];
        if (v2.containsKey("drum[0]"))
        {
          drum[0] = v2["drum[0]"];
          drum[1] = v2["drum[1]"];
          drum[2] = v2["drum[2]"];
        }
        if (v2.containsKey("check_runing_time[0]"))
        {
          check_runing_time[0] = v2["check_runing_time[0]"];
          check_runing_time[1] = v2["check_runing_time[1]"];
          check_runing_time[2] = v2["check_runing_time[2]"];
        }
        if (v2.containsKey("TimeCountdowndrum[0]"))
        {
          TimeCountdowndrum[0] = v2["TimeCountdowndrum[0]"];
          TimeCountdowndrum[1] = v2["TimeCountdowndrum[1]"];
        }
        if (v2.containsKey("TimeCountdown1[0]"))
        {
          TimeCountdown1[0] = v2["TimeCountdown1[0]"];
          TimeCountdown1[1] = v2["TimeCountdown1[1]"];
        }
        if (v2.containsKey("TimeCountdown2[0]"))
        {
          TimeCountdown2[0] = v2["TimeCountdown2[0]"];
          TimeCountdown2[1] = v2["TimeCountdown2[1]"];
        }
        if (v2.containsKey("TimeCountdown3[0]"))
        {
          TimeCountdown3[0] = v2["TimeCountdown3[0]"];
          TimeCountdown3[1] = v2["TimeCountdown3[1]"];
        }
        if (v2.containsKey("coinValue")) { int cv = v2["coinValue"].as<int>(); if (cv >= 1) coinValue = cv; }
        if (v2.containsKey("ldr_set")) { int v = v2["ldr_set"].as<int>(); if (v >= 0) ldr_set = v * 100; }
        if (v2.containsKey("pinSlot")) { int v = v2["pinSlot"].as<int>(); if (v == SIG_PIN || v == SIG_PIN2) pinSlot = v; }
        if (v2.containsKey("StateShutdown"))
          StateShutdown = v2["StateShutdown"].as<int>();
        if (v2.containsKey("statusReportIntervalMinutes")) {
          int v = v2["statusReportIntervalMinutes"].as<int>();
          if (v >= 1 && v <= 60) statusReportIntervalMinutes = v;
        }
        if (v2.containsKey("otaServer")) {
          server = v2["otaServer"].as<String>();
          normalizeOtaServer();
          host = server;
        }
        if (v2.containsKey("otaPort")) {
          int p = v2["otaPort"].as<int>();
          if (p > 0 && p <= 65535) port = p;
          normalizeOtaServer();
        }
        Serial.println("Setup from MQTT applied, writing preferences.");
        setRelayType();
        vTaskDelay(pdMS_TO_TICKS(20));
        writePreferencesfirst();
        vTaskDelay(pdMS_TO_TICKS(20));
        writePreferences();
        vTaskDelay(pdMS_TO_TICKS(20));
        setPriceShow();
      }
    }
    return;
  }

  // ไม่มี MQTT payload -> ใช้ค่าจากโรงงาน (ไม่ดึง HTTP แบบเก่า)
  Serial.println("⚠️ ไม่มี config จาก MQTT -> ใช้ค่าจากโรงงาน");
  applyFactoryDefaultsConfig();
}

static void applyPromoSlotsPayload(const String &payloadJson)
{
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payloadJson);
  if (err)
  {
    Serial.print(F("deserializeJson(setPromoSlots) failed: "));
    Serial.println(err.f_str());
    return;
  }

  promoSlotCount = 0;
  JsonArray slots = doc["slots"].as<JsonArray>();
  if (!slots.isNull())
  {
    for (JsonObject slot : slots)
    {
      if (promoSlotCount >= MAX_PROMO_SLOTS)
        break;

      PromoSlot &ps = promoSlots[promoSlotCount++];
      ps.day = slot["day"] | 0;

      String startStr = slot["start"] | "00:00";
      String endStr = slot["end"] | "00:00";
      ps.startHour = startStr.substring(0, 2).toInt();
      ps.startMin = startStr.substring(3, 5).toInt();
      ps.endHour = endStr.substring(0, 2).toInt();
      ps.endMin = endStr.substring(3, 5).toInt();

      JsonArray pricesSlot = slot["prices"].as<JsonArray>();
      for (int i = 0; i < 3; i++)
      {
        if (!pricesSlot.isNull() && i < (int)pricesSlot.size())
          ps.pricePro[i] = pricesSlot[i].as<int>();
        else
          ps.pricePro[i] = pricePro[i];
      }
    }
  }

  if (doc.containsKey("Price1Pro")) pricePro[0] = doc["Price1Pro"].as<int>();
  if (doc.containsKey("Price2Pro")) pricePro[1] = doc["Price2Pro"].as<int>();
  if (doc.containsKey("Price3Pro")) pricePro[2] = doc["Price3Pro"].as<int>();
  if (doc.containsKey("Price1")) price[0] = doc["Price1"].as<int>();
  if (doc.containsKey("Price2")) price[1] = doc["Price2"].as<int>();
  if (doc.containsKey("Price3")) price[2] = doc["Price3"].as<int>();

  writePreferences();
  setPriceShow();
  statePriceShow = true;
  Serial.println("Updated promo slots via MQTT, count: " + String(promoSlotCount));
}

// ส่ง config ปัจจุบันไป topic getdataResponse เพื่อให้ server (MelodyWebapp) รับไปบันทึก
void PublishConfigViaMqtt()
{
  const size_t capacity = 2048;
  DynamicJsonDocument doc(capacity);
  doc["cm"] = "getdataResponse";
  doc["id"] = Noserial;

  JsonObject v2 = doc.createNestedObject("value_str2");
  v2["id"] = Noserial;
  v2["gid"] = gid;
  v2["ModeSystem"] = Mode;
  v2["mqttStatus"] = mqttStatus;
  v2["CodeMachine"] = CodeMachine;
  v2["Price1"] = price[0];
  v2["Price2"] = price[1];
  v2["Price3"] = price[2];
  v2["PricePro1"] = pricePro[0];
  v2["PricePro2"] = pricePro[1];
  v2["PricePro3"] = pricePro[2];
  v2["coinValue"] = coinValue;
  v2["timedry1"] = timerDry[0];
  v2["timedry2"] = timerDry[1];
  v2["timedry3"] = timerDry[2];
  v2["program1[0]"] = program1[0];
  v2["program1[1]"] = program1[1];
  v2["program1[2]"] = program1[2];
  v2["program2[0]"] = program2[0];
  v2["program2[1]"] = program2[1];
  v2["program2[2]"] = program2[2];
  v2["program3[0]"] = program3[0];
  v2["program3[1]"] = program3[1];
  v2["program3[2]"] = program3[2];
  v2["rinStep2[0]"] = rinStep2[0];
  v2["rinStep2[1]"] = rinStep2[1];
  v2["rincommand[0]"] = rincommand[0];
  v2["rincommand[1]"] = rincommand[1];
  v2["spin"] = spin;
  v2["drum[0]"] = drum[0];
  v2["drum[1]"] = drum[1];
  v2["drum[2]"] = drum[2];
  v2["check_runing_time[0]"] = check_runing_time[0];
  v2["check_runing_time[1]"] = check_runing_time[1];
  v2["check_runing_time[2]"] = check_runing_time[2];
  v2["TimeCountdowndrum[0]"] = TimeCountdowndrum[0];
  v2["TimeCountdowndrum[1]"] = TimeCountdowndrum[1];
  v2["TimeCountdown1[0]"] = TimeCountdown1[0];
  v2["TimeCountdown1[1]"] = TimeCountdown1[1];
  v2["TimeCountdown2[0]"] = TimeCountdown2[0];
  v2["TimeCountdown2[1]"] = TimeCountdown2[1];
  v2["TimeCountdown3[0]"] = TimeCountdown3[0];
  v2["TimeCountdown3[1]"] = TimeCountdown3[1];
  v2["ldr_set"] = ldr_set / 100;
  v2["StateShutdown"] = StateShutdown;
  v2["SetupData"] = SetupData;
  v2["pinSlot"] = pinSlot;
  v2["statusReportIntervalMinutes"] = statusReportIntervalMinutes;
  v2["otaServer"] = server;
  v2["otaPort"] = port;
  v2["ssid"] = ssidStr;
  v2["password"] = passStr;

  String output;
  serializeJson(doc, output);
  if (!netLockEnter())
    return;
  bool ok = mqclient.publish("getdataResponse", output.c_str());
  mqttPumpLoopLocked(3);
  netLockLeave();
  if (ok) {
    Serial.println("MQTT getdataResponse published OK");
  } else {
    Serial.print("MQTT getdataResponse publish failed (payload len=");
    Serial.print(output.length());
    Serial.println(" bytes, ต้อง setBufferSize มากกว่านี้ถ้าเกิน 2048)");
  }
}

void sentVarjson()
{
  preferences.begin("config", true); // เปิด Preferences ในโหมดอ่านอย่างเดียว
  StaticJsonDocument<1024> jsonDoc;
  const char *keys[] = {"Noserial", "ssid", "password", "IDserver"};

  for (const char *key : keys)
  {
    jsonDoc[key] = preferences.getString(key, "");
  }

  jsonDoc["gid"] = gid;
  jsonDoc["Mode"] = Mode;
  jsonDoc["CodeMachine"] = CodeMachine;

  JsonArray priceArr = jsonDoc.createNestedArray("price");
  for (int i = 0; i < 3; i++)
  {
    priceArr.add(price[i]);
  }

  JsonArray program1Arr = jsonDoc.createNestedArray("program1");
  for (int i = 0; i < 3; i++)
  {
    program1Arr.add(program1[i]);
  }

  JsonArray program2Arr = jsonDoc.createNestedArray("program2");
  for (int i = 0; i < 3; i++)
  {
    program2Arr.add(program2[i]);
  }

  JsonArray program3Arr = jsonDoc.createNestedArray("program3");
  for (int i = 0; i < 3; i++)
  {
    program3Arr.add(program3[i]);
  }

  JsonArray rinStep2Arr = jsonDoc.createNestedArray("rinStep2");
  for (int i = 0; i < 2; i++)
  {
    rinStep2Arr.add(rinStep2[i]);
  }

  JsonArray rincommandArr = jsonDoc.createNestedArray("rincommand");
  for (int i = 0; i < 2; i++)
  {
    rincommandArr.add(rincommand[i]);
  }

  jsonDoc["spin"] = spin;

  JsonArray drumArr = jsonDoc.createNestedArray("drum");
  for (int i = 0; i < 3; i++)
  {
    drumArr.add(drum[i]);
  }

  JsonArray checkRunTimeArr = jsonDoc.createNestedArray("check_run_time");
  for (int i = 0; i < 3; i++)
  {
    checkRunTimeArr.add(check_runing_time[i]);
  }

  JsonArray TimeCountDrumArr = jsonDoc.createNestedArray("TimeCountDrum");
  for (int i = 0; i < 2; i++)
  {
    TimeCountDrumArr.add(TimeCountdowndrum[i]);
  }

  JsonArray TimeCount1Arr = jsonDoc.createNestedArray("TimeCount1");
  for (int i = 0; i < 2; i++)
  {
    TimeCount1Arr.add(TimeCountdown1[i]);
  }

  JsonArray TimeCount2Arr = jsonDoc.createNestedArray("TimeCount2");
  for (int i = 0; i < 2; i++)
  {
    TimeCount2Arr.add(TimeCountdown2[i]);
  }

  JsonArray TimeCount3Arr = jsonDoc.createNestedArray("TimeCount3");
  for (int i = 0; i < 2; i++)
  {
    TimeCount3Arr.add(TimeCountdown3[i]);
  }

  JsonArray timerDryArr = jsonDoc.createNestedArray("timerDry");
  for (int i = 0; i < 3; i++)
  {
    timerDryArr.add(timerDry[i]);
  }

  jsonDoc["ldr_set"] = ldr_set;
  jsonDoc["StateShutdown"] = StateShutdown;
  jsonDoc["SetupData"] = SetupData;

  JsonArray priceProArr = jsonDoc.createNestedArray("pricePro");
  for (int i = 0; i < 3; i++)
  {
    priceProArr.add(pricePro[i]);
  }

  preferences.end(); // ปิด Preferences

  String jsonString;
  serializeJson(jsonDoc, jsonString);
  Serial.println(jsonString);
  // mqclient.publish("check", jsonString.c_str());
  delay(100);

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!netLockEnter())
      return;
    HTTPClient http;
    http.begin("http://mawell.thddns.net:4740/check");
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonString);
    Serial.print(httpResponseCode);
    http.end();
    netLockLeave();
  }
  else
  {
    // moneyEEprom = moneyEEprom + paul;
  }
}

void commandApp()
{
  if (cm == "cmProgram")
  {
    // ตั้งค่าโปรโมชั่นหลายช่วงเวลา/หลายวัน ผ่าน MQTT
    if (value_str2 == "setPromoSlots")
    {
      if (isMelodyBootSetupPhase())
      {
        bootMelodyPromoPayload = value_str1;
        bootMelodyPromoPending = true;
        bootMelodySyncLastMs = millis();
        Serial.println(F("[MQTT] เก็บ setPromoSlots (รอชุดสุดท้ายจาก Melody)"));
        return;
      }
      applyPromoSlotsPayload(value_str1);
    }
    else if (value_str2.toInt() == 0)
    {
      status_machine_run = false;
      status_machine_prepare = false;
      chanel = 0;step = 0;
      statedisplaystandby = 3;
      display.setSegments(SEG_Mode);
      Serial.println("command reset program");
      stateWhile = false;
      stateReset = true;
      // chanel = 10;
      // endProgram = false;
    }
    else if (value_str2.toInt() == 7 && status_machine_run)
    {
      if (Mode == 2)
      {
        minn = minn + DRY_EXTEND_MIN_PER_COIN;
        if (minn >= 60)
        {
          hrs++;
          minn = minn - 60;
        }
      }
    }
    else if (value_str2.toInt() == 9)
    { // off
      // not thing
    }
    else
    {
      if (!status_machine_prepare && !status_machine_run && value_str2.toInt() != 7)
      {
        statedisplaystandby = 3;
        program = value_str2.toInt();
        Serial.println("command run program : " + String(program));
        setStartMachine();
      }
    }
    
    String StateOK = "OK";
    String cmProgramBack = "cmProgramBack";
    String msg = "{\"cm\":\"" + cmProgramBack + "\",\"id\":\"" + Noserial + "\",\"value_str1\":\"" + gid + "\",\"value_str2\":\"" + StateOK + "\"}";
    msg.toCharArray(pendingCommandBackBuf, sizeof(pendingCommandBackBuf));
    pendingCommandBackPublish = true;
    Serial.println("sent to command received ..!! :: " + Noserial + " :: " + cmProgramBack + " : " + gid + " : " + StateOK);

      /*{
        "cm": "cmProgram",
        "id": "65M000000",
        "value_str1": "99",
        "value_str2": "1"
      }*/
  }
  else if (cm == "cmCommand")
  {
    if (value_str2 == "power")
    {
      Power();
    }
    else if (value_str2 == "jok")
    {
      Jok();
    }
    else if (value_str2 == "start")
    {
      Start();
    }
    else if (value_str2 == "temp")
    {
      Temp();
    }
    else if (value_str2 == "Spin")
    {
      Spin();
    }
    else if (value_str2 == "Shutdown")
    {
      publishPresenceOfflineGraceful();
      preferences.begin("config", false);
        StateShutdown = 1;
        preferences.putInt("StateShutdown", StateShutdown);
      preferences.end();
      state_error = 4;
      chanel = 11;
      suspendMachineTasksForOta();
      delay(1000);
      if (OldBoard == 1)
      {
        Slot(1);
      }
      else
      {
        Slot(0);
      }
      display.setSegments(off);
    }
    else if (value_str2 == "Restart")
    {
      runSessionClear();
      preferences.begin("config", false);
        StateShutdown = 0;
        preferences.putInt("StateShutdown", StateShutdown);
      preferences.end();
      step = 0;
      statedisplaystandby = 0;
      chanel = 0;
      delay(1000);
      publishPresenceOfflineGraceful();
      ESP.restart();
    }
    else if (value_str2 == "slot")
    {
      step = 0;chanel = 0;
      statedisplaystandby = 3;
      if (pinSlot == SIG_PIN)
      {
        pinSlot = SIG_PIN2;
      }
      else
      {
        pinSlot = SIG_PIN;
      }
      Serial.println("pinSlot = " + String(pinSlot));
      if (!preferences.begin("config", false))
      {
        Serial.println("Failed to open preferences");
        return;
      }
      preferences.putInt("pinSlot", pinSlot);
      preferences.end();

      display.setSegments(SL_TEXT, 2, 0);
      display.showNumberDec(pinSlot, false, 2, 2);
      delay(5000);
    }
    else if (value_str2 == "update")
    {
      chanelLdrCheck = chanel;
      stepLdrCheck = step;
      displaystandbyLdrCheck = statedisplaystandby;
      suspendMachineTasksForOta();
      display.setSegments(SEG_Up);
      statedisplaystandby = 4;
      chanel = 0;
      step = 0;
      if (OldBoard == 1)
      {
        Slot(1);
      }
      else
      {
        Slot(0);
      }
      delay(1000);
      UpdateFw = true;
    }
    else if (value_str2 == "ESP")
    {
      step = 0;chanel = 0;
      statedisplaystandby = 3;
      Serial.println("******* rebooting *******");
      display.setSegments(SEG_boot);
      delay(3000);
      publishPresenceOfflineGraceful();
      ESP.restart();
    }
    else if (value_str2 == "check")
    {
      sentVarjson();
    }
    else if (value_str2 == "LdrOpen")
    {
      chanelLdrCheck = chanel;
      stepLdrCheck = step;
      displaystandbyLdrCheck = statedisplaystandby;
      status_machine_run = false;
      step = 0;chanel = 0;
      statedisplaystandby = 3;
      stateCheckLdr1 = true;
    }
    else if (value_str2 == "LdrClose")
    {
      chanel = chanelLdrCheck;
      step = stepLdrCheck;
      statedisplaystandby = displaystandbyLdrCheck;
      if(stepLdrCheck != 0)
      {
        status_machine_run = true;
      }
      stateCheckLdr1 = false;
    }
    else if (value_str2 == "LdrOpen2")
    {
      chanelLdrCheck = chanel;
      stepLdrCheck = step;
      displaystandbyLdrCheck = statedisplaystandby;
      status_machine_run = false;
      step = 0;chanel = 0;
      statedisplaystandby = 3;
      stateCheckLdr2 = true;
    }
    else if (value_str2 == "LdrClose2")
    {
      chanel = chanelLdrCheck;
      step = stepLdrCheck;
      statedisplaystandby = displaystandbyLdrCheck;
      if(stepLdrCheck != 0)
      {
        status_machine_run = true;
      }
      stateCheckLdr2 = false;
    }
    else if (value_str2 == "Setup")
    {
      SetupData = 1;

      preferences.begin("config", false); // เปิด Preferences (โหมดอ่านอย่างเดียว)
      preferences.putInt("SetupData", SetupData);
      preferences.end(); // ปิด Preferences
      step = 0;chanel = 0;
      statedisplaystandby = 3;
      Serial.println("*******setup complete and rebooting *******");
      display.setSegments(SEG_boot);
      delay(3000);
      publishPresenceOfflineGraceful();
      ESP.restart();
    }
    else if (value_str2 == "BT1")
    {
      if (chanel == 12)
      {
        BT = 1;
      }
    }
    else if (value_str2 == "BT2")
    {
      if (chanel == 12)
      {
        BT = 2;
      }
    }
    else if (value_str2 == "BT3")
    {
      if (chanel == 12)
      {
        BT = 3;
      }
      else if (chanel == 0)
      {
        Serial.println("button = " + String(sw_name[2]));
        test = true;
        if (!status_machine_run)
        {
          count++;
          item_price = item_price + (count * coinValue);
          statedisplaystandby = 1;
          Serial.println("item price = " + String(item_price));
          count = 0;
          timerstanby = millis();
        }
        else if (Mode == 2 && status_machine_run)
        {
          minn = minn + DRY_EXTEND_MIN_PER_COIN;
          if (minn >= 60)
          {
            hrs++;
            minn = minn - 60;
          }
        }
      }
    }
    else if (value_str2 == "BT4")
    {
      if (chanel == 12)
      {
        BT = 4;
      }
      else if (chanel == 0)
      {
        chanel = 12;
      }
    }
    else if (value_str2 == "BT5")
    {
      status_machine_run = false;
      status_machine_prepare = false;
      display.setSegments(SEG_Mode);
      Serial.println("button = " + String(sw_name[4]));
      chanel = 10;
      endProgram = false;
    }
    else if (value_str2 == "Setfirst")
    {
      writePreferencesfirst(); 
    }
  }
}
void SetTimerSend()
{
  if (minn < 10)
  {
    TimeSent = "0" + String(hrs) + ":" + "0" + String(minn);
  }
  else
  {
    TimeSent = "0" + String(hrs) + ":" + String(minn);
  }
}
void SetStatusControl()
{
  if (StatusControl == "00" || StatusControl == "01" || StatusControl == "02")
  {
    // not thing
  }
  else
  {
    if (program == 1)
    {
      StatusControl = "1on";
    }
    else if (program == 2)
    {
      StatusControl = "2on";
    }
    else if (program == 3)
    {
      StatusControl = "3on";
    }
    else if (program == 4)
    {
      StatusControl = "Drum";
    }
    else if (program == 5)
    {
      StatusControl = "rin";
    }
    else if (program == 6)
    {
      StatusControl = "spin";
    }
    else if (program == 7)
    {
      StatusControl = "+10";
    }
    else if (program == 0)
    {
      StatusControl = "off";
    }
  }
}

bool UpdateBalanceV3(int amount)
{
  // dataMoney = dataMoney + inMoney;
  if (WiFi.status() == WL_CONNECTED && statewifi)
  {
    if (!netLockEnter())
      return false;
    HTTPClient http;
    http.begin(ServerSentBalanceV3);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    // Command = "Balance";

    String httpRequestData = "{\"Title\":\"" + Noserial + "\",\"ID\":\"" + IDserver + "\",\"Program\":\"" + String(program) + "\",\"Price\":\"" + String(amount) + "\",\"Slot\":\"" + String(amount) + "\",\"Qr\":\"" + String("0") + "\"}";
    int httpResponseCode = http.POST(httpRequestData);

    Serial.print("HTTP Balance " + Noserial + " code : ");
    Serial.print(httpResponseCode);

    if (httpResponseCode == 202)
    {
      Serial.println(" HTTP Send Data Balance : " + String(item_price) + " Bath : Complete...");
      http.end();
      netLockLeave();
      return true;
    }
    else
    {
      Serial.println(" HTTP Send Data Balance Not Complete...");
    }
    http.end();
    netLockLeave();
    return false;
  }
  else
  {
    // moneyEEprom = moneyEEprom + paul;
  }
  return false;
}

void CheckPromotion()
{
  static unsigned long timerCheckPro = millis();
  if (millis() - timerCheckPro >= 1000)
  {
    int dayNow = rtc.getDayofWeek();
    int hourNow = rtc.getHour(true);
    int minNow = rtc.getMinute();

    // ถ้ามี promoSlots จาก MQTT ให้ใช้โหมดใหม่ (หลายช่วงต่อวัน และราคาโปรต่อช่วง)
    if (promoSlotCount > 0)
    {
      bool promoActive = false;
      PromoSlot *activeSlot = NULL;
      for (int i = 0; i < promoSlotCount; i++)
      {
        PromoSlot &ps = promoSlots[i];
        if (ps.day != dayNow) continue;

        bool afterStart = (hourNow > ps.startHour) || (hourNow == ps.startHour && minNow >= ps.startMin);
        bool beforeEnd = (hourNow < ps.endHour) || (hourNow == ps.endHour && minNow <= ps.endMin);

        if (afterStart && beforeEnd)
        {
          promoActive = true;
          activeSlot = &ps;
          break;
        }
      }

      if (promoActive && activeSlot != NULL)
      {
        for (int i = 0; i < 3; i++)
        {
          PriceShow[i] = activeSlot->pricePro[i];
        }
      }
      else
      {
        for (int i = 0; i < 3; i++)
        {
          PriceShow[i] = price[i];
        }
      }
    }
    else
    {
      for (int i = 0; i < 3; i++)
      {
        PriceShow[i] = price[i];
      }
    }
    timerCheckPro = millis();
  }

  if (millis() < timerCheckPro)
  {
    timerCheckPro = millis();
  }
}

void Button()
{
  if (digitalRead(sw_pin[2]) == 0)
  {
    BT = 3;
    while (digitalRead(sw_pin[2]) == 0)
    {
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
    }
    Serial.println("button = " + String(sw_name[2]));
    delay(300);
  }
  if (digitalRead(sw_pin[1]) == 0)
  {
    BT = 2;
    while (digitalRead(sw_pin[1]) == 0)
    {
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
    }
    Serial.println("button = " + String(sw_name[1]));
    delay(300);
  }
  if (digitalRead(sw_pin[3]) == 0)
  {
    BT = 4;
    while (digitalRead(sw_pin[3]) == 0)
    {
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
    }
    Serial.println("button = " + String(sw_name[3]));
    delay(300);
  }
  if (digitalRead(sw_pin[0]) == 0)
  {
    BT = 1;
    while (digitalRead(sw_pin[0]) == 0)
    {
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
    }
    Serial.println("button = " + String(sw_name[0]));
    delay(300);
  }
}
void checkbuttonFirst()
{
  if (digitalRead(sw_pin[3]) == 0)
  { // setting
    while (digitalRead(sw_pin[3]) == 0)
    {
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
    }
    delay(300);
    Serial.println("button = " + String(sw_name[3]));
    chanel = 12; // setting mode
  }

  if (digitalRead(sw_pin[2]) == 0)
  {
    while (digitalRead(sw_pin[2]) == 0)
    {
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield กันปุ่มค้างกิน CPU -> idle WDT reboot
    }
    delay(300);
    Serial.println("button = " + String(sw_name[2]));
    test = true;
    if (!status_machine_run)
    {
      count++;
      item_price = item_price + (count * coinValue);
      statedisplaystandby = 1;
      Serial.println("item price = " + String(item_price));
      count = 0;
      timerstanby = millis();
    }
    else if (Mode == 2 && status_machine_run)
    {
      minn = minn + DRY_EXTEND_MIN_PER_COIN;
      if (minn >= 60)
      {
        hrs++;
        minn = minn - 60;
      }
    }
  }
}
void buttonReset()
{
  if (digitalRead(sw_pin[4]) == 0)
  { // reset / long-press -> WiFi setup
    unsigned long pressStart = millis();
    while (digitalRead(sw_pin[4]) == 0)
    {
      // ถ้ากดค้างเกิน 10 วินาที และเครื่องไม่ได้ทำงาน -> เข้าโหมดตั้งค่า WiFi
      if ((millis() - pressStart >= 10000) && !status_machine_run && !status_machine_prepare)
      {
        Serial.println("Long-press RESET -> enter WiFi setup mode");
        enterWifiConfigMode(); // ฟังก์ชันนี้จะไม่คืนค่า (รีสตาร์ตเมื่อบันทึกเสร็จ)
      }

      // พฤติกรรม reset เดิม
      status_machine_run = false;
      status_machine_prepare = false;
      chanel = 0;
      step = 0;
      statedisplaystandby = 3;
      display.setSegments(SEG_Mode);
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield ระหว่างกดค้าง (long-press 10s เชื่อถือได้)
    }
    delay(300);
    stateWhile = false;
    stateReset = true;
    Serial.println("button = " + String(sw_name[4]));
  }
}
void setMode()
{
  static bool settingMode = false;
  static bool setupVarable = false;
  if (digitalRead(sw_pin[0]) == 0)
  { // setting mode
    timerSetMode = millis();
    while (digitalRead(sw_pin[0]) == 0)
    {
      if (millis() - timerSetMode >= 5000)
      {
        Serial.println("set Mode Now...");
        display.setSegments(SEG_Mode);
        settingMode = true;
      }
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield ระหว่างกดค้าง
    }
    if (settingMode)
    {
      settingMode = false;
      chanel = 13; // setting mode
    }
  }

  if (digitalRead(sw_pin[1]) == 0)
  { // setup varable
    timerSetMode = millis();
    while (digitalRead(sw_pin[1]) == 0)
    {
      if (millis() - timerSetMode >= 5000)
      {
        Serial.println("set varable Now...");
        display.showNumberDecEx(0, 0b01000000, true, 4, 0); // แสดง 00:00 (มีจุด)
        setupVarable = true;
      }
      vTaskDelay(pdMS_TO_TICKS(5));  // cooperative yield ระหว่างกดค้าง
    }
    if (setupVarable)
    {
      setupVarable = false;
      SetupData = 1;

      preferences.begin("config", false); // เปิด Preferences (โหมดอ่านอย่างเดียว)
      preferences.putInt("SetupData", SetupData);
      preferences.end(); // ปิด Preferences

      Serial.println("*******setup complete and rebooting *******");
      delay(3000);
      publishPresenceOfflineGraceful();
      ESP.restart();
    }
  }
}

void printTocore()
{
  Serial.print("checkcoin core : ");
  Serial.print(xPortGetCoreID());
  Serial.print(" Piority : ");
  Serial.println(uxTaskPriorityGet(NULL));
}