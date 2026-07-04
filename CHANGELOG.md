# ESP Firmware Changelog — ATD_TM_V3_New_Hier

รูปแบบเวอร์ชัน: `Version X.YY` ใน `varable.h` → `fwversion[1]` (OTA / presence MQTT)

**กฎทีม:** ทุกครั้งที่แก้โค้ด → bump เวอร์ชัน **คู่กับ ATD35_Melody_V3** (เลขเดียวกัน) + เขียนหัวข้อใหม่ด้านบนทั้งสองโปรเจกต์ + ใส่ `### Rollback` (ดู `.cursor/rules/firmware-version-rollback.mdc` และ `firmware-tm-atd35-sync.mdc`)

---

## Version 3.76 (2026-07-04)

### แก้ crash `pbuf_free: p->ref > 0` (recv ซ้อนข้าม task) + คืน keepAlive 60

- **หลักฐาน (v3.75 log):** keepAlive 15 → drop rc=-4 ถี่ขึ้นมาก (ทุก ~30 วิ) → churn reconnect สูง → crash:
  ```
  assert failed: pbuf_free ... (pbuf_free: p->ref > 0)
  #14 PubSubClient::loop()  #15 mqttPumpLoopLocked  #16 taskWifiMqtt
  ```
- **สาเหตุจริง:** `updateWiFiIcon()` (รันใน **taskDisplay**) เรียก `mqclient.connected()` ตรง ๆ → `WiFiClient::connected()` เรียก `recv()` บน socket **พร้อมกับ** `mqclient.loop()` ที่ `recv()` ใน taskWifiMqtt = อ่าน socket เดียวกัน 2 task → lwIP pbuf double-free → รีบูต
- **แก้:**
  - เพิ่ม cache `volatile bool g_mqttOnline` อัปเดตเฉพาะใน taskWifiMqtt; `updateWiFiIcon()` + web/LVGL config handler อ่าน cache แทน (ไม่แตะ socket ข้าม task)
  - คืน `setKeepAlive(60)` — keepAlive 15 พิสูจน์แล้วว่า drop ถี่ขึ้น + churn กระตุ้น crash (broker ไม่ตอบ ping ตอน idle จริง)
- ไฟล์: `src/main.cpp`

### Rollback

- ย้อนไป: **Version 3.74** (keepAlive 60, ไม่มี cache — มีความเสี่ยง crash เดิม)
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.75 (2026-07-04)

### ทดลอง keepAlive 60→15 (แก้ MQTT หลุด rc=-4 ตอน idle) — อยู่ระหว่างทดสอบหน้างาน

- **หลักฐาน (v3.72 log):** `[MQTT] dropped rc=-4 wifi=3 rssi=-51` วนซ้ำตอน off/idle → ping timeout, WiFi/สัญญาณดี = broker/NAT ปิด TCP ตอน idle ก่อน 60s
- **ทดลอง:** `setKeepAlive(15)` — ping ทุก 15s อุ่น connection กัน idle-close
- **สถานะ:** กำลังทดสอบว่า drop ลดลงจริงไหม (ถ้าลด = idle-close; ถ้าเท่าเดิม/ถี่ขึ้น = broker ไม่ตอบ ping → ย้อนกลับ 60)
- ไฟล์: `src/main.cpp`

### Rollback

- ย้อนไป: **Version 3.74** (keepAlive 60)

---

## Version 3.74 (2026-07-04)

### 5 นาทีสุดท้าย ส่ง UpdateState ทุก 1 นาที (เวลา ESP↔server ตรงกันสุด)

- เดิม: ส่งทุก `statusReportIntervalMinutes` (default 5 นาที) ตลอดรอบ
- เพิ่ม: เมื่อเวลาที่เหลือ `hrs==0 && minn<=5` → ส่งทุก 1 นาที เพื่อให้ค่าเวลาใกล้จบตรงกับ server มากที่สุด
- ไฟล์: `src/main.cpp` (`machineRuning`)

### Rollback

- ย้อนไป: **Version 3.73**

---

## Version 3.73 (2026-07-04)

### log RunSession save แสดงเวลาที่บันทึก

- เพิ่มเวลา (`hrs:minn:second` จาก snapshot จริง) ต่อท้าย `[RunSession] save phase=X time=H:M:S`
- ช่วยยืนยันว่า autosave เก็บเวลาที่เหลือถูกต้อง (phase เดิมแต่เวลาเปลี่ยน)
- ไฟล์: `src/run_session.h`

### Rollback

- ย้อนไป: **Version 3.72**

---

## Version 3.72 (2026-07-04)

### แก้ MQTT หลุดซ้ำทั้งที่ WiFi ยังต่อ (socket timeout 6→15 = ตรง 3.00) + log สาเหตุ

- **อาการ (3.69 หน้างาน):** ระหว่างอบ connect 4741 สำเร็จทุกครั้ง แต่ ~นาทีละครั้งหลุด→reconnect (มี `presence online` ซ้ำทุกรอบ = fresh connect จริง)
- **สาเหตุ:** log เดิมไม่บอกเหตุ (พิมพ์ "MQTT server" หลังหลุดแล้ว) แต่ connect สำเร็จทุกครั้ง = broker ไม่ปฏิเสธ → socket ถูกตัดหลัง connect จุดต่างจริงจาก 3.00 (เสถียร) = `setSocketTimeout(6)`/`client.setTimeout(6000)` — 3.00 ใช้ default 15s → 6s ตัด socket เร็วเกินตอน WiFi jitter/แพ็กเก็ตช้า
- **แก้:**
  - `setSocketTimeout(15)` + `client.setTimeout(15000)` + `MQTT_SOCKET_TIMEOUT=15` (platformio) — ตรงกับ 3.00
  - เพิ่ม log `[MQTT] dropped rc=.. wifi=.. rssi=..` ตอน connected→disconnected เพื่อยืนยันสาเหตุหน้างาน (rc=-3 TCP ถูกตัด / -4 ping timeout / -1 เราสั่ง)
- **ยืนยันหน้างาน:** ถ้ายังหลุด ดู `rc`: `-4`=keepalive/loop starve, `-3`=network ตัด (RSSI ต่ำ?), `-1`=teardown จาก WiFi hysteresis

