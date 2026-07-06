# Stability Notes — ATD_TM_V3_New_Hier

อัปเดตล่าสุด: `Version 3.79`

## v3.79 — รายรับ HTTP idempotent + กัน MQTT flap ทำ loop() ขาด

- **รายรับส่ง HTTP เป็นหลัก** (`POST /public/machines/device-revenue`) แทน MQTT `postSQL` — ไม่หายแม้ MQTT flap
  - **idempotency key `txnId`** (`<Noserial>-<seq>`, persistent) — backend เก็บ `description="txn:<id>"` → retry ปลอดภัย ไม่เกิดรายรับซ้ำ (สำคัญ: เคยมีเคสรายรับซ้ำ 23,979 แถว)
  - persist `pendingBalance/sendAmt/sendTxn/txnSeq` ลง NVS namespace `revenue` → กู้ยอดค้างหลัง reboot
  - freeze batch (amount+txnId) ตอนเริ่มส่ง; coin ที่หยอดเพิ่มระหว่างส่งไป batch ถัดไป (txn ใหม่)
- **กัน flap:** throttle ส่งรายรับ HTTP ทุก 4s + pump `mqclient.loop()` ต้นรอบด้วย `netLockTryEnter(30ms)` — ไม่แตะ keepAlive(60)/socketTimeout(15)/port ตาม regression-guard
- **ยังเปิด:** root cause `rc=-4` idle (broker DDNS/NAT) — A ทำให้รายรับปลอดภัย แต่ presence ยังอาจ flap; ต้องเก็บ timing log connect→drop เพื่อปิดสนิท

## สิ่งที่ harden แล้ว

- กัน `HTTP + MQTT` ใช้ lwIP พร้อมกันด้วย `gNetMutex`
- ตอนส่ง `pendingBalance` (v3.79): ส่งทาง **HTTP idempotent** (buffer+retry จน 2xx) — เลิกใช้ MQTT postSQL/UpdateBalanceV3
- เพิ่ม `wifiLinkUsable()` รอ WiFi stable **2s** (standby) / **1s** (เครื่องทำงาน) ก่อน MQTT — v3.58 ลดจาก 4s
- **v3.58 WiFi hysteresis 2.5s:** อย่าตัด MQTT ทันทีเมื่อ WiFi กระพริบ; pump keepalive ระหว่างรอ
- **v3.58 mqtt port:** หมุน 4741–4744 เฉพาะตอน connect **fail** (ไม่ ++ ทุกครั้ง)
- เพิ่ม reconnect backoff
  - WiFi: `0s (boot) -> 1s -> 2s -> 4s ...` สูงสุด `30s`
  - MQTT: `5s -> 10s -> 20s ...` สูงสุด `60s`
- **v3.49:** WiFi ทั้งหมดใน `taskWifiMqtt` (`connectwifi`) — ไม่บล็อก `setup()`; `WiFi.disconnect(true)` หลัง fail
- แก้ reconnect path (v3.41): `taskWifiMqtt` + `noteWifiLinkUp()` ตั้ง warmup เมื่อ WiFi กลับมา
- **v3.50:** WiFi หลุด → `teardownMqttOnWifiDown()` ตัด MQTT/TCP ทันที; `UpdateState` คิวไป `processDeferredMqttWork` (pump ครั้งเดียวต่อรอบ)
- **v3.50 task-hang watchdog:** `loop()` เฝ้า heartbeat ทุก task — display/program ค้าง >2 นาที หรือ wifi >6 นาที → `ESP.restart()` (ข้ามช่วง OTA); กู้รอบด้วย run_session
- **v3.50 low-heap guard:** free heap < 8 KB ต่อเนื่อง 1 นาที → รีบูท (กัน crash จาก fragmentation)
- **v3.50 ปุ่ม busy-wait:** เพิ่ม `vTaskDelay` ในลูปรอปุ่มปล่อยทุกจุด — กัน CPU starvation/idle-WDT ตอนปุ่มค้าง
- **v3.53 task-hang WDT:** boot grace 60s + `hangElapsedMs()` กัน false trigger บน `taskWifiMqtt`
- **v3.60 `taskWifiMqtt` core 1:** อย่าปัก core 0 — `mqclient.connect()` block นานทำให้ `task_wdt` IDLE0 (~30s) หลัง OTA/reboot
- **v3.57 build:** โปรเจกต์ TM = ESP32 classic (`esp32dev`) เท่านั้น; S3 touch = โปรเจกต์ **ATD35_Melody_V3**
- **v3.52 boot display:** `primeBootStandbyDisplay()` หลัง countdown 0000 — กันจอค้างบอร์ดใหม่ OldBoard 0
- **v3.52–3.53 บอร์ดใหม่ 0000 + reboot loop (case 69M540494):** false WDT `taskWifiMqtt` ~3.6s บน v3.51; แก้ grace + `hangElapsedMs` — ดู `.cursor/rules/firmware-tm-board-deploy.mdc`

