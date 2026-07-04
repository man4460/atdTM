# เอกสารโปรโตคอล MQTT / การตั้งค่าผ่าน Admin

ใช้เป็น reference สำหรับฝั่ง server หรือแอป admin ในการส่งคำสั่งและตั้งค่าเครื่องซัก/อบ (โปรเจกต์ ATD_TM_V2_New_Hier และ ATD35_Melody_V2)

---

## 1. Topic / การเชื่อมต่อ MQTT

| รายการ | ค่า |
|--------|-----|
| **Client ID** | `Noserial` ของเครื่อง (เช่น `65M090069`) |
| **Topic ที่เครื่อง subscribe** | `V<gid>` เช่น `V7`, `V14` |
| **Broker (mqttStatus=1)** | `mawell.thddns.net` พอร์ต `4741–4744` |
| **Broker (mqttStatus=2)** | `broker.mqtt.cool` พอร์ต `1883` |

ทุกคำสั่งจาก admin ให้ **publish ไปที่ topic `V<gid>`** พร้อม payload JSON ด้านล่าง

---

## 2. รูปแบบ payload พื้นฐาน

```json
{
  "cm": "cmCommand | cmProgram | getdata | setup",
  "id": "<Noserial>",
  "value_str1": "...",
  "value_str2": "..."
}
```

- **cm** = กลุ่มคำสั่ง (`cmCommand`, `cmProgram`, `getdata`, `setup`)
- **id** = ต้องตรงกับ `Noserial` ของเครื่อง
- **value_str2** = action หลัก (ชื่อคำสั่ง)
- **value_str1** = ข้อมูลเพิ่ม (ข้อความหรือ JSON string)

---

## 3. คำสั่งควบคุมเครื่อง (cm = "cmCommand")

ส่งไปที่ `V<gid>`

### 3.1 คำสั่งหลัก

| value_str2 | ความหมาย |
|------------|----------|
| `start` | สั่ง Start (เหมือนกดปุ่มเริ่ม) |
| `power` | Pulse Power |
| `Shutdown` | ปิดเครื่อง (StateShutdown=1, จอดับ) |
| `Restart` | ตั้ง StateShutdown=0 แล้วรีบูต ESP |
| `ESP` | รีบูต ESP ทันที |
| `update` | สั่ง OTA (OTI) |
| `slot` | สลับขาเหรียญ (SIG_PIN / SIG_PIN2) |
| `check` | ขอข้อมูลสถานะ / ส่งรายงาน |
| `Setup` | ตั้ง SetupData=1 แล้วรีบูต |
| `BT1` .. `BT5` | สั่งเทียบเท่ากดปุ่ม 1–5 |
| `Setfirst` | บันทึกค่าปัจจุบันลง Preferences (Noserial, ssid, password, gid) |

### 3.2 ตั้งค่า WiFi ผ่าน MQTT (setWifi)

```json
{
  "cm": "cmCommand",
  "id": "<Noserial>",
  "value_str2": "setWifi",
  "value_str1": "{\"ssid\":\"ShopWifi\",\"password\":\"12345678\"}"
}
```

- ESP จะอัปเดต `ssidStr`, `passStr`, เซฟลง Preferences
- อาจรีสตาร์ตเพื่อใช้ WiFi ใหม่ (ขึ้นกับ implementation)

---

## 4. คำสั่งตั้งค่าโปรแกรม/ราคา (cm = "cmProgram")

### 4.1 ตั้งราคาโปรแกรมพื้นฐาน (setPrice)

```json
{
  "cm": "cmProgram",
  "id": "<Noserial>",
  "value_str2": "setPrice",
  "value_str1": "{\"Price1\":30,\"Price2\":40,\"Price3\":50}"
}
```

### 4.2 รันโปรแกรมโดยตรง (value_str2 = เลขโปรแกรม)

```json
{
  "cm": "cmProgram",
  "id": "<Noserial>",
  "value_str2": "1",
  "value_str1": ""
}
```

- `value_str2` = `"0"` → รีเซ็ตโปรแกรม  
- `"1"`, `"2"`, `"3"` ฯลฯ → เริ่มโปรแกรมนั้น (เมื่อเครื่องพร้อม)

---

## 5. โปรโมชั่นหลายช่วงเวลา/วัน (setPromoSlots)

### 5.1 โครงสร้างฝั่ง ESP (varable.h)

```cpp
struct PromoSlot {
  uint8_t day;       // 0=Sun..6=Sat
  uint8_t startHour; // 0-23
  uint8_t startMin;  // 0-59
  uint8_t endHour;   // 0-23
  uint8_t endMin;    // 0-59
  int pricePro[3];   // ราคาโปรสำหรับโปรแกรม 1,2,3 ในช่วงนี้
};

const int MAX_PROMO_SLOTS = 10;
PromoSlot promoSlots[MAX_PROMO_SLOTS];
int promoSlotCount = 0;
```