### Rollback

- ย้อนไป: **Version 3.71**
- ไฟล์: `src/main.cpp`, `platformio.ini`, `src/varable.h`

---

## Version 3.71 (2026-07-04)

### MQTT reconnect — backoff เบา 5→15s + เปลี่ยนพอร์ตหลัง fail 4 ครั้งในพอร์ตเดิม

- อยู่พอร์ตเดิมก่อน (broker หลักอาจแค่สะดุด) — fail ครบ **4 ครั้งในพอร์ตเดียว** ค่อยหมุนพอร์ต, เปลี่ยนแล้วเริ่ม backoff ใหม่ที่ 5s
- backoff **5→10→15s** (cap 15s) ระหว่างพยายามพอร์ตเดิม — ไม่ยาวถึง 60s
- คงไว้: keepalive 60s, single-close (3.69), pump loop() ทุกลูป

### Rollback

- ย้อนไป: **Version 3.70**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.70 (2026-07-04)

### MQTT reconnect กลับเป็น 3.00-style (แก้ "หลุดบ่อย/ต่อกลับช้า")

- **อาการ:** MQTT หลุดแล้วต่อกลับช้า/นาน
- **สาเหตุ:** exponential backoff 5→60s (รัน cap 15s) — หลุดทีต้องรอนานถึงจะ retry (3.00 retry คงที่ 5s เสมอ)
- **แก้:** `mqttreconnect()` retry **คงที่ 5s** + หมุน port ทุกครั้งที่ fail (ตรง 3.00) — ตัด backoff/`mqttPortsTried` ออก
- คงไว้: keepalive 60s, single-close (3.69), pump `mqclient.loop()` ทุกลูปตอน connected
- **หมายเหตุ loop:** การ pump loop() ปัจจุบันเพียงพอแล้ว (~ทุก 10ms ตอน connected เท่า 3.00) — ตัวที่ทำให้รู้สึกหลุดคือ backoff

### Rollback

- ย้อนไป: **Version 3.69**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.69 (2026-07-04)

### แก้ crash รีบูตกลางงาน: lwIP `assert pbuf_free: p->ref > 0` (double-close socket)

- **อาการ (3.65 หน้างาน):** เครื่องกำลังอบ (00:10:52) → MQTT reconnect → `[MQTT] presence online` → crash `assert pbuf_free p->ref>0` ใน `mqttPumpLoopLocked` (`mqclient.loop()`) → `SW_CPU_RESET`
- **สาเหตุ:** ปิด socket ซ้ำสองครั้ง — `mqclient.disconnect()` (เรียก `client.stop()` ในตัวอยู่แล้ว) ตามด้วย `client.stop()` ซ้ำ → lwIP pbuf refcount เพี้ยน → assert ตอน recv ของ socket ใหม่ (3.32/3.00 ไม่มี pattern นี้ จึงไม่ crash)
- **แก้ (3 จุด):** `mqttreconnect()`, `teardownMqttOnWifiDown()`, `pauseMqttForOta()` → ปิด **ครั้งเดียว**: `if (connected) mqclient.disconnect(); else client.stop();`
- **หมายเหตุ:** 3.68 (watchdog/recovery) ไม่ได้แตะ crash ตัวนี้ — ต้อง 3.69

### Rollback

- ย้อนไป: **Version 3.68**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.68 (2026-07-04)

### กู้รอบงานเฉพาะไฟดับเท่านั้น (run-session recovery gate)

- **เจตนา:** เริ่มกู้รอบซัก/อบต่อ **เฉพาะเมื่อไฟดับจริง** — รีบูตจากสาเหตุอื่นไม่ต้องกู้ (กันระบบรวนจาก resume ผิดจังหวะ)
- **แก้:** `runSessionBeginRecovery()` เช็ค `esp_reset_reason()` — กู้เฉพาะ `ESP_RST_POWERON` / `ESP_RST_BROWNOUT`; reset จาก software / watchdog / crash / OTA → ล้าง snapshot + ไม่กู้
- ไฟล์: `src/run_session.h`

### Rollback

- ย้อนไป: **Version 3.67**
- ไฟล์: `src/run_session.h`, `src/varable.h`

---

## Version 3.67 (2026-07-04)

### watchdog เช็คห่างขึ้น 10 วิ (ต่อจาก 3.66)

- `loop()` เรียก `checkTaskHang()` ทุก **10 วิ** (เดิม 1 วิ) — ลดโอกาส false-trigger
- ยืนยัน: ตอนเครื่องทำงาน/เตรียม **ไม่รีบูทเองเลย** (3.66) — ระหว่างทำงานพฤติกรรมเท่ากับ 3.00 (ไม่มี watchdog)

### Rollback

- ย้อนไป: **Version 3.66**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.66 (2026-07-04)

### กันรีบูทเองระหว่างทำงาน (เทียบ 3.00 = เสถียรสุด ไม่มี watchdog)

- **อาการ:** 3.65 ยังรีบูทเองระหว่างเครื่องทำงาน
- **สาเหตุ:** `checkTaskHang()` ใน `loop()` (3.00 ไม่มี — `loop()` แค่ `delay(10)`) false-trigger กลางรอบซัก/อบ (busy-loop `checkLightStart`, relay delay, MQTT block) → `ESP.restart()`
- **แก้:** `checkTaskHang()` ข้ามการรีบูทเมื่อ `status_machine_run || status_machine_prepare` + feed heartbeat กันค้างสะสมหลังจบงาน — ตอนรันไม่รีบูทเอง (เหมือน 3.00), ตอน idle ยังกู้ตัวเองได้
- คงไว้: boot grace 60s, low-heap guard (ตอน idle), taskWifiMqtt core 1 (ตรง 3.00)

### Rollback

- ย้อนไป: **Version 3.65**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.65 (2026-07-04)

### จอ TM1637 — คืนส่วนโปรแกรม/จอ ให้ตรง 3.32 เป๊ะ (แก้กระพริบ)

