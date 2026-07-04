# ขั้นตอนเมื่อ ESP32 เปิดครั้งแรก — ดึงข้อมูลจากไหน

## สรุปสั้นๆ

- **เปิดครั้งแรก:** อ่านค่าจาก **NVS (Preferences)** ในตัว ESP32 ก่อน → ต่อ WiFi/MQTT → ส่งคำขอ **configRequest** ทาง MQTT → **Backend (MelodyWebapp)** หาเครื่องจาก `controllerId` แล้วส่ง config กลับที่ **configResponse/<controllerId>** → ESP32 รับแล้วบันทึกลง NVS อีกครั้ง
- **แหล่งข้อมูลสุดท้าย:** config ที่ใช้จริง = จาก **Backend ทาง MQTT** (ตาราง `machines` + `machine_esp_config`) ถ้าส่งกลับมาทันใน 8 วินาที มิฉะนั้น ESP32 ใช้ **ค่าจากโรงงาน (factory defaults)**

---

## ขั้นตอนละเอียด (ในโค้ด)

### 1. `setup()` — เริ่มต้น

- เปิด NVS (Preferences) ชื่อ `"config"`
- ถ้ายังไม่มี key `"SetupData"` หรือ `"Noserial"` → บันทึกค่าเริ่มต้น (`writePreferences` / `writePreferencesfirst`)
- **คืนค่าโรงงาน (ถ้ากดปุ่ม SETTING ใน 3 วินาที):** `setupWaitAdminRestoreFactory()` → เขียนค่า default ลง NVS
- **อ่านจาก NVS (แหล่งที่ 1 — ค่าที่เคยบันทึกในเครื่อง):**
  - `readPreferencesfirst()` → อ่าน `Noserial`, `ssid`, `pass`, `gid`, `IDserver` ฯลฯ
  - `readPreferences()` → อ่าน `Mode`, `CodeMachine`, `price[]`, `program1/2/3`, เวลาโปรแกรม ฯลฯ
- ตั้ง `topic = "V" + gid` (ใช้ subscribe MQTT)
- ถ้าเปิด WiFi: `WiFi_ini()` → ต่อ WiFi
- สร้าง tasks: `taskDisplay`, `taskProgram`, `taskWifiMqtt`
- ตั้ง **`firstGetdata = true`** (ให้ task WiFi/MQTT ไปดึง config จากระบบเมื่อพร้อม)

### 2. `taskWifiMqtt` — เมื่อ WiFi เชื่อมแล้ว

- ถ้า **`stateGetdata` หรือ `firstGetdata`** เป็น true:
  - ตั้ง `firstGetdata = false`, `stateGetdata = false`
  - เรียก **`GetData()`**

### 3. `GetData()` — ขอดึง config จากระบบ

- ตรวจว่า WiFi เชื่อมแล้ว
- ถ้า **MQTT เชื่อมแล้ว:**
  - Publish ไป topic **`configRequest`** ด้วย payload:
    ```json
    { "controllerId": "<Noserial>" }
    ```
  - รอสูงสุด **8 วินาที** โดยวน `mqclient.loop()` และ `delay(50)` ทุกครั้ง
  - ถ้าได้รับข้อความที่ topic **`configResponse/<Noserial>`** → MQTT callback จะตั้ง `mqttPayloadBuffer = message` และ **`stateSetupdata = true`**
  - เมื่อเห็น `stateSetupdata == true` → ถือว่าได้ config จากระบบแล้ว → return
- ถ้า **รอ 8 วินาทีไม่ได้รับ configResponse** (หรือ MQTT ไม่ต่อ):
  - เรียก **`applyFactoryDefaultsConfig()`** → ใช้ค่าจากโรงงาน (รวม `CodeMachine = 1`) แล้วเขียนลง NVS

### 4. MQTT callback (เมื่อมีข้อความเข้า)

- Topic **`configResponse/<Noserial>`** (ตอบจาก Backend หลัง configRequest):
  - เก็บ payload ลง **`mqttPayloadBuffer`**
  - ตั้ง **`stateSetupdata = true`**
- Topic **`V<gid>`** (เช่น V0, V1):
  - ถ้า `cm == "setup"` และ `id == Noserial` → เก็บ payload ลง `mqttPayloadBuffer` และตั้ง `stateSetupdata = true`
  - ถ้า `cm == "getdata"` → ตั้ง `stateSendConfigMqtt = true` (ส่ง config กลับไปที่ getdataResponse)

### 5. รอบถัดไปของ `taskWifiMqtt`

- ถ้า **`stateSetupdata == true`**:
  - ตั้ง `stateSetupdata = false`
  - เรียก **`GetSetupData()`**

### 6. `GetSetupData()` — นำ config ที่ได้ไปบันทึก

- ถ้า **`mqttPayloadBuffer`** ไม่ว่าง:
  - แปลง JSON ตรวจว่า `id == Noserial` และ `cm == "setup"`
  - อ่านจาก **`value_str2`** แล้วอัปเดตตัวแปรในเครื่อง เช่น:
    - `CodeMachine`, `Mode` (ModeSystem), `gid`, ราคา, เวลาโปรแกรม, ฯลฯ
  - เรียก **`writePreferences()`** → บันทึกลง NVS (รวม `CodeMachine` ผ่าน `writePreferences`)
