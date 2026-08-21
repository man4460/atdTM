# LDR — OldBoard 1 + CM3 (บันทึกวิเคราะห์ 2026-08-12)

อัปเดตล่าสุด: **v4.20** implement โปรไฟล์ Mode 1 (2026-08-21)

## สถานะ implement v4.20

- [x] `LdrLightProfile` + learn + `checkLightWithProfile` (`ldr_sampler.h`)
- [x] Mode 1 `checkLightStart` ใช้โปรไฟล์เมื่อ `valid`
- [x] เมนู LP1/2/3 + NVS (TM + ATD35)
- [ ] ทดสอบบนเครื่องจริง

---

เปรียบเทียบ: **ATD_TM_V3_New_Hier** vs **ATD_TM_V1_New_Hier** (`D:\ESP32\ATD_TM_V1_New_Hier\ATD_TM_V1_New_Hier`)

เครื่องเป้าหมาย: **OldBoard 1** (บอร์ดเก่า) + **CodeMachine 3** (LG24KgBlack)  
หมายเหตุ: **ไม่มี logic LDR แยกเฉพาะ CM3** — ใช้ path บอร์ดเก่าทั่วไป (ต่างจาก CM4 ที่มี path แยกเฉพาะ `OldBoard == 0`)

---

## ค่าพื้นฐาน (V1 = V3 ตรงกัน)

| ค่า | ค่า |
|-----|-----|
| `ldr_set` | **1500** |
| `ldrMinus` | **500** |
| LDR pin | GPIO **34** / **35** |
| polarity บอร์ดเก่า | **สูง = สว่าง** (`> ldr_set`) |
| มืดชัด V1 | **`< 1000`** (`ldr_set - ldrMinus`) |

---

## สรุปปัญหา V3 ไม่เสถียรเท่า V1

### 1. เกณฑ์มืดผิด (กระทบมากสุด)

V3 ใช้ `darkMargin = ldrMinus/2` → มืด = **`< 1250`** แทน **`< 1000`**

- โซน **1250–1500** กลายเป็น **gray / unstable** → fault **02** ง่าย
- Power check ใช้ `darkLo = 1250` เหมือนกัน (`main.cpp` case 2)

**แนวแก้:** OldBoard 1 ใช้ `dark = val < ldr_set - ldrMinus` (1000) — **ไม่มี gray zone** หรือ gray ไม่ fail เอง

### 2. วิธีอ่านต่างจาก V1

| | V1 | V3 |
|--|----|----|
| เช็ค 02 | เฉลี่ย 10 ครั้งติด ~100ms | median 10 ตัว + รอ **700ms** รอบถัดไป |
| Power | เฉลี่ย 10 ครั้ง | `analogRead` **ครั้งเดียว** |
| จบรอบ | เฉลี่ย 10 ครั้ง | median กระจาย 700ms |

**แนวแก้:** OldBoard 1 ใช้ `readLDRAverage(pin, 10)` แบบ blocking (~เหมือน V1) ใน `checkLightStart` และ power

### 3. Timeout + เงื่อนไข fail เพิ่มใน V3

| | V1 | V3 |
|--|----|----|
| timeout เช็ค 02 | **5 วิ** | **3 วิ** |
| fail เพิ่ม | ไม่มี | `sawGray`, `countStateLight≥2` → unstable |

**แนวแก้:** OldBoard 1 → timeout **5000ms**; ลบ penalty gray / countStateLight≥2 ตอน timeout

### 4. จบรอบ (step 3)

เกณฑ์ OldBoard 1 ใน V3 **ถูกแล้ว**: `< 1000` ค้าง 3 วิ, รีเซ็ตเมื่อ `> 1500` — ตรง V1  
ถ้ายังไม่นิ่ง → เปลี่ยนวิธีอ่านเป็น `readLDRAverage(10)` (ทางเลือก)

### 5. ค้าง 0:01 (CM3)