- **root cause กระพริบ:** orphan reset `statedisplaystandby==3 → 0` ใน `case 0` (taskProgram) แข่ง (race) กับ `prepareRunMachine()`/`machineRuning()` ที่อยู่ใน **taskDisplay** — ตอนสลับ prepare→run มีช่องที่ `!run && !prepare` ชั่วขณะ → ปัด state เป็น 0 → `standbyDisplay()` แทรก 1 เฟรม
- **แก้ (ตาม 3.32):**
  - `case 0 statedisplaystandby==3` → **not thing** (ไม่ปัด state; orphan จัดการที่ `CH_RECOVERY`/`RUN_RECOVERY_ABORTED` อยู่แล้ว → 0000 ไม่กลับมา)
  - `setStartMachine()` → โชว์ `00:00` (มีจุด) ทั้งอบ/ซัก แล้วปล่อย `machineRuning()` เดินต่อ
  - `machineRuning()` → วาด `hrs:minn` toggle จุด แบบ 3.32 (`0b11100000`)
  - ลบ guard ใน `standbyDisplay()` และฟังก์ชัน `displayShowRunTimer()` (ไม่ใช้แล้ว)
- คงไว้: reset `chanel/step/indexSet=0` ใน setStartMachine (กันค้าง setting mode BT4), boot fix 3.52 (CH_RECOVERY/primeBootStandby)

### Rollback

- ย้อนไป: **Version 3.64**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.64 (2026-07-04)

### จอ TM1637 — คืนดีไซน์ 3.51 (statedisplaystandby=3 + machineRuning วาด timer ที่เดียว)

- ตัด `displayRunTimerStandbyState()` (แทรกวาดจาก taskProgram) — เป็นต้นเหตุกระพริบ/regression
- `statedisplaystandby==3`: งด `standbyDisplay()` เฉย ๆ (ตรง 3.51) + คืน 0 เมื่อ orphan (fix boot 3.52 คงไว้)
- `setStartMachine()` วาดเวลาเริ่มต้นครั้งเดียว → prepare ค้างจอ 2 วิ → run ให้ `machineRuning()` เดินต่อ
- คง guard `standbyDisplay()` return ตอน run/prepare (กัน 2 task แตะ TM ชนกัน)

### Rollback

- ย้อนไป: **Version 3.63**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.63 (2026-07-04)

### จอ TM1637 — แก้เวลากระพริบสลับ standby ตอนอบ/รัน

- **อาการ:** สั่งโหมด 2 (relay ทำงาน) จอโชว์เวลาสลับกับ standby (ตัววิ่ง)
- **สาเหตุ:** 2 task แตะ TM1637 (bit-bang) พร้อมกัน — `machineRuning()` วาดเวลา, `standbyDisplay()` แทรกวาดเมื่อ `statedisplaystandby` race เป็น 0
- **แก้:** `standbyDisplay()` return ทันทีถ้า `status_machine_run || status_machine_prepare` — machineRuning ถือจอที่เดียวตอนทำงาน (ตรงเจตนา 3.32)

### Rollback

- ย้อนไป: **Version 3.62**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.62 (2026-07-04)

### จอ TM1637 — แก้ regression 3.61 (สั่งโหมด 2 แล้วจอโชว์ standby ไม่โชว์เวลา)

- **สาเหตุ:** 3.61 ย้ายการวาดเวลาออกจาก `machineRuning()` → ตอน run ไม่มี task วาดเวลา จอตกไป standby (ตัววิ่ง)
- **แก้:** คืนการวาดเวลาใน `machineRuning()` (task เดียว) — prepare วาดที่ branch `statedisplaystandby==3`, run ปล่อย `machineRuning` วาด กัน TM bit-bang เขียนซ้อน
- เพิ่ม forward declaration `displayShowRunTimer()` (แก้ compile error 3.61)

### Rollback

- ย้อนไป: **Version 3.61**
- ไฟล์: `src/main.cpp`, `src/varable.h`
- โปรเจกต์คู่ ATD35: bump เวอร์ชันคู่ (ไม่มี logic จอ TM)

---

## Version 3.61 (2026-07-04)

### จอ TM1637 — timer ไม่ขึ้นหลังสั่งโปรแกรมจาก MQTT (หลัง BT4/setting)

- **`setStartMachine()`** — `chanel=0` ออกจาก setting (`chanel 12`) กัน `modeSetting`/`Anothersetting` ทับจอ timer
- **อบ (Mode 2)** — โชว์ `hh:mm` จริงทันที (ไม่ค้าง `----` / `00:00`) ตรง Melody `00:31`
- **`commandApp` cmProgram** — ไม่เรียก `SEG_Mode` ก่อน start (ลดทับ segment)
- **`statedisplaystandby==3` (chanel 0)** — วาด timer ใน `taskProgram` (`displayRunTimerStandbyState`); `machineRuning()` เหลือแค่นับเวลา

### Rollback

- ย้อนไป: **Version 3.60**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.60 (2026-07-04)

### แก้ reboot loop หลัง OTA — `task_wdt` IDLE0 / `taskWifiMqtt`

- **อาการ:** หลัง OTA/reboot ค้าง `MQTT server … port: 4741` ~30s แล้ว `task_wdt: IDLE0` + `CPU 0: taskWifiMqtt` วนรีบูท
- **สาเหตุ:** `taskWifiMqtt` อยู่ **core 0** แต่ `mqclient.connect()` block นาน → IDLE0 ไม่ได้รัน (TWDT 30s)
- **แก้:** ย้าย `taskWifiMqtt` ไป **core 1** (ตรง ATD35) + `vTaskDelay(1)` ก่อน/หลัง connect + socket timeout 6s

### Rollback

- ย้อนไป: **Version 3.59**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.59 (2026-07-04)

### OTA — แก้ "Written only : 16/…" ตอนอัพจาก Melody (3.57→3.58)

- **`otiUdate()`** — `suspendMachineTasksForOta()` ทุกครั้ง (OTA จาก HTTP/MQTT ไม่ผ่านเมนู update ไม่เคยหยุด display/program)
- **`pauseMqttForOta()`** หลัง `OtaStatus start` — `sendOtaStatusMqtt` reconnect MQTT แล้ว ต้องตัดก่อนดาวน์โหลด (แย่ง lwIP/WiFiClient)
- **`otaWriteStreamWithRetry()`** — แทน `Update.writeStream()` อ่าน chunk + retry เมื่อ stream ยังไม่พร้อม + log `Update.getError()`

