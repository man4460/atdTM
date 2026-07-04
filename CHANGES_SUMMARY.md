# สรุปการแก้ไขที่ทำใน ATD_TM_V2_New_Hier

ใช้เป็น checklist เวลาปรับแก้ในโปรเจกต์ **ATD35_Melody_V2** ให้เหมือนกัน

---

## 1. ลบโค้ดวันเปิดปิดร้านเดิม (dateState, TimeStart, TimeEnd)

โปรโมชั่นใช้ **promoSlots** (หลายช่วง/หลายวัน) แทนแล้ว ไม่ใช้ dateState + TimeStart/TimeEnd อีก

### 1.1 ไฟล์ `varable.h`
- **ลบ** ตัวแปร:
  - `int dateState[] = {0,0,0,0,0,0,0};`
  - `int TimeStart[] = {0, 0};`
  - `int TimeEnd[] = {0, 0};`

### 1.2 ไฟล์ `main.cpp`

- **writePreferences()**  
  ลบ `preferences.putInt(...)` ของ `TimeStart[0],[1]`, `TimeEnd[0],[1]`, `dateState[0]..[6]`

- **readPreferences()**  
  ลบ `preferences.getInt(...)` ของ key เดียวกัน

- **GetSetupData() (ส่วน parse จาก HTTP/JSON)**  
  ลบการ assign `dateState`, `TimeStart`, `TimeEnd` จาก `value_str2_buf` และลบ Serial.printf ของ Days / Time Start|End (และ comment ที่อ้าง `dateState`)

- **PublishConfigViaMqtt()**  
  ลบ `v2["Sun"]`..`v2["Sat"]`, `v2["TimeStart"]`, `v2["TimeEnd"]` และตัวแปร `timeStartBuf`/`timeEndBuf`

- **sentVarjson()**  
  ลบ `JsonArray TimeStartArr`, `TimeEndArr`, `dateStateArr` และ loop ที่ add ค่า

- **CheckPromotion()**  
  ลบทั้งบล็อก `else` โหมดเก่า (ใช้ dateState + TimeStart/TimeEnd ต่อวัน)  
  เหลือเฉพาะ: ถ้า `promoSlotCount > 0` ใช้ promoSlots ถ้าไม่ใช้ `PriceShow[i] = price[i]`

---

## 2. โปรโมชั่นหลายช่วง (promoSlots) — บันทึก/โหลดใน Preferences

### 2.1 ไฟล์ `varable.h`
- มี struct `PromoSlot` (day, startHour, startMin, endHour, endMin, pricePro[3])
- มี `PromoSlot promoSlots[MAX_PROMO_SLOTS]`, `int promoSlotCount = 0` (และ `MAX_PROMO_SLOTS = 10`)

### 2.2 ไฟล์ `main.cpp`
- **writePreferences()**  
  บันทึก `psCnt` และต่อ slot: `ps0d`, `ps0sH`, `ps0sm`, `ps0eH`, `ps0em`, `ps0p0`, `ps0p1`, `ps0p2` … (วนตาม promoSlotCount)

- **readPreferences()**  
  อ่าน `psCnt` แล้ว loop อ่าน key แบบเดียวกันมาใส่ `promoSlots[]`

- **MQTT callback / รับ setPromoSlots**  
  มีการ parse payload ชื่อ setPromoSlots (array ของ day, start, end, pricePro) แล้วอัปเดต `promoSlots[]` และ `promoSlotCount` แล้ว `writePreferences()`

- **CheckPromotion()**  
  ใช้ `promoSlotCount` + `promoSlots[]` ตรวจวัน/เวลาและตั้ง `PriceShow[i]` ตาม active slot หรือใช้ `price[i]`

---

## 3. ดึง config จากระบบปัจจุบันแบบ MQTT (ไม่มี HTTP แบบเก่า)

### 3.1 ไฟล์ `varable.h`
- **ลบ** `ServerSetupdataV3` และ `ServerGetdataV3` (ไม่ใช้ดึง config แล้ว)
- **เหลือ** comment: ดึง config ใช้ MQTT เท่านั้น ถ้าไม่มีข้อมูลใช้ค่าจากโรงงาน
- **เหลือ** `ServerSentBalanceV3` (ยังใช้ส่งยอดเงิน)

### 3.2 ไฟล์ `main.cpp`

- **MQTT callback**  
  - ถ้า topic เป็น `"configResponse/" + Noserial` → ใส่ payload ลง `mqttPayloadBuffer` และตั้ง `stateSetupdata = true` (เหมือนรับ setup)

- **mqttreconnect() (หลัง connect สำเร็จ)**  
  - เรียก `mqclient.setBufferSize(2048)` ก่อน connect (เพื่อรองรับ getdataResponse โต)
  - subscribe เพิ่ม: `"configResponse/" + Noserial`

- **GetData()**  
  - ถ้า WiFi ไม่ต่อ → return
  - ถ้า MQTT ต่ออยู่: publish ไป topic `configRequest` ด้วย `{"controllerId":"<Noserial>"}` แล้วรอรับที่ `configResponse/<Noserial>` สูงสุด 8 วินาที (loop เรียก `mqclient.loop()`, ดู `stateSetupdata`)
  - ถ้าได้ config (`stateSetupdata`) → return (ให้ GetSetupData() บันทึก)
  - **ไม่มีการเรียก HTTP เก่า**  
  - ถ้าไม่ต่อ MQTT หรือ timeout → เรียก **applyFactoryDefaultsConfig()**