- ถ้าไม่มี payload จาก MQTT:
  - เรียก **`applyFactoryDefaultsConfig()`** → ใช้ค่าจากโรงงาน

---

## แหล่งข้อมูลที่ Backend ใช้ตอบ configRequest

- **MelodyWebapp (Backend):** เมื่อได้รับข้อความที่ topic **`configRequest`** จะเรียก **`getDeviceBootstrap(controllerId)`** แล้วส่ง **configResponse** และถ้ามีโปรโมชั่นจะส่ง **setPromoSlots** ตามลำดับ

### กฎการเติม value_str2 (configResponse)

1. **จากตาราง Machine เสมอ:** ราคา (Price1/2/3), เวลาอบ (timedry1/2/3), โหมด (ModeSystem), **CodeMachine** — ใช้จาก `machines.price1/2/3`, `duration1/2/3`, `mode`, `machines.codeMachine` (ถ้าไม่มีคอลัมน์ใช้จาก `machine_esp_config.config`)
2. **โปรโมชั่น:** ค้นตาราง **Promotion** ของเครื่องนี้  
   - **ถ้ามี** → หลังส่ง configResponse แล้ว ส่งข้อความ **setPromoSlots** ไปที่ topic **V&lt;gid&gt;** (เช่น V0, V1) (cmProgram + value_str2 = "setPromoSlots" + value_str1 = JSON ช่วงเวลา/ราคาโปร) ให้เครื่องตั้งโปรโมชั่น  
   - **ถ้าไม่มี** → ไม่ส่ง setPromoSlots เครื่องทำงานโหมดปกติ (ไม่มีช่วงโปรโมชั่น)
3. **ค่าอื่นๆ** (program, timing, mqttStatus, coinValue ฯลฯ) ที่ไม่มีใน DB = **ค่าจากโรงงาน** (default จาก ESP_VARIABLE_DEFINITIONS)

### ลำดับการส่ง

1. Publish ไป **`configResponse/<controllerId>`** เป็น JSON `{ "cm": "setup", "id": "<controllerId>", "value_str2": { ... } }`
2. ถ้ามีโปรโมชั่นของเครื่องนี้ → Publish ไป **`V<gid>`** ข้อความ **setPromoSlots** (cmProgram) เพื่อให้ ESP ตั้งช่วงโปรโมชั่น

### การค้นหาเครื่องใน DB (configRequest)

- Backend ใช้ค่าที่ ESP ส่งมาใน payload เป็น **controllerId** (ตรงกับ **Noserial** ของเครื่อง)
- **ลำดับการค้นหา:**
  1. หาเครื่องที่ **machines.controllerId** = ค่าที่ส่งมา (ต้องตั้ง "Controller ID" ในเว็บให้ตรงกับ Noserial)
  2. ถ้าไม่พบ → หาเครื่องที่ **machines.code** = ค่าที่ส่งมา (ถ้าตั้งรหัสเครื่อง = Noserial จะได้เหมือนกัน)
- ถ้าไม่พบทั้งสองแบบ → Backend ไม่ส่ง configResponse → ESP แสดง "ใช้ค่าจากโรงงาน (ไม่มี config จากระบบ)"

### กรณีไม่มีข้อมูลใน DB

- ถ้า **ไม่พบเครื่อง** จาก controllerId และจาก code → Backend **ไม่ส่ง** configResponse
- ESP32 รอ 8 วินาทีไม่ได้รับ → เรียก **`applyFactoryDefaultsConfig()`** = ใช้ค่าจากโรงงานทั้งหมด

---

## สรุปแหล่งที่มา (เรียงตามลำดับ)

| ลำดับ | แหล่งที่มา | เมื่อไหร่ |
|-------|-------------|-----------|
| 1 | **NVS (Preferences)** ใน ESP32 | อ่านใน `setup()` จาก `readPreferencesfirst()` + `readPreferences()` (ค่าที่เคยบันทึกจากครั้งก่อน) |
| 2 | **MQTT configResponse** จาก Backend | หลัง publish ไป `configRequest` แล้ว Backend ส่งกลับที่ `configResponse/<Noserial>` ภายใน 8 วินาที |
| 3 | **ค่าจากโรงงาน (factory)** | ถ้าไม่ได้รับ configResponse ภายใน 8 วินาที (ไม่มีเครื่องใน DB หรือ timeout/MQTT ไม่ต่อ) → `applyFactoryDefaultsConfig()` — ราคา/เวลาอบ/mode/CodeMachine และค่าอื่นเป็นค่าจากโรงงาน โหมดปกติ ไม่มีโปรโมชั่น |

---

## หมายเหตุ

- **CodeMachine** ใน Backend ตอนตอบ configRequest ใช้ **`machines.codeMachine`** เป็นหลัก (ถ้า null ถึงใช้จาก `machine_esp_config.config` หรือ default 1)
- ESP32 อ่าน **CodeMachine** จาก `value_str2["CodeMachine"]` ใน `GetSetupData()` แล้วบันทึกลง NVS ผ่าน `writePreferences()`