### MQTT — แก้ rc=-4 / หมุน port ช้า (v3.58 หน้างาน)

- **OldBoard 1** เริ่ม `mqtt_port1=4741` (broker หลัก Melody) แทน 4742
- **Backoff หลังครบ 4 port** (4741–4744) — ไม่ double backoff ทุก port (เดิม 4741 รอ ~70s)
- **Socket timeout 8s** (เดิม 4s) — ลด rc=-4 ในร้านที่ network ช้า

### Rollback

- ย้อนไป: **Version 3.58**
- ไฟล์: `src/main.cpp`, `src/varable.h`
- โปรเจกต์คู่ ATD35: ไฟล์เดียวกัน

---

## Version 3.58 (2026-07-04)

### MQTT/WiFi — เสถียรขึ้น ลดหลุดบ่อย + ต่อได้ตอนเครื่องทำงาน

- **Warmup 4s → 2s** (standby) / **1s** เมื่อ `status_machine_run` / `status_machine_prepare`
- **WiFi down hysteresis 2.5s** — อย่า `teardownMqttOnWifiDown()` ทันทีเมื่อ WiFi สะดุดสั้น; pump keepalive ระหว่างรอ
- **`connectwifi()`** — อย่ารีเซ็ต warmup / disconnect stack จนกว่า WiFi หลุดยืนยันแล้ว
- **`mqttreconnect()`** — ใช้ port ปัจจุบัน; **หมุน port เฉพาะตอน connect fail** (เดิม ++ ทุกครั้งทำให้พลาด port 4741)
- **MQTT backoff cap 15s** ขณะเครื่องทำงาน (เดิม 60s)

### Rollback

- ย้อนไป: **Version 3.57**
- ไฟล์: `src/main.cpp`, `src/varable.h`
- โปรเจกต์คู่ ATD35: ไฟล์เดียวกัน

---

## Version 3.57 (2026-07-04)

### ยืนยันฮาร์ดแวร์ — TM = ESP32 classic ทั้งหมด (S3 เฉพาะ ATD35)

- ลบ `env:esp32s3_tm` — โปรเจกต์นี้ build/upload ด้วย `esp32dev` อย่างเดียว
- **OldBoard 0:** `userID = ai_new`, LDR 36/39 ตายตัว (ไม่มี branch S3)
- ESP32-S3 touch → **ATD35_Melody_V3** + OTA `ai_touch`

### Rollback

- ย้อนไป: **Version 3.56**
- ไฟล์: `platformio.ini`, `src/varable.h`

---

## Version 3.56 (2026-07-04)

### แก้ upload ล้ม — บอร์ดใหม่มีทั้ง ESP32 classic และ S3

- **อาการ:** `This chip is ESP32 not ESP32-S3. Wrong --chip argument?` ตอน upload — ตั้ง `default_envs=esp32s3_tm` แต่เครื่องจริง (เช่น `69M540494`) เป็น **ESP32 classic** เหมือน v3.32
- **`default_envs = esp32dev`** คืนค่า default เดิม
- **OldBoard 0:** `userID` + LDR pin แยกตามชิป (`CONFIG_IDF_TARGET_ESP32S3`) — classic ใช้ `ai_new` + GPIO 36/39; S3 ใช้ `ai_new_s3` + GPIO 1/2

### Rollback

- ย้อนไป: **Version 3.55**
- ไฟล์: `platformio.ini`, `src/varable.h`

---

## Version 3.55 (2026-07-04)

### บอร์ดใหม่ OldBoard 0 — บังคับ OTA ai_new_s3 (S3 ทั้งหมด)

- `userID = "ai_new_s3"` ตายตัว (ไม่มี `#if` / ไม่ fallback `ai_new`)
- LDR pin 1/2 ตายตัว (ไม่รองรับ ESP32 classic 36/39 ในบล็อก OldBoard 0)
- Melody backend: แมป `ai_new` และ legacy user_id บอร์ดใหม่ → `fw/ai_new_s3/`

### Rollback

- ย้อนไป: **Version 3.54**
- ไฟล์: `src/varable.h`, Melody `ota-user-id-map.util.ts`

---

## Version 3.54 (2026-07-04)

### OTA — แยก ESP32-S3 จาก ESP32 classic (chip mismatch error #9)

- **อาการ:** `boot_comm: mismatch chip ID, expected 0, found 9` — ดาวน์โหลด `fw/ai_new/firmware.bin` (ESP32) ลงบอร์ด ESP32-S3
- **`userID = ai_new_s3`** เมื่อ build `env:esp32s3_tm` (OldBoard 0); **`ai_new`** คงใช้กับ `env:esp32dev`
- **`fwUpdate_OTI_POST`** — ตรวจ chip ใน image header ก่อน `Update.begin` แจ้ง "chip mismatch" ชัดเจน
- **Melody backend:** โฟลเดอร์ `fw/ai_new_s3/` + validate chip ตอน admin upload

### Rollback

- ย้อนไป: **Version 3.53**
- ไฟล์ TM: `src/main.cpp`, `src/varable.h`
- MelodyWebapp: `backend/src/ota/ota.service.ts`, `ota-user-id-map.util.ts`, `frontend/.../MachineFormModal.tsx`

---

## Version 3.53 (2026-07-04)

### Boot loop ESP32-S3 + false WDT taskWifiMqtt (บอร์ดใหม่ OldBoard 0)

- **สาเหตุ log:** `[WDT] task hang detected -> restart: taskWifiMqtt` ~3.6s หลัง boot — false positive จาก `(now - hbWifiMs)` ตอน `now < hbWifiMs` (unsigned wrap) + ไม่มี boot grace
- **`hangElapsedMs()`** — เช็ค `now >= since` ก่อนเปรียบเทียบ; **`BOOT_HANG_GRACE_MS` 60s** หลัง boot งดเช็ค hang
- **`taskWifiMqtt`** ย้ายไป **core 0** + เก็บ handle สำหรับ watchdog
- **`esp_pm_configure`** ใช้เฉพาะ ESP32 classic (`CONFIG_IDF_TARGET_ESP32`) — ไม่เรียกบน S3
- **LDR pins S3:** `LDR1_PIN=1`, `LDR2_PIN=2` แทน 36/39 (ไม่มีบน ESP32-S3)
- **`platformio.ini`:** `esp32s3_tm` ตัวเดียว (release) + `default_envs`; ลบ `esp32s3_tm_release`
- รวม fix v3.52: `primeBootStandbyDisplay()` กันจอค้าง 0000