### 5.2 รูปแบบ MQTT: setPromoSlots

**ตัวอย่าง:** วันอังคาร (day=2) สองช่วง ราคาโปรต่างกัน

```json
{
  "cm": "cmProgram",
  "id": "65M090069",
  "value_str2": "setPromoSlots",
  "value_str1": "{
    \"slots\": [
      {\"day\":2,\"start\":\"08:00\",\"end\":\"10:00\",\"prices\":[20,30,40]},
      {\"day\":2,\"start\":\"18:00\",\"end\":\"20:00\",\"prices\":[25,35,45]}
    ],
    \"Price1\":30,
    \"Price2\":40,
    \"Price3\":50
  }"
}
```

### 5.3 ความหมายฟิลด์

| ฟิลด์ | ความหมาย |
|-------|----------|
| **slots** | อาร์เรย์ของช่วงโปรโมชั่น (สูงสุด 10 ช่วง) |
| **slots[].day** | 0=อาทิตย์, 1=จันทร์, …, 6=เสาร์ |
| **slots[].start** | เวลาเริ่ม `"HH:MM"` |
| **slots[].end** | เวลาสิ้นสุด `"HH:MM"` |
| **slots[].prices** | `[ราคาโปร1, ราคาโปร2, ราคาโปร3]` สำหรับช่วงนี้ (ถ้าไม่ส่ง ใช้ pricePro หลัก) |
| **Price1, Price2, Price3** | ราคาปกติ (นอกช่วงโปร) และ fallback |

### 5.4 Logic การแสดงราคา (CheckPromotion)

- ทุก 1 วินาที ESP เช็ค `dayNow`, `hourNow`, `minNow`
- ถ้ามี `promoSlotCount > 0`: วนหา slot ที่ `day` ตรงและเวลาอยู่ในช่วง `start–end`
  - **เจอ** → ใช้ `PriceShow[i] = slot.pricePro[i]` (ราคาโปรของช่วงนั้น)
  - **ไม่เจอ** → ใช้ `PriceShow[i] = price[i]` (ราคาปกติ)
- ถ้า `promoSlotCount == 0` → ใช้ logic เดิม (dateState + TimeStart/TimeEnd หนึ่งช่วงต่อวัน)

---

## 6. การกันยอดรายรับเมื่อเน็ต/MQTT หลุด

### 6.1 ตัวแปรที่ใช้

| ตัวแปร | ความหมาย |
|--------|----------|
| `item_price` | ยอดปัจจุบัน (แสดงบนจอ / ใช้ใน step) |
| `pendingBalance` | ยอดสะสมที่ยังส่ง server/MQTT ไม่สำเร็จ |
| `stateSentPriceServer` | flag ขอให้ task WiFi/MQTT พยายามส่ง |

### 6.2 เมื่อมีรายรับ (เริ่มโปรแกรมหรือเพิ่มเวลาอบ)

- เครื่องจะ: `pendingBalance += item_price`, ตั้ง `stateSentPriceServer = true`, เคลียร์ `item_price`
- **ไม่ส่ง HTTP/MQTT ทันที** ถ้าเน็ตหลุด → เก็บใน RAM

### 6.3 การส่งเมื่อออนไลน์ (taskWifiMqtt)

- เมื่อ WiFi + MQTT ต่ออยู่ และ `pendingBalance > 0`:
  1. เรียก **HTTP** `UpdateBalanceV3(pendingBalance)` ไป Logic App
  2. **Publish MQTT** ไป topic `postSQL`:
     ```json
     {"idEsp":"<Noserial>","idUser":"<gid>","idBranch":"<gid>","price":"<pendingBalance>","typePay":"0"}
     ```
  3. ถ้าทั้ง HTTP และ MQTT สำเร็จ → เคลียร์ `pendingBalance` และ `stateSentPriceServer`

---

## 7. Topic ที่เครื่อง Publish ออก

| Topic | ใช้เมื่อ |
|-------|----------|
| **UpdateState** | อัปเดตสถานะเครื่อง + เวลาที่ส่ง (JSON: ID, Title, Status, Time) |
| **postSQL** | ส่งยอดรายรับหลังซัก/อบเสร็จ (JSON: idEsp, idUser, idBranch, price, typePay) |
| **getdataResponse** | ตอบเมื่อรับคำสั่ง getdata จาก V&lt;gid&gt; — ส่ง config ปัจจุบัน (cm, id, value_str2) ให้ server รับไปบันทึก |
| **OtaStatus** | ส่งสถานะ OTA (JSON: id, gid, phase, percent?, message?) — ดู 7.2 |

### 7.1 ส่ง/รับ config ผ่าน MQTT (MelodyWebapp)

- **Server ส่ง getdata** ไปที่ `V<gid>`: `{"cm":"getdata","id":"<Noserial>"}`  
  → เครื่องจะ **Publish** ไป topic **getdataResponse** ด้วย JSON: `{"cm":"getdataResponse","id":"<Noserial>","value_str2":{ ... ตัวแปรทั้งหมด ... }}`  
  → Server subscribe topic **getdataResponse** แล้วนำ value_str2 ไปบันทึก (MachineEspConfig / Machine)