| | V1 | V3 |
|--|----|----|
| นาทีที่ 5 | บังคับ Start+Power (CM3/4) | **ไม่บังคับ** — รอ LDR จริง |

ไม่ใช่สาเหตุหลัก fault 02 แต่พฤติกรรมท้ายรอบต่างกัน

### 6. โหมดทดสอบ

V1: เฉลี่ย 10 ครั้ง | V3: อ่านดิบ 100ms — ทางเลือกให้ OldBoard 1 กลับเฉลี่ย 10

---

## แผนปรับที่เสนอ (เฉพาะ `OldBoard == 1`)

| ลำดับ | จุด | ไฟล์ | การแก้ |
|------|-----|------|--------|
| 1 | เช็ค 02 fault | `main.cpp` `checkLightStart` | แยก branch OldBoard 1: logic แบบ V1 (เฉลี่ย 10, มืด `<1000`, timeout 5s, blink = On≥5 + Off≥5) |
| 2 | Power case 2 | `main.cpp` | `readLDRAverage(10)`, มืด `<1000` |
| 3 | จบรอบ step 3 | `main.cpp` | (ถ้าจำเป็น) `readLDRAverage(10)` |
| 4 | โหมดทดสอบ | `checkLdr1` | OldBoard 1 → เฉลี่ย 10 ครั้ง |
| — | **ไม่แตะ** | CM4 path (`OldBoard==0`), `cm4_blink_th`, บอร์ดใหม่ | คงเดิม |

---

## Logic เช็ค 02 เป้าหมาย (OldBoard 1 — อ้าง V1)

```
ทุกรอบ (~100ms):
  val = เฉลี่ย analogRead 10 ครั้ง

  if val > 1500 && กำลังมืด → นับ On (edge)
  if val < 1000 && กำลังสว่าง → นับ Off (edge)
  if On≥5 && Off≥5 → BLINK → result 1 (fault 02)

  if val > 1500 ติด 20 รอบ → ON → result 2
  if 5 วิ && val < 1000 → OFF → result 0

  ค่า 1000–1500 → รอต่อ (ไม่ fail)
```

---

## สิ่งที่ไม่ควรทำ (บอร์ดเก่า)

- อย่าใช้ logic CM4 (amplitude / `cm4_blink_th`) กับ OldBoard 1
- อย่าใช้ `ldrMinus/2` เป็นเกณฑ์มืดบน OldBoard 1
- อย่า timeout เช็ค 02 ต่ำกว่า 5 วิ
- อย่าใช้ `analogRead` ครั้งเดียวตัดสิน power/start

---

## บริบท CM4 / บอร์ดใหม่ (คนละเรื่อง)

- CM4 path แยก: `CodeMachine == 4 && OldBoard == 0` เท่านั้น
- บอร์ดใหม่: `ldr_set=3000`, polarity ต่ำ=สว่าง
- `cm4_blink_th` (admin ปรับได้) — **ไม่เกี่ยว OldBoard 1 + CM3**

ดู CHANGELOG v4.15–v4.19 สำหรับ CM4 บอร์ดใหม่

---

## Checklist ทดสอบหลังแก้

1. โหมดทดสอบ LDR — ไฟติดค้าง ค่า ~>1500 นิ่ง
2. Start — ไฟติดค้าง → ผ่าน ไม่ fault 02
3. Start — ไฟกระพริบ → fault 02 ภายใน 5 รอบ
4. จบรอบ — ไฟดับ `<1000` ค้าง 3 วิ → จบปกติ
5. Power — ไฟสว่าง `>1500` → ผ่าน

---

## อ้างอิงโค้ด

- V3 `checkLightStart` generic: `src/main.cpp` ~2755 (darkMargin, gray, 3s timeout)
- V3 power OldBoard 1: `src/main.cpp` case 2 ~3597 (`darkLo = ldr_set - ldrMinus/2`)
- V1 `checkLightStart` OldBoard 1: `ATD_TM_V1_New_Hier/src/main.cpp` ~1248
- V3 sampler: `src/ldr_sampler.h` (`LDR_READ_INTERVAL_MS=700`, `readLDRAverage`)