### Rollback

- ย้อนไป: **Version 3.51**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์ TM: `src/main.cpp`, `src/varable.h`, `platformio.ini`
- ไฟล์ ATD35: `src/main.cpp`, `src/varable.h`

---

## Version 3.52 (2026-07-04)

### บอร์ดใหม่ (OldBoard 0) — จอค้าง 0000 หลัง boot

- **`0000`** มาจาก `setupWaitAdminRestoreFactory()` (countdown 3 วิ) — หลังหมดเวลาไม่ได้สลับจอ → ค้างถ้า `statedisplaystandby==3` หรือ `chanel==CH_RECOVERY`
- **`primeBootStandbyDisplay()`** — หลัง boot countdown และท้าย `setup()` โชว์ standby ทันที
- **`CH_RECOVERY` grace** — วาด standby ระหว่างรอ run_session (ไม่ปล่อยจอค้าง 0000)
- **`statedisplaystandby==3`** แต่เครื่องไม่ทำงาน → รีเซ็ตเป็น 0 ให้วาด standby (กันค้างหลัง recovery abort / reset)

### Rollback

- ย้อนไป: **Version 3.51**
- โปรเจกต์คู่: bump เวอร์ชัน ATD35 คู่กัน (logic TM1637 เฉพาะ TM)
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.51 (2026-07-03)

### กู้รอบเครื่องซัก — เข้า step ที่บันทึก โชว์ timer อยู่ chanel 0

- state machine เครื่องซัก: countdown จริงอยู่ที่ `case 0`/chanel 0 กับ step 1/2/3 (chanel 1/2/6/8/9 เป็น action ชั่วคราวที่วนกลับ case 0)
- RESUMED handler สำหรับ wash: เข้า chanel 0 ที่ step เดิมจาก snapshot → `machineRuning()` โชว์ timer และเดินต่อ
- **กันค้าง startup:** ถ้า reboot ช่วง `RS_WASH_STARTUP` (step ยังเป็น 0) จะกู้ที่ `chanel = 1` เพื่อรัน Power→Start ใหม่จน step ถูกตั้ง (กันค้างที่ chanel 0/step 0 ซึ่งไม่เดินต่อ)
- อบ (Mode 2) คง `chanel = 0` เหมือนเดิม — `machineRuning()` เดินเวลาโดยไม่ขึ้นกับ chanel

### Rollback

- ย้อนไป: **Version 3.50**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์: `src/main.cpp` (handleRunSessionRecoveryChannel), `src/varable.h`

---

## Version 3.50 (2026-07-03)

### WiFi/MQTT — กัน pbuf_free crash ตอน presence + UpdateState ชนกัน

- **`teardownMqttOnWifiDown()`** — เมื่อ WiFi หลุด ตัด `mqclient` + `WiFiClient` ทันที (ก่อน `connectwifi` retry) แทนปล่อย socket ค้าง
- **`UpdateState`** — คิวผ่าน `pendingUpdateStatePublish` ไป `processDeferredMqttWork` รวม publish+pump ครั้งเดียวต่อรอบ (ไม่ `mqttPumpLoopLocked(4)` แยกจาก presence)
- **`stateUpdateState` / `stateSendConfigMqtt`** — ต้องผ่าน `wifiLinkUsable()` ก่อนส่ง MQTT
- **`postSQL`** — ลด pump เหลือ 1 รอบต่อการส่ง

### Task-hang watchdog — รีบูทกู้ตัวเองเมื่อ task ค้าง

- แต่ละ task (`taskDisplay` / `taskProgram` / `taskWifiMqtt`) อัปเดต heartbeat ต้นลูป
- `loop()` เป็นตัวเฝ้า: display/program ค้าง > 2 นาที หรือ wifi ค้าง > 6 นาที (เผื่อ OTA) → `ESP.restart()`
- **low-heap guard:** free heap < 8 KB ต่อเนื่อง 1 นาที → `ESP.restart()` (กัน crash จาก fragmentation ระยะยาว)
- ข้ามการเช็คระหว่าง OTA (`otaInProgress`) และ shutdown mode (task ถูกลบ / hb=0)
- แก้อาการจอ TM1637 ค้างเมื่อ task ใด task หนึ่ง deadlock (เช่นหลุด serial); กู้รอบซัก/อบต่อด้วย run_session

### กู้รอบหลังรีบูท — โชว์ timer ไม่ใช่ standby

- RESUMED handler ตั้ง `statedisplaystandby = 3` — งด `standbyDisplay()` ให้ `machineRuning()` โชว์ timer รอบที่กู้มา (เดิมโชว์จอ standby ทั้งที่ relay อบทำงานจริง)

### ปุ่ม busy-wait — กัน CPU starvation / idle-WDT reboot ตอนปุ่มค้าง

- `while (digitalRead(sw_pin)==LOW) {}` ทุกจุด (Button/setMode/buttonReset/checkbuttonFirst/setting) เพิ่ม `vTaskDelay(5ms)`
- long-press RESET 10 วินาที (เข้าโหมดตั้งค่า WiFi) ทำงานเชื่อถือได้ (ไม่ค้าง CPU ก่อนครบเวลา)

### Rollback

- ย้อนไป: **Version 3.49**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์: `src/main.cpp`, `src/varable.h`, `STABILITY_NOTES.md`

---

## Version 3.49 (2026-06-29)

### WiFi boot — non-blocking + reset stack หลัง connect ล้มเหลว

- **ลบ `WiFi_ini()` บล็อกใน `setup()`** — ต่อ WiFi ทั้งหมดผ่าน `connectwifi()` ใน `taskWifiMqtt` (แนวทางมาตรฐาน ESP: ไม่บล็อก boot)
- **`WiFi.disconnect(true)`** ก่อน retry และหลัง timeout — กัน WiFi stack ค้าง (อาการต่อไม่ได้จนกว่ารีบูท)
- ครั้งแรกหลัง boot: backoff 0 → พยายามต่อทันที; ล้มเหลวแล้ว backoff 1s→2s→… สูงสุด 30s
- NTP (`setupTime`) เรียกจาก `noteWifiLinkUp()` เมื่อต่อสำเร็จเท่านั้น (ATD35 ลบ `setupTime()` ใน setup)