- **Server ส่ง setup** ไปที่ `V<gid>`: `{"cm":"setup","id":"<Noserial>","value_str2":{ ... }}`  
  → เครื่องรับ payload จาก MQTT โดยตรง (ไม่เรียก HTTP) แปลง value_str2 แล้ว **บันทึกลง Preferences** (writePreferences)

### 7.2 สถานะ OTA (topic OtaStatus)

เครื่อง Publish ไป topic **OtaStatus** เพื่อให้ server/แอดมินรู้ว่ากำลังอัพเดท กี่เปอร์เซ็นต์ และเสร็จหรือล้มเหลว

- **Payload (JSON):** `id` (Noserial), `gid`, `phase`, `percent` (0–100, optional), `message` (optional)
- **phase:** `start` = เริ่มอัพเดท, `progress` = กำลังดาวน์โหลด (มี percent), `success` = อัพเดทเสร็จ, `failed` = อัพเดทไม่สำเร็จ (มี message)
- **MelodyWebapp:** Server subscribe OtaStatus → อัปเดต Machine.otaStatus, otaProgress, otaMessage, otaUpdatedAt → แดชบอร์ดแสดง "กำลังอัพเดท X%", "อัพเดทเสร็จ", "อัพเดทไม่สำเร็จ"

### 7.3 รหัสปัญหาเครื่อง 00 / 01 / 02 (UpdateState)

เมื่อเครื่องตรวจพบปัญหา ESP จะตั้ง `StatusControl` แล้วเรียก `reportEspFaultToMelody()` → `stateUpdateState = 1` → ส่งออกทาง **UpdateState** (หรือ HTTP fallback)

| Status | ความหมาย | จุดที่ตั้งใน firmware |
|--------|----------|----------------------|
| **00** | เครื่องมีปัญหา (ไม่มีไฟ / เปิดไม่ได้) | ตรวจ LDR หลัง Power ไม่ผ่าน, เริ่มทำงานไม่ได้ (Mode≠1) |
| **01** | ไม่สามารถปั่นได้ | นับเวลาผ่านไป 15 นาทีแต่ปั่นไม่ถึงความเร็ว |
| **02** | ประตูเปิด | ตรวจไฟ/LDR หลัง Start ไม่ผ่าน 5 ครั้ง (ประตูเปิด) |

**Payload MQTT (topic `UpdateState` หรือ `Upstatus`):**

```json
{
  "ID": "94",
  "Title": "65M000000",
  "Status": "00",
  "Time": "00:00"
}
```

- **Title** = `Noserial` / controllerId (Melody ค้นหาเครื่องจากฟิลด์นี้)
- **Status** = `00` | `01` | `02` (ไม่ถูกแทนด้วย `1on`/`2on` — `SetStatusControl()` คงค่าไว้)
- **Time** = เวลาที่เหลือจาก `SetTimerSend()` (มักเป็น `00:00` ตอน error)

**HTTP fallback** (MQTT ล่มเกิน 5 ครั้ง) — POST ไป Melody:

`POST /public/machines/update-state`

```json
{
  "api_key": "<MELODY_HTTP_API_KEY>",
  "controller_id": "65M000000",
  "ID": "94",
  "Status": "02",
  "Time": "00:00"
}
```

**MelodyWebapp รับแล้วทำอะไร:**

1. Subscribe `UpdateState` (+ `Upstatus` ถ้าตั้งใน `.env`)
2. `applyUpdateStateData()` เห็น Status = 00/01/02
3. อัปเดต DB: `status = maintenance`, `espFaultCode = "00"|"01"|"02"`
4. ส่ง SSE → แดชบอร์ดแสดงป้าย "เครื่องมีปัญหา" / "ไม่สามารถปั่นได้" / "ประตูเปิด"

**Serial log บน ESP:**

```
[Melody] fault -> UpdateState Status=02
update status and time ..!! :: 02 :: 00:00
```

---

## 8. ตั้งค่า WiFi ผ่าน SoftAP (ไม่ใช้ MQTT)

- **กดปุ่ม RESET ค้าง ~10 วินาที** (เครื่อง standby, ไม่กำลังซัก/อบ)
- เครื่องเปิด Hotspot: **SSID = `ESP-Setup`**, **Password = `12345678`**
- เชื่อมมือถือ → เปิดเบราว์เซอร์ไปที่ **`http://192.168.4.1/`**
- กรอก SSID + Password → กดบันทึก → เครื่องจะเซฟและรีสตาร์ต

---

*เอกสารนี้ครอบคลุมการปรับปรุงที่ทำในโปรเจกต์ ATD_TM_V2_New_Hier และสามารถใช้อ้างอิงใน ATD35_Melody_V2 (จอ touch) ได้*