---

## สถานะ (แผนเดิม V1-style — ค้างไว้)

- [x] ยืนยันแผนกับ user (2026-08-12)
- [ ] implement ข้อ 1+2 (เช็ค 02 + power) — **ระงับ** รอแผนโปรไฟล์ Mode 1 ด้านล่าง
- [ ] ทดสอบบนเครื่อง OldBoard 1 + CM3 จริง
- [ ] bump version + CHANGELOG (+ ATD35 ถ้าแตะ shared logic)

---

# Mode 1 — โปรไฟล์ LDR (ยืนยัน 2026-08-21)

แนวคิดที่ user confirm: เก็บโปรไฟล์กระพริบ/ติดค้าง/ดับ แล้ว `checkLightStart` อ่านตามคาบกระพริบ

**สถานะ implement:** รอคำสั่ง "แก้เลย" — มีแค่ดีไซน์ด้านล่าง

## ตัวแปร (เสนอ)

```
struct LdrLightProfile {
  bool valid;           // เรียนรู้อย่างน้อย BLINK แล้ว
  uint16_t periodMs;    // เวลาสว่างสุด → มืดสุด (ms)
  int brightLevel;      // ระดับสว่างตอนกระพริบ (ADC)
  int darkLevel;        // ระดับมืดตอนกระพริบ (ADC)
  int onLevel;          // ไฟติดค้าง
  int offLevel;         // ไฟดับ
};
```

NVS (namespace `config`, เก็บ /100 สำหรับ level ถ้าต้องการสั้นเหมือน ldr_set):
- `lpValid`, `lpPeriod`, `lpBright`, `lpDark`, `lpOn`, `lpOff`

Default ก่อนเรียนรู้: `valid=false` → `checkLightStart` ใช้ logic เดิม (fallback)

## เมนูเรียนรู้ (เสนอ)

โหมดทดสอบ LDR หรือเมนูแอดมิน 3 ขั้น:
1. **บันทึก BLINK** — ไฟกระพริบอยู่ → อ่านถี่ 50–100ms นาน ~3–5 วิ → หา peak/trough + periodMs (median หลายรอบ)
2. **บันทึก ON** — ไฟติดค้าง → median ~2 วิ → `onLevel`
3. **บันทึก OFF** — ไฟดับ → median ~2 วิ → `offLevel`

แล้ว Save ลง NVS

## `checkLightStart` เมื่อ Mode==1 และ profile.valid

1. `sampleGap = max(50, periodMs/4)`
2. หน้าต่างยาว `2–3 × periodMs` (clamp 1500–6000ms)
3. นับครั้งที่ค่าใกล้ `darkLevel` (tolerance เช่น ±15% ของ span หรือ ±200 ADC)
4. ถ้า darkHits ≥ 2 ในหน้าต่าง → result **1 BLINK**
5. ถ้านิ่งใกล้ `onLevel` → result **2 ON**
6. ถ้านิ่งใกล้ `offLevel` → result **0 OFF**
7. ไม่ชัด → result **1** (unstable) หรือรอ timeout ตามเดิม

## ขอบเขตรอบแรก

- Mode 1 เท่านั้น
- ไม่ลบ CM4 path / `cm4_blink_th` ในรอบแรก (ใช้คู่กับ fallback)
- กรอง glitch `<35` ก่อนวัด period
- polarity: แปลงเป็น bright/dark ตาม OldBoard ก่อนเก็บ

## Checklist หลัง implement

1. เรียนรู้ 3 สถานะ → NVS คงหลังรีบูต
2. กระพริบ → 02
3. ติดค้าง → ผ่าน
4. ดับ → off
5. ยังไม่เรียนรู้ → พฤติกรรมเดิม