### Rollback

- ย้อนไป: **Version 3.48**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์: `src/main.cpp`, `src/main.h`, `src/varable.h`

---

## Version 3.48 (2026-06-29)

### Mode 2 (อบ) — นาทีส่วนเกินเมื่อหยอดครั้งแรกเกินราคาแพ็กสูงสุด

- ตัวอย่าง: แพ็กสูงสุด 70 บาท = 50 นาที, ต่อเวลา 10 บาท = 10 นาที — หยอดครั้งแรก 100 บาท ได้ **80 นาที** (50 + 30)
- `setStartMachine(dryFirstPaymentBaht)` คำนวณ `(ยอดเกิน / coinValue) × DRY_EXTEND_MIN_PER_COIN`
- TM: ส่งยอดจาก `checkpriceprogram()`; ATD35: ส่ง `priceSentVerver - item_price` ตอนหยอดครบ
- ATD35: `pendingBalance` บันทึกยอดจ่ายจริงทั้งหมด (รวมส่วนเกิน)

### Rollback

- ย้อนไป: **Version 3.47**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.47 (2026-06-29)

### Boot — ย้าย ID จาก EEPROM ไป NVS ครั้งแรก (อัปเกรดจาก firmware เก่า)

- ถ้า NVS ยังไม่มี `Noserial` → อ่านจาก EEPROM (layout เดิม addr 70/82/106/138) ก่อน
- พบค่าใน EEPROM → ใช้ `Noserial`, `ssid`, `password`, `gid` แล้วบันทึก NVS
- ไม่พบ → ใช้ค่า default จาก `varable.h` แล้วบันทึก NVS
- ฟังก์ชัน: `tryLoadIdentityFromEeprom()` ใน `main.cpp` (TM + ATD35)

### Rollback

- ย้อนไป: **Version 3.46**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์: `src/main.cpp`, `src/varable.h`
- NVS: ไม่ต้องล้าง (เครื่องที่ migrate แล้วยังใช้ค่าใน NVS ได้)

---

## Version 3.46 (2026-06-27)

### Run session — Mode 2 autosave timer ทุก 10 นาที

- **`runSessionMaybeDryAutosave()`** ใน `machineRuning()` — เฉพาะโหมดอบ (`Mode == 2`) บันทึก NVS ทุก **10 นาที** (`RUN_SESSION_DRY_SAVE_MS`)
- **`runSessionSavePhase(..., force)`** — bypass dedupe เพื่ออัปเดต timer ระหว่างอบ
- โหมดซัก: ยังเขียน NVS เฉพาะเมื่อ step/phase เปลี่ยน (v3.45)

### Rollback

- ย้อนไป: **Version 3.45**
- ไฟล์: `src/run_session.h`, `src/main.cpp`, `src/varable.h`

---

## Version 3.45 (2026-06-27)

### Run session — NVS เขียนเฉพาะเมื่อ step/phase เปลี่ยน

- **ลบ autosave ทุก 2 นาที** (`runSessionMaybeAutosave`, `RUN_SESSION_SAVE_MS`)
- **`runSessionSavePhase`:** dedupe ด้วย checkpoint key (phase, step, stepHier, state_step2/3, pause_timer, drain_water) — snapshot timer ตอน checkpoint เท่านั้น
- **เพิ่ม save จุดเปลี่ยน step** ใน `taskProgram` (drain → rin → spin, case 6/8/9, Hier stepHier)
- **อบ (Mode 2):** NVS อัปเดตตอนเริ่มอบเท่านั้น — reboot กลางรอบอาจคลาดเวลานับถอยหลังเล็กน้อย

### Rollback

- ย้อนไป: **Version 3.44**
- ไฟล์: `src/run_session.h`, `src/main.cpp`, `src/varable.h`
- NVS: ไม่ต้องล้าง

---

## Version 3.44 (2026-06-27)

### MQTT — ลด pbuf_free crash หลัง presence online

- **`processDeferredMqttWork`:** รวม publish (presence/diag/commandBack) แล้ว **`mqttPumpLoopLocked` ครั้งเดียว** ต่อรอบ task
- **presence heartbeat:** คิวผ่าน `pendingPresenceHeartbeat` แทน publish+pump ทันที
- **ลบ `mqclient.loop()` ท้าย `taskWifiMqtt`** — ใช้ pump จาก deferred เท่านั้น
- **`publishMqttDiag`:** publish อย่างเดียว ไม่ pump ซ้อน

### Run session — ลดความถี่เขียน NVS

- `RUN_SESSION_SAVE_MS` 30s → **120s** (autosave ระหว่าง running)

### Rollback

- ย้อนไป: **Version 3.43**
- ไฟล์: `src/main.cpp`, `src/run_session.h`, `src/varable.h`

---

## Version 3.43 (2026-06-27)

### Run session — grace หลัง reboot 10 วิ (เดิม 5 วิ)

- `RECOVERY_GRACE_MS` 5000 → **10000** ใน `src/run_session.h` — รอให้เครื่องซักติดหลังไฟกลับก่อน ESP ตรวจ LDR / สั่ง relay

### Rollback

- ย้อนไป: **Version 3.42**
- ไฟล์: `src/run_session.h` (`RECOVERY_GRACE_MS` → 5000), `src/varable.h` (`fwversion` → 3.42)
- NVS: ไม่ต้องล้าง

---

## Version 3.42 (2026-06-27)

### Run session — กู้คืนรอบซัก/อบหลัง ESP reboot