- **GetSetupData()**  
  - ถ้ามี `mqttPayloadBuffer` (รับจาก MQTT รวมทั้ง configResponse): parse แบบ setup (id, gid, program, drum, price, …) แล้ว `writePreferences()` ตามเดิม
  - **ลบ** ทั้งบล็อก HTTP ไปที่ ServerSetupdataV3
  - ถ้าไม่มี MQTT payload → เรียก **applyFactoryDefaultsConfig()**

- **applyFactoryDefaultsConfig() (ฟังก์ชันใหม่)**  
  - ตั้งค่าตัวแปร config เป็นค่าจากโรงงาน (เทียบ varable.h): price, pricePro, timerDry, program1/2/3, rinStep2, rincommand, spin, drum, check_runing_time, TimeCountdowndrum, TimeCountdown1/2/3, promoSlotCount=0, mqttStatus=1, Mode=1, CodeMachine=1
  - เรียก `setRelayType()`, `writePreferences()`, `setPriceShow()` และ Serial.println ว่าใช้ค่าจากโรงงาน

---

## 4. อื่นๆ ที่เกี่ยวข้อง

- **getdata (แอดมินถาม)**  
  - เมื่อรับ MQTT getdata และ id ตรง → ตั้ง `stateSendConfigMqtt = true` และ `getdataDisplayUntil = millis() + 2500` เพื่อแสดง "PC" บนจอ
  - ส่ง config กลับทาง topic getdataResponse (payload ใหญ่ จึงต้อง setBufferSize(2048))

- **Backend (MelodyWebapp)**  
  - Subscribe topic `configRequest` รับ `{"controllerId":"..."}`  
  - ถ้า DB มี config (getDeviceBootstrap) → publish ไปที่ `configResponse/<controllerId>` เป็น `{ cm: "setup", id, value_str2 }`  
  - มี endpoint GET `/public/device-bootstrap/:controllerId` (สำหรับ web/อื่น ไม่ได้ใช้จาก ESP แล้ว)

---

## 5. Checklist สำหรับนำไปใช้ใน ATD35_Melody_V2

- [ ] varable.h: ลบ dateState, TimeStart, TimeEnd
- [ ] varable.h: ลบ ServerSetupdataV3, ServerGetdataV3 (เหลือ comment + ServerSentBalanceV3)
- [ ] main: writePreferences – ลบ TimeStart/TimeEnd/dateState, เพิ่มบันทึก promoSlots (psCnt, ps*d, ps*sH, …)
- [ ] main: readPreferences – ลบ TimeStart/TimeEnd/dateState, อ่าน promoSlots
- [ ] main: GetSetupData – ลบการ assign dateState/TimeStart/TimeEnd จาก JSON; ลบทั้งบล็อก HTTP ไป ServerSetupdataV3
- [ ] main: PublishConfigViaMqtt – ลบ Sun..Sat, TimeStart, TimeEnd
- [ ] main: sentVarjson – ลบ TimeStartArr, TimeEndArr, dateStateArr
- [ ] main: CheckPromotion – ลบโหมดเก่า dateState/TimeStart/TimeEnd เหลือ promoSlots + fallback price[]
- [ ] main: callback – เพิ่มรับ topic "configResponse/"+Noserial → mqttPayloadBuffer + stateSetupdata
- [ ] main: mqttreconnect – setBufferSize(2048), subscribe "configResponse/"+Noserial
- [ ] main: GetData – ใช้เฉพาะ MQTT (configRequest + รอ configResponse), ไม่มี HTTP; timeout/ไม่ต่อ → applyFactoryDefaultsConfig()
- [ ] main: GetSetupData – ไม่มี HTTP; ไม่มี MQTT payload → applyFactoryDefaultsConfig()
- [ ] main: เพิ่มฟังก์ชัน applyFactoryDefaultsConfig()
- [ ] main: รับ MQTT setPromoSlots แล้วอัปเดต promoSlots + writePreferences (ถ้าโปรเจกต์มีโครงนี้อยู่แล้วให้เช็กให้ตรง)

---

## 6. LDR sampler + ประวัติ firmware (2026-06-14)

- เพิ่ม `src/ldr_sampler.h` — อ่าน LDR แบบเฉลี่ย non-blocking (ดูรายละเอียดใน **CHANGELOG.md**)
- ATD_TM (บอร์ดเก่า/ใหม่): **Version 3.35**
- ATD35 S3 (touch): **Version 3.34** — เลขเวอร์ชันเดียวกับ ATD_TM, แยกโฟลเดอร์ OTA (`ai_touch`)
- Boot log Serial: `[FW] Current Firmware` / `[FW] Version X.YY`

---

## 8. Mode 1 LDR instant read (2026-06-18) — v3.35

- `taskProgram` case 2: Mode 1 ใช้ `readLDRInstant()` แทนเฉลี่ย — จับไฟเครื่องกระพริบหลัง `Power()`

## 7. OTA / MQTT fallback (2026-06-18) — v3.34 (ทั้ง ATD_TM และ ATD35)

- `MQTT_FAIL_STREAK_FALLBACK` 20 ครั้งก่อน HTTP poll / UpdateState fallback
- `ensureMqttForOtaStatus()` ก่อนส่ง `OtaStatus`
- เวอร์ชัน firmware ตรงกัน → ส่ง `failed` ไม่ค้าง updating
- ดูรายละเอียด backend ใน **CHANGELOG.md** และ MelodyWebapp migration `httpOtaEnabled`

ถ้า ATD35_Melody_V3 มีโครงเดิมใกล้เคียง ATD_TM_V3_New_Hier แนะนำให้ diff ระหว่างสองโปรเจกต์ที่โฟลเดอร์ `src` (varable.h, main.cpp) แล้ว apply ตามรายการด้านบนจะเร็วที่สุด