## Deploy บอร์ด (v3.57+)

| OldBoard | บอร์ด | OTA | env |
|----------|-------|-----|-----|
| 0 | ใหม่ TM1637 | `ai_new` | `esp32dev` |
| 1 | เก่า TM1637 | `ai_old` | `esp32dev` |

Build แยก: ตั้ง `#define OldBoard` ใน `varable.h` แล้ว `pio run` ใหม่ทุกครั้ง

**กัน regression:** ดู `.cursor/rules/firmware-tm-regression-guard.mdc` ก่อนแก้ boot/WDT/platformio/OTA

## จุดที่ยังควรจับตา

- รีบูทจาก `pbuf_free: p->ref > 0`
- รีบูทตอน WiFi เพิ่งกลับมาแล้ว MQTT/HTTP ยิงพร้อมกัน
- socket churn จาก reconnect ถี่เกินไป
- publish MQTT บน socket ค้างหลัง WiFi หลุดชั่วคราว (presence + UpdateState ชนกัน)
- ถ้าไฟเลี้ยงตกตอน relay ทำงาน อาจรีบูทได้เหมือนกัน แต่ log จะไม่ใช่ `pbuf_free`
- ถ้า broker/route แกว่งหนักมาก อาจเห็น reconnect ช้าเพราะ backoff ที่เพิ่มไว้
- ถ้า OTA หรือ HTTP ภายนอกยังมีปัญหา ควรดู log ร่วมกับ `WiFi retry backoff` และ `MQTT retry backoff`
- ถ้ายัง crash ซ้ำแม้แก้ boot warmup แล้ว ให้ดูต่อเรื่องไฟเลี้ยงและ noise จาก relay/โหลดจริง
- หลัง v3.41: ถ้าเห็น log `warming up` ค้างนานเกิน ~4s หลัง WiFi ต่อ ให้เก็บ Serial (อาจเป็น regression ใหม่)
- **v3.44 MQTT:** หลัง reconnect ไม่ควร crash `pbuf_free` ตอน presence — deferred publish + pump ครั้งเดียวต่อรอบ
- **Run session (v3.42+):** log `[RunSession]` — grace 10s แล้ว resume; **v3.45:** NVS เฉพาะเมื่อ step/phase เปลี่ยน; **v3.46:** โหมดอบ autosave timer ทุก 10 นาที
- **บอร์ดใหม่ OldBoard 0 — ปุ่ม GPIO 34/35:** input-only ไม่มี internal pull-up; `INPUT_PULLUP` ใน setup ไม่ช่วย — ถ้าปุ่มลอยอาจตั้ง `statedisplaystandby=3` (ยังไม่แก้ pinMode ใน v3.57)

## วิธีทดสอบภาคสนาม

1. เปิดเครื่องให้ต่อ WiFi และ MQTT ตามปกติ
2. เริ่มโปรแกรมซัก/อบ แล้วปิด router ชั่วคราว 10-20 วินาที
3. เปิด router กลับ ดูว่าเครื่องไม่รีบูทและกลับมาต่อเอง
4. ทดสอบตอนมี `pendingBalance` ค้างด้วย
5. ถ้า crash อีก ให้เก็บ log ช่วงก่อนรีบูท 30-60 วินาที