- **`src/run_session.h` (ใหม่):** บันทึกสถานะรอบลง NVS namespace `runSession` (step, timer, program, flags)
- **Grace 5 วิ** ก่อนตรวจ LDR / สั่ง relay — รองรับไฟดับแล้วเครื่องซักติดก่อน ESP
- **เครื่องซัก:** ต้อง LDR สว่าง (≥2/3 sample) จึง resume — ไม่ยิง Power/Start ซ้ำ
- **เครื่องอบ (Mode 2):** ไม่อ่าน LDR — กู้ timer แล้ว **`Dry(1)`** ต่อไฟฮีตเตอร์; TM เพิ่ม loop ค้าง `Dry(1)` ตรง ATD35
- **`chanel=99` (CH_RECOVERY):** state machine กู้คืนใน `taskProgram`
- ล้าง session เมื่อจบรอบ / fault 00–02 / Restart / คืนค่าโรงงาน

### Rollback

- ย้อนไป: **Version 3.41**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์: `src/run_session.h` (ลบ), `src/main.cpp`, `src/varable.h` (`fwversion` → 3.41, `drain_water`/`stepHier`)
- NVS: ลบ namespace `runSession` ได้ถ้าต้องการ (ไม่บังคับ — ไม่มี session ก็ boot ปกติ)

---

## Version 3.41 (2026-06-20)

### แก้ WiFi reconnect ค้าง warmup — MQTT ไม่กลับมาหลัง WiFi ต่อใหม่

- เพิ่ม `noteWifiLinkUp()` ตั้ง `wifiConnectedSinceMs` เมื่อ WiFi กลับมา `WL_CONNECTED`
- **`taskWifiMqtt`:** ถ้า `WiFi.isConnected()` แต่ `wifiConnectedSinceMs == 0` ให้เริ่ม warmup ทันที (เดิมเรียก `connectwifi()` เฉพาะตอน WiFi หลุด จึงไม่เคยตั้ง timestamp หลัง auto-reconnect)
- **`connectwifi()`:** ตั้ง warmup เมื่อต่อสำเร็จแม้ state ไม่ใช่ `WIFI_CONNECTING`
- log warmup แสดงเวลาที่เหลือ (ms) แทนข้อความซ้ำถาวร

### Rollback

- ย้อนไป: **Version 3.40**
- ไฟล์: `src/main.cpp` (`noteWifiLinkUp`, `connectwifi`, `taskWifiMqtt`), `src/varable.h` (`fwversion` → 3.40)
- NVS: ไม่ต้องล้าง

---

## Version 3.40 (2026-06-20)

### LDR — อ่านเสถียรขึ้นเมื่อไฟไม่สม่ำเสมอ (เครื่องทำงานปกติ)

- **`LdrAvgSampler` / `readLDRAverage`:** ใช้ **median** แทนค่าเฉลี่ย + ตัด sample ต่ำกว่า 35 (glitch `avg=0`)
- **Mode 1 หลัง Power (case 2):** เพิ่ม **`LdrPeakWindow`** — ผ่านถ้า `peak` หรือค่าปัจจุบันข้าม `ldr_set` (จับไฟกระพริบ)
- **`checkLightStart` (บอร์ดเก่า):** ลดจำนวน `Light On Count` 10 → **7** รอบ
- ช่วงอ่าน LDR 700 ms (เดิม 900 ms), sample 10 ครั้ง (เดิม 8)

### Rollback

- ย้อนไป: **Version 3.39**
- ไฟล์: `src/ldr_sampler.h`, `src/main.cpp` (LDR / `LdrPeakWindow` / `checkLightStart`), `src/varable.h` (`fwversion` → 3.39)
- NVS: ไม่ต้องล้าง

---

## Version 3.39 (2026-06-20)

### แก้ pbuf_free crash ตอนสั่งโปรแกรม + LDR (หลัง v3.38)

- **ห้าม `mqttPumpLoopLocked()` ใน MQTT callback** — `commandApp()` คิว `commandBack` ไป `processDeferredMqttWork()` ใน `taskWifiMqtt` แทน (nested `loop()` ทำให้ lwIP พัง)
- **หลัง MQTT reconnect** — ไม่เรียก `publishPresenceOnline()` / `mqttDiag` ทันทีหลัง `subscribe` แต่ defer รอบถัดไป
- **`sentVarjson()`** — ห่อ HTTP ด้วย `netLockEnter()` (เดิมไม่มี lock)
- **`subscribe(configResponse/...)`** — ใช้ buffer ถาวร แทน temporary `String`

### Rollback

- ย้อนไป: **Version 3.38**
- ไฟล์: `src/main.cpp` (`processDeferredMqttWork`, `commandApp`/`callback`, `sentVarjson`, `mqttreconnect`), `src/varable.h` (`fwversion` → 3.38)
- NVS: ไม่ต้องล้าง

---

## Version 3.38 (2026-06-20)

### แก้ boot ค้าง warmup หลัง WiFi ต่อครั้งแรก

- แก้เส้นทาง `WiFi_ini()` ให้ตั้ง `wifiConnectedSinceMs` และ reset `wifiReconnectBackoffMs`
- เดิม `wifiLinkUsable()` ถูกปลดล็อกเฉพาะตอน reconnect ผ่าน `connectwifi()` ทำให้การต่อ WiFi ครั้งแรกหลัง boot ค้าง log `connected but warming up`
- หลังแก้แล้ว boot path และ reconnect path ใช้หลัก `WiFi stable window` เหมือนกัน

### Rollback

- ย้อนไป: **Version 3.37**
- ไฟล์: `src/main.cpp` (`WiFi_ini`, `wifiConnectedSinceMs`), `src/varable.h` (`fwversion` → 3.37)
- NVS: ไม่ต้องล้าง

---

## Version 3.37 (2026-06-20)

### WiFi reconnect - ลดโอกาสหลุดกลางต่อกลับ

- เพิ่ม `wifiLinkUsable()` รอให้ WiFi ต่อค้างอย่างน้อย **4 วินาที** ก่อนเริ่ม MQTT / HTTP
- เพิ่ม **backoff** ตอน `connectwifi()` fail: 1s -> 2s -> 4s ... สูงสุด 30s
- เพิ่ม **backoff** ตอน `mqttreconnect()` fail: 5s -> 10s -> 20s ... สูงสุด 60s
- ระหว่าง WiFi เพิ่งกลับมา จะ log `connected but warming up` และยังไม่ยิง `mqclient.loop()`, `pollMelodyDeviceHttp()`, `UpdateBalanceV3()`

### Rollback

- ย้อนไป: **Version 3.36** (MQTT reconnect ทันทีหลัง WiFi ต่อ ไม่รอ 4s — ทนทาน WiFi กระพริบกว่า v3.37+)
- ไฟล์: `src/main.cpp` (`wifiLinkUsable`, `connectwifi`, `mqttreconnect` backoff), `src/varable.h` (`fwversion` → 3.36)
- หมายเหตุ: ถ้า MQTT หลุดหลัง OTA 3.40 ลอง OTA กลับ 3.36/3.32 บนเครื่องที่มีปัญหา (เช่น `66M200187`)

---

## Version 3.36 (2026-06-18)

### แก้รีบูทกะทันหัน (assert `pbuf_free` ใน lwIP ตอน MQTT loop)

- **สาเหตุ:** `taskWifiMqtt` ส่งยอดค้าง (`pendingBalance`) ทั้ง **HTTP + MQTT พร้อมกัน** ขณะ MQTT ยังเชื่อมต่อ — ทำให้ lwIP buffer พัง (`pbuf_free: p->ref > 0`) มักเกิดช่วงเริ่มเครื่องหลังชำระเงิน / ตรวจ LDR
- **แก้:** MQTT ออนไลน์ → ส่ง `postSQL` ทาง MQTT เท่านั้น | MQTT ล่ม (fail > 20) → ส่ง HTTP (`UpdateBalanceV3`) เท่านั้น
- **เพิ่ม `gNetMutex` (recursive):** ห้าม HTTP กับ MQTT ทับซ้อนกัน — ครอบ `mqclient.loop/publish`, HTTPClient, reconnect
- **`mqttPumpLoopLocked()`:** flush packet หลัง publish (`UpdateState`, `postSQL`, ฯลฯ)

---

## Version 3.35 (2026-06-18)

### Mode 1 — ตรวจไฟเครื่องหลัง Power() (case 2)

- **`taskProgram` case 2 + `Mode == 1`:** เปลี่ยนจาก `LdrAvgSampler` (เฉลี่ย 8 ครั้ง / รอ 900 ms) เป็น **`readLDRInstant()`** — `analogRead()` ครั้งเดียวทุก loop
- **วัตถุประสงค์:** ตรวจว่าเครื่องติด / มีสัญญาณไฟ — ไฟจริงที่ LDR มักกระพริบ ค่าเฉลี่ยต่ำเกินไปจน error `00` ผิดพลาด
- **เงื่อนไขผ่าน:** บอร์ดเก่า `val > ldr_set` → `chanel = 3` | บอร์ดใหม่ `val <= ldr_set` → `chanel = 3` (logic เดิม)
- เพิ่ม `readLDRInstant()` ใน `ldr_sampler.h`

---

## Version 3.34 (2026-06-18)

### OTA / MQTT — แก้ค้าง "กำลังอัพเดท 0%" และ HTTP fallback

- **MQTT fail ก่อน HTTP fallback:** `MQTT_FAIL_STREAK_FALLBACK` 5 → **20** ครั้ง
  - `pollMelodyDeviceHttp`, `sendUpdateStateHttp`, รับคำสั่ง OTA ทาง HTTP — ทำงานหลัง fail เกิน 20 ครั้งเท่านั้น
- **`sendOtaStatusMqtt`:** เพิ่ม `ensureMqttForOtaStatus()` — reconnect MQTT หลายรอบก่อนส่งสถานะหลัง `pauseMqttForOta`
- **OTA เวอร์ชันตรงกัน:** ส่ง `OtaStatus phase=failed` ข้อความ `"เวอร์ชันตรงกัน"` (ไม่ปล่อยให้ Melody ค้าง updating)

### อ้างอิง MelodyWebapp backend (ต้อง deploy คู่กัน)

- HTTP OTA: เปิด/ปิดได้จากแอดมิน (`httpOtaEnabled`) — ปิดแล้วเปิดได้เฉพาะแอดมิน ไม่เปิดอัตโนมัติ
- ไม่ตั้ง `otaStatus=updating` ตอน HTTP device-ack — รอ `OtaStatus start` จาก ESP
- Cron เคลียร์ updating ค้าง > 15 นาที → `failed`
- ส่งคำสั่ง OTA ใน HTTP poll เมื่อ `fail_count >= 20` และ `httpOtaEnabled=true`

---

## Version 3.33 (ก่อนหน้า)

- ปรับเวอร์ชัน firmware ใน `varable.h`

---

## Version 3.32 (2026-06-14)

### LDR — อ่านค่าเสถียร ไม่ block MQTT

- เพิ่ม `src/ldr_sampler.h`
  - `setupLdrAdc()` — ตั้ง ADC 12-bit, attenuation 11dB, warm-up
  - `LdrAvgSampler` — เฉลี่ย 8 ครั้ง ห่าง 4 ms แบบ non-blocking
  - `readLDRAverage()` — blocking สั้น ~3 ms (โหมดตั้งค่า/แสดงค่า)
- ใช้ sampler ใน:
  - `checkLightStart()` — บอร์ดเก่า + ใหม่
  - `checkLdr1()` / `checkLdr2()`
  - `SetFirstHier()` — แทน `delay(500)` ด้วย throttle 500 ms
  - `taskProgram` case 2 — ตรวจ power (OldBoard + NewBoard)
  - `taskProgram` step 3 — ตรวจ ldr จบโปรแกรม (แทน loop `delay(10)×10`)
  - `case 11` state_error==3 — แสดงค่า LDR บนจอ
- แก้วงเล็บ `case 11` ที่ทำให้ compile error

### อ้างอิง Melody backend

- รหัสข้อผิดพลาด ESP `00/01/02` ส่งผ่าน `UpdateState` / `Upstatus` (ดู MelodyWebapp CHANGELOG)

---

## Version 3.31 (ก่อนหน้า)

- Melody MQTT v3: presence, command ack, HTTP fallback
- โปรโมชั่น `promoSlots`, config ผ่าน MQTT `configRequest` / `configResponse`
- OTA path `/ota/version`, `/ota/download`

---

## วิธีอ่าน log ตอน boot

Serial Monitor จะพิมพ์:

```
[FW] Current Firmware
[FW] Version 3.35
```
