# ESP Firmware Changelog — ATD_TM_V3_New_Hier

รูปแบบเวอร์ชัน: `Version X.YY` ใน `varable.h` → `fwversion[1]` (OTA / presence MQTT)

**กฎทีม:** ทุกครั้งที่แก้โค้ด → bump เวอร์ชัน **คู่กับ ATD35_Melody_V3** (เลขเดียวกัน) + เขียนหัวข้อใหม่ด้านบนทั้งสองโปรเจกต์ + ใส่ `### Rollback` (ดู `.cursor/rules/firmware-version-rollback.mdc` และ `firmware-tm-atd35-sync.mdc`)

---

## Version 4.34 (2026-10-01) — จบรอบ: LDR เฉลี่ย 1 วิ + มืดติดกัน 3 ครั้ง

### เช็ค LDR ตอนจบ (step 3)

- เดิม: median 10 ครั้งห่าง 4 ms (~40 ms) + ต้องมืดต่อเนื่อง 3 วิ — LDR สวิงครั้งเดียวรีเซ็ต เครื่องดับแล้วไม่จบ
- ใหม่: `LdrMeanSampler` อ่าน **10 ครั้ง ห่าง 100 ms** แล้ว**เฉลี่ย** (~1 วิ)
- ต้องได้ค่าเฉลี่ย**มืดติดกัน 3 ครั้ง** (`LDR_END_DARK_STREAK`) ถึงจบ — ไม่มืด (สว่าง/เทา) นับใหม่
- เกณฑ์มืดเดิม (โปรไฟล์ / `ldr_set`); Power / Start / เรียนโปรไฟล์ไม่เปลี่ยน
- Serial: `end dark streak=N`

### Rollback

- ย้อนไป: **Version 4.33**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์: `src/main.cpp`, `src/ldr_sampler.h`, `src/varable.h`

---

## Version 4.33 (2026-10-01) — ล้างถัง (โปรแกรม 4) ไม่แจ้ง 01

### โปรแกรม 4 ล้างถังซัก

- ตัวนับถึงนาทีสุดท้าย → ค้าง `00:01` **ไม่นับ** `count_minn_pass` (ไม่แจ้ง 01 / ไม่รีเซ็ตที่ 20 นาที)
- เข้า step 3 รอ LDR มืดค้าง ≥3 วิ → `DONE` + รีเซ็ต (`endProgram = true` ไม่กด Start/Power ซ้ำ)
- ไม่มี timeout — ถ้า LDR เสียจะรอจนกว่าไฟดับ
- โปรแกรมอื่นคงเดิม

### Rollback

- ย้อนไป: **Version 4.32**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 4.32 (2026-08-29) — Mode 7: ซักข้ามเช็ค LDR หลัง Start

### Mode 7

- **Power** ยังเช็ค LDR (โปรไฟล์ / `ldr_set`) เหมือน Mode 1
- **หลัง Start** ข้าม `checkLightStart` — ไม่ขึ้น fault 02 จากขั้นตอนเช็คประตู/ไฟ
- ใช้เมื่อ LDR ไม่เสถียร แยกไฟกระพริบกับค้างไม่ได้
- ตั้ง Mode = 7 (กดค้าง BACK หรือ Melody `ModeSystem`)

### Rollback

- ย้อนไป: **Version 4.31**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์: `src/main.cpp`, `src/varable.h`, `ADMIN_SETTINGS_GUIDE.md`

---

## Version 4.31 (2026-08-27) — LDR เรียน/ตรวจมืดรับค่า 0 (บอร์ดเก่า)

### บอร์ดเก่าไฟมืด = 0

- เดิม `LDR_GLITCH_FLOOR = 35` ตัด sample < 35 → เรียน OFF / dark ไม่สำเร็จเมื่อมืดจริง = 0
- **`learnLdrOffProfile` / `learnLdrBlinkProfile`:** รับ raw = 0
- **`LdrAvgSampler` / `readLDRAverage` / `LdrPeakWindow`:** รับ 0 (median กรอง spike)
- **`checkLightOnOff` / `checkLightWithProfile`:** ถ้าโปรไฟล์มืด/ปิด < 35 ใช้ floor = 0
- เรียน ON ยังตัด < 35 กัน spike ดึงค่าเฉลี่ยลง

### Rollback

- ย้อนไป: **Version 4.30**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์: `src/ldr_sampler.h`, `src/varable.h`

---

## Version 4.30 (2026-08-21) — สลับแหล่ง LDR: โปรไฟล์ที่เรียน vs ldr_set

### เลือกแหล่งตรวจไฟ

- NVS `lpUse` (default **true** = พฤติกรรมเดิมหลัง OTA)
- `true` → ใช้โปรไฟล์ที่เรียน (Mode 1 / Mode 6 / Power / จบรอบ)
- `false` → เกณฑ์ `ldr_set` เดิม — ค่าที่เรียนยังอยู่ใน NVS
- MQTT: `LdrUseLearn` / `LPUse` · `LdrUseDefault` / `LPDefault`
- `LdrLearnStatus` ส่ง `lpUse` กลับเว็บด้วย
- Melody: ปุ่ม «ใช้ค่าที่เรียนรู้» / «ใช้ค่าเดิม (ldr_set)»

### Rollback

- ย้อนไป: **Version 4.29**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`
- หมายเหตุ: คีย์ NVS `lpUse` เหลือได้ ไม่กระทบ 4.29

---

## Version 4.29 (2026-08-21) — Step3 จบรอบใช้โปรไฟล์ LDR ที่เรียน

### Step 3 (`check ldr end program`)

- ถ้ามีโปรไฟล์ → `classifyPowerLdrSample`: **มืด(ปิด) ค้าง ≥3 วิ** ถึงจบ; สว่าง/เทา = ยังทำงาน รีเซ็ตนาฬิกา
- ไม่มีโปรไฟล์ → เกณฑ์ `ldr_set` เดิม
- fault **01** (ค้างมืดไม่พอตอนนาทีท้าย) คงเดิม

### Rollback

- ย้อนไป: **Version 4.28**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.28 (2026-08-21) — Power check อ่าน LDR แบบ median

### Power check

- ใช้ `readLDRAverage()` (median ~10 sample) แทน `analogRead` ครั้งเดียว — ลด false DARK จาก spike 4095
- Serial: `avg=` แทน `now=`

### Rollback

- ย้อนไป: **Version 4.27**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.27 (2026-08-21) — Power check ใช้โปรไฟล์ LDR ที่เรียน

### Power check (`chanel` 2)

- ถ้ามีโปรไฟล์เรียนแล้ว (`hasOn`/`hasOff` หรือ LP1 `valid`) → ใช้ `classifyPowerLdrSample()` แทน `ldr_set`
- Mode **1** และ **6**
- ยังไม่มีโปรไฟล์: Mode 1 ใช้ `ldr_set`/`ldrMinus` เหมือนเดิม; Mode 6 ข้ามเช็คเหมือนเดิม
- Serial: `profileCls=` เมื่อใช้โปรไฟล์ (2=PASS, 0=DARK)

### Rollback

- ย้อนไป: **Version 4.26**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.26 (2026-08-21) — แก้ ANOT Ln/MQ ปุ่มสลับ

### Anothersetting (TM)

- Mode2=10 (**Ln**): UP/DOWN ปรับ `ldrMinus` (เดิมไปแตะ mqttStatus)
- Mode2=11 (**MQ**): UP/DOWN ปรับ `mqttStatus` (เดิมไปแตะ ldrMinus)
- จอและปุ่มตรงกันแล้ว

### Rollback

- ย้อนไป: **Version 4.25**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.25 (2026-08-21) — Mode 6 เรียนรู้เปิด/ปิด LDR

### Mode 6

- ใช้โปรไฟล์ `hasOn` + `hasOff` เท่านั้น (ไม่มีกระพริบ)
- เรียนรู้ด้วย LP2 / LP3 หรือ MQTT `LdrLearnOn` / `LdrLearnOff`
- `checkLightOnOffProfile()` ใน `ldr_sampler.h`

### Rollback

- ย้อนไป: **Version 4.24**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.24 (2026-08-21) — LDR remote max:min + learn result

### LdrOpen stream

- ทุก 1000ms ส่ง `ldrMax` / `ldrMin` (และ `ldr`=max) จากตัวอย่างในรอบนั้น เช่น 4000:500

### LdrLearnBlink / On / Off

- โชว์บนจอ TM (LP1/2/3 + ผล) และ publish `cm=ldrLearnResult` → `commandBack`

### Rollback

- ย้อนไป: **Version 4.23**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.23 (2026-08-21) — LPStatus ตอบ MQTT

### LdrLearnStatus / LPStatus

- publish `cm=ldrProfileStatus` → topic **`commandBack`** (lpValid, period, bright, dark, hasOn/on, hasOff/off)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.22**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.22 (2026-08-21) — LDR ทางไกลส่งค่าทุก 1000ms

### LdrOpen / LdrOpen2

- เมื่อสั่ง `LdrOpen` ตั้ง `stateLdrOpen` → publish `cm=ldrSample` ไป topic **`commandBack`** ทุก **1000ms**
- payload: `id`, `ldr`, `pin`, `value_str2=LdrOpen|LdrOpen2`
- `LdrClose` / `LdrClose2` หยุด stream
- จอ/Serial ยังอ่าน 100ms (เฉพาะเครื่อง) — ไม่ยิง MQTT ถี่
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.21**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.21 (2026-08-21) — MQTT เรียนรู้โปรไฟล์ LDR

### cmCommand

- `LdrLearnBlink` / `LP1` — เรียนรู้กระพริบ + Save NVS
- `LdrLearnOn` / `LP2` — เรียนรู้ไฟติดค้าง + Save
- `LdrLearnOff` / `LP3` — เรียนรู้ไฟดับ + Save
- `LdrLearnStatus` / `LPStatus` — ดูสถานะโปรไฟล์
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.20**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.20 (2026-08-21) — Mode 1 โปรไฟล์ LDR (คาบกระพริบ)

### checkLightStart Mode 1

- เรียนรู้โปรไฟล์: **LP1** กระพริบ (วัด `periodMs` + bright/dark), **LP2** ติดค้าง, **LP3** ดับ
- ถ้า `ldrLightProfile.valid` → อ่านตามคาบ; มียอดมืดตามโปรไฟล์ ≥2 ครั้ง = BLINK (02)
- ยังไม่เรียนรู้ → ใช้ logic เดิม (CM4 path / generic)
- NVS: `lpValid`, `lpPeriod`, `lpBright`, `lpDark`, `lpOn`, `lpOff`
- TM เมนู Another: Mode2 18–21 (`LP` + 1/2/3 + period/10)
- ไฟล์: `src/ldr_sampler.h`, `src/main.cpp`, `src/tm1637.h`, `src/varable.h`

### วิธีใช้

1. ตั้งไฟกระพริบ → เมนู LP1 → กด UP (จับ ~4 วิ) → Save
2. ไฟติดค้าง → LP2 → UP → Save
3. ไฟดับ → LP3 → UP → Save
4. Start Mode 1 ทดสอบ 02

### Rollback

- ย้อนไป: **Version 4.19**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- หรือลบ NVS key `lpValid` เพื่อปิดโปรไฟล์

---

## Version 4.19 (2026-07-26) — CM4 เกณฑ์กระพริบปรับได้โดยแอดมิน

### cm4_blink_th

- ตัวแปรใหม่ default **1000** — ยอด ≥ ค่านี้ในเช็ค 02 = BLINK
- แอดมิน TM: เมนู Another → `bL` (Mode2=17) ปรับทีละ 100 (ช่วง 100–4000) แล้ว Save
- แอดมิน ATD35: Another setting → "CM4 blink th"
- NVS key `cm4Blink` (/100) | MQTT `cm4_blink_th` (รับได้ทั้ง /100 และค่าเต็ม)
- เครื่อง noise สูง (ไฟนิ่งยอด ~1900) แนะนำตั้ง **2000**
- ไฟล์: `src/varable.h`, `src/main.cpp`, `src/tm1637.h`, `src/run_session.h`

### Rollback

- ย้อนไป: **Version 4.18**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.18 (2026-07-26) — CM4 จบรอบต้อง LDR >3500

### step==3 end LDR CodeMachine 4

- ไฟดับจบรอบ: ค่าต้อง **>3500** ค้าง 3 วิ (เดิมสูตร ldr_set+ldrMinus/2 บนบอร์ดใหม่ได้แค่ ~1750)
- สว่างรีเซ็ตยัง `<1500`
- เฉพาะ CM4
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.17**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.17 (2026-07-26) — CM4 เช็ค 02 อ่าน 5 หน้าต่าง

### checkLightStart CodeMachine 4

- `winCount = 5` (เดิม 3) ≈ 10 วิ ก่อนสรุปผล
- คง EMPTY→ON และยอด ≥1000 = BLINK
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.16**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.16 (2026-07-26) — CM4 EMPTY ถือเป็น ON

### checkLightStart CodeMachine 4

- หน้าต่าง EMPTY (sample ไม่พอ / ค่าต่ำ) → นับเป็น **ON** ไม่ fail เป็น unstable
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.15**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.15 (2026-07-26) — CM4 เช็ค 02 แยกด้วยยอด ≥1000 @500ms

### checkLightStart CodeMachine 4 (fault 02)

- อ่านทุก **500ms**, หน้าต่าง **2s × 3** (~6 วิ)
- ตัด glitch `<35`; มียอด **≥1000** ในหน้าต่าง → BLINK; ไม่มี → ON
- จาก log ทดสอบ: ไฟนิ่ง max<~800, กระพริบมียอด ≥1000
- เฉพาะ CM4 — โหมดทดสอบยัง 500ms ตามที่ทดลอง
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.14**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 4.14 (2026-07-26) — โหมดทดสอบ LDR อ่านทุก 1300ms

### checkLdr1

- ช่วงอ่านทดสอบ: **1300ms** (สาย LDR ยาว ~150 ซม. ลด noise)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.13**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.13 (2026-07-26) — โหมดทดสอบ LDR อ่านทุก 900ms

### checkLdr1

- ช่วงอ่านทดสอบ: **900ms** (ค่าดิบทันที ตาม 4.12)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.12**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.12 (2026-07-26) — โหมดทดสอบ LDR แสดงค่าดิบทันที

### checkLdr1

- อ่าน `analogRead` แล้วโชว์/พิมพ์ทันที — ไม่เฉลี่ย ไม่จัด ON/BLINK
- `checkLightStart` CM4 ยังใช้หน้าต่าง 700ms ตามเดิม (4.11)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.11**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 4.11 (2026-07-26) — CM4 ทดลองอ่าน LDR ทุก 700ms

### checkLightStart + checkLdr1 CodeMachine 4

- `sampleGapMs` / ช่วงอ่านทดสอบ: **700ms** (เดิม 80ms)
- หน้าต่าง: **3s × 3** (~9 วิ) ให้ได้อย่างน้อย ~4 จุดต่อหน้าต่าง
- เฉพาะ CM4 — ทดสอบว่าค่าเสถียรกว่าหรือไม่
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.10**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 4.10 (2026-07-26) — CM4 แยกไฟนิ่ง / กระพริบใหม่

### checkLightStart + checkLdr1 CodeMachine 4

- ยอดสูง **≥900**; BLINK เมื่อ `highN ≥ 3` หรือ (`highN ≥ 2` และ `max ≥ 1000`)
- เลิกเกณฑ์ `highN≥1 + span` ที่ทำให้ไฟนิ่ง noise ขึ้น BLINK ผิด
- คง delay หลัง Start (4.09) + ยืนยัน ON 2 รอบ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.09**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 4.09 (2026-07-26) — CM4 รอไฟนิ่งหลัง Start ก่อนอ่าน LDR

### fault 02 CodeMachine 4

- หลัง `Start()` รอ **4 วิ** แล้วค่อยเข้า `checkLightStart` (กันช่วงไฟกำลังติดอ่านเป็น BLINK)
- ยืนยัน ON รอบ 2: รอ 3 วิ แล้วเช็คซ้ำที่ case 5 — **ไม่กด Start ซ้ำ**
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.08**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (case 4/5), `src/varable.h`

---

## Version 4.08 (2026-07-26) — ท้ายรอบพึ่ง LDR มืดจริง (เลิกจบนาที 5 ของ CM3/4)

### 5 นาทีสุดท้าย / ค้าง 0:01

- **ลบ** บังคับจบที่ `count_minn_pass == 5` สำหรับ CodeMachine 3/4
- คง: 15 → fault **01** + จอ `-01-` | 20 → `case 10` รีเซ็ต
- LDR end (บอร์ดใหม่, Mode 1/6 หรือ CM3/4): มืด **`≥3500`** ค้าง 3 วิ → จบ; สว่างชัด **`<1500`** รีเซ็ตนาฬิกา; ค่ากลางรอต่อ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.07**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`machineRuning`, LDR end step 3), `src/varable.h`

---

## Version 4.07 (2026-07-26) — CM4 จับกระพริบเพดานต่ำ + ยืนยัน ON 2 รอบ

### fault 02 CodeMachine 4

- **สาเหตุ:** ช่วงกระพริบค่าสูงสุดแค่ ~700 ไม่ถึงเกณฑ์ 1000 → ทั้ง 3 หน้าต่างเป็น ON แล้วไปซัก
- **แก้:** `blinkHighTh` 1000→**700**, `span` 700→**500**; Mode 1 ต้องได้ result 2 **ติดกัน 2 ครั้ง** ก่อนเข้าซัก (`CM4 confirm ON streak`)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.06**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 4.06 (2026-07-26) — CM4 เช็ค 02 ยืดเวลา 3×1.5 วิ

### checkLightStart CodeMachine 4

- หน้าต่าง 2×1 วิ → **3×1.5 วิ (~4.5 วิ)** — จับกระพริบได้มั่นคงขึ้น; ON เฉพาะครบทั้ง 3
- หน้าต่างว่าง (sample ไม่พอ) → `EMPTY` นับไม่ผ่าน (ไม่โชว์ BLINK ปลอม min=4095 max=0)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.05**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLightStart` CM4), `src/varable.h`

---

## Version 4.05 (2026-07-26) — CM4 BLINK เมื่อ highN พอ ไม่บังคับ span

### checkLightStart / checkLdr1 CodeMachine 4

- **สาเหตุ:** `highN=5` แต่ `span=550` ไม่ถึง 700 → ตัดสิน ON ผิด ทั้งที่ไฟกระพริบ
- **แก้:** BLINK ถ้า `highN ≥ 2` หรือ (`highN ≥ 1` และ `span ≥ 700`)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.04**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 4.04 (2026-07-26) — CodeMachine 4 เช็ค 02 ด้วยแอมปลิจูด LDR

### checkLightStart — เฉพาะ CodeMachine 4 + บอร์ดใหม่

- ไม่พึ่งมืด 4095: อ่านถี่ 2 หน้าต่าง × 1 วิ แยก ON/BLINK/OFF แบบโหมดทดสอบ
- มี BLINK ≥1 หน้าต่าง → result 1 (วน Start แล้ว **02**); ON ทั้ง 2 หน้าต่าง → result 2
- CodeMachine อื่นใช้ logic เดิม
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.03**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLightStart`), `src/varable.h`

---

## Version 4.03 (2026-07-26) — checkLdr1 อ่านถี่จับไฟกระพริบ

### โหมดทดสอบ LDR1

- ช่วงอ่าน 900 ms → **80 ms**; ใช้ `readLDRInstant` แทน average/median
- เฉพาะ `checkLdr1()` — ไม่แตะ `checkLdr2` / power / `checkLightStart`
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.02**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLdr1`), `src/varable.h`

---

## Version 4.02 (2026-07-26) — Mode 1 power check ตามเกณฑ์ &lt;3000 / &gt;3500

### fault 00 — หลักการเรียบ ไม่ใช้ peak/trough

- บอร์ดใหม่: `val < ldr_set` (3000) → ไปต่อทันที; `val > ldr_set+500` (3500) → รอ 5 วิ แล้ว Power ซ้ำ; ครบ 5 ครั้ง → **00**
- โซน 3000–3500: รออย่างเดียว (ไม่ผ่าน ไม่นับมืด)
- บอร์ดเก่า: polarity กลับ (`> ldr_set` ไปต่อ / `< ldr_set-500` มืด)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.01**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (case 1–2 Mode 1), `src/varable.h`

---

## Version 4.01 (2026-07-26) — checkLightStart กันกระพริบหลุดเป็น Light On

### Mode 1 fault 02 — A+B+C

- **A:** มีค่าเทาในหน้าต่าง 3 วิ → `Light Unstable` (ไม่ประกาศ On)
- **B:** เกณฑ์มืดผ่อนเป็น `ldrMinus/2` (บอร์ดใหม่ Off เมื่อ >3500 แทน >4000) ให้ค่าอย่าง 3967 นับ Blink Off ได้
- **C:** มี Blink On ≥2 ครั้ง (กระพริบซ้ำ) → ไม่ประกาศ On; On ได้เฉพาะสว่างนิ่งหลังขอบขึ้นครั้งเดียว และไม่เทา
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.00**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLightStart`), `src/varable.h`

---

## Version 4.00 (2026-07-26) — Mode 1 power check ใช้ trough บนบอร์ดใหม่

### fault 00 — จับไฟกระพริบถูกขั้ว

- **สาเหตุ:** `LdrPeakWindow.peak()` = ค่าสูงสุด — บอร์ดใหม่สว่าง=ค่าต่ำ → peak ค้างมืด ไม่ช่วยจับกระพริบ
- **แก้:** เพิ่ม `trough()` (ต่ำสุด); OldBoard 0 ผ่านเมื่อ `val` หรือ `trough` ≤ `ldr_set`; OldBoard 1 ยังใช้ `peak` (สูง=สว่าง)
- รวม case 2 Mode 1 เป็นบล็อกเดียวตาม polarity
- ไฟล์: `src/ldr_sampler.h`, `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.99**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/ldr_sampler.h`, `src/main.cpp`, `src/varable.h`

---

## Version 3.99 (2026-07-26) — checkLightStart หลักการเดียวทั้ง OldBoard 0/1

### Mode 1 start — polarity กลับกันตามบอร์ด

- รวม logic เป็นชุดเดียว: สว่างนิ่ง→2, มืดนิ่ง→0, โซนเทา/กระพริบครบ→1 (retry แล้ว 02)
- **OldBoard 1:** สูง=สว่าง (`> ldr_set`) / ต่ำ=มืด (`< ldr_set-ldrMinus`) — ไม่นับโซนเทาเป็น On Count อีก
- **OldBoard 0:** ต่ำ=สว่าง / สูง=มืด (เหมือน v3.98)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.98**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLightStart`), `src/varable.h`

---

## Version 3.98 (2026-07-26) — checkLightStart ไม่ถือโซนเทาเป็น Light On

### Mode 1 start — ไฟกระพริบต้อง retry แล้ว 02

- **สาเหตุ:** timeout 3 วิ ของ `checkLightStart` (OldBoard 0) ถือค่ากลาง (`ldr_set` < val ≤ `ldr_set+ldrMinus`) เป็น On → ไปซักต่อทั้งที่ไฟกระพริบ ไม่ถึง error 02
- **แก้:** On เฉพาะ `<= ldr_set`; Off เมื่อ `> ldr_set+ldrMinus`; โซนเทา → `Light Unstable` คืน 1 (Mode 1 วน Start แล้วครบ 5 ครั้ง → 02)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.97**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLightStart` timeout), `src/varable.h`

---

## Version 3.97 (2026-07-08) — กัน setRelayType ทับเวลาโปรแกรมเป็น 31

### TimeCountdown sync

- หลัง `setRelayType()` เรียก `applyMelodyProgramDurations(timerDry…)` อีกครั้งใน `commitMelodyPreferencesToNvs` / factory defaults
- ซัก P1–P3 ใช้เวลาตาม Melody (`timedry`/`duration`) ไม่ถูกทับกลับเป็น `0:31`
- ไม่แก้ logic โปรแกรม 4/5/6
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อน **3.95** (ข้าม 3.96 ที่ ATD35 มีแต่ TM ไม่มี)

---

## Version 3.95 (2026-07-08) — fault 01 จอ TM แสดง -01- ค้าง 15–19 นาที

### machineRuning() — ซักค้าง 0:01

- ครบ 15 นาที: แจ้ง Melody fault 01 + จอ `-01-` ค้าง (ไม่ถูก timer blink ทับ)
- ครบ 20 นาที: รีเซ็ตโปรแกรมเหมือนเดิม
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อน **3.94**

---

## Version 3.94 (2026-07-08) — เวลาโปรแกรม Melody → ซัก P1–P3

### sync duration1–3 (timedry1–3) → TimeCountdown1–3

- `applyMelodyProgramDurations()` หลังรับ config MQTT — ซักใช้เวลาเดียวกับอบ (ไม่ค้าง 0:31 จาก ESP config เก่า)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อน **3.93**

---

## Version 3.93 (2026-07-08) — boot MQTT sync บันทึก NVS รอบเดียว

### configResponse + setPromoSlots หลัง debounce

- ใช้ config/promo ใน RAM ก่อน แล้ว `writePreferencesfirst` + `writePreferences` ครั้งเดียว
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อน **3.92**

---

## Version 3.92 (2026-07-07) — OTA โฟลเดอร์ v4 WSS

### แยก OTA folder สำหรับ Melody v4 (MQTT_USE_WEBSOCKET)

- OldBoard 1 → `userID = wss_old` → `fw/wss_old/`
- OldBoard 0 → `userID = wss_new` → `fw/wss_new/`
- TCP เก่า (MQTT_USE_WEBSOCKET 0) ยังใช้ `ai_old` / `ai_new`
- ไฟล์: `src/varable.h`

### Rollback

- ย้อน **3.91** หรือตั้ง `MQTT_USE_WEBSOCKET 0`

---

## Version 3.91 (2026-07-07) — Melody protocol v4 (mv:4 + WSS) — TM + ATD35

### Melody v4 — แยก transport จาก v1–v3

- `MELODY_PROTOCOL_VERSION 4` + presence `mv:4`, `transport:wss` เมื่อ `MQTT_USE_WEBSOCKET`
- v4 = คำสั่ง/payload เหมือน v3 แต่ MQTT over WSS (Cloudflare)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ตั้ง `MQTT_USE_WEBSOCKET 0`, `mv:3` หรือย้อน **3.90**

---

## Version 3.90 (2026-07-07) — TM only

### WSS ผ่าน Cloudflare `melodymqtt.ma-well.com` → Mosquitto WS :9001

- `MQTT_USE_WEBSOCKET 1` ใน `varable.h` — ใช้ `WebSocketsClient` + `PubSubClient` แทน TCP
- Cloudflare: `wss://melodymqtt.ma-well.com:443/` + subprotocol `mqtt`
- ทดสอบ LAN ตรง PC: `MQTT_WS_USE_SSL 0`, `mqtt_ws_host` = IP PC, `mqtt_ws_port` = 9001
- lib: `links2004/WebSockets` ใน `platformio.ini`
- ไฟล์: `src/main.cpp`, `src/varable.h`, `platformio.ini`
- **ATD35 ไม่เปลี่ยน**

### Rollback

- ตั้ง `MQTT_USE_WEBSOCKET 0` หรือย้อนไป **Version 3.88**

---

## Version 3.88 (2026-07-07) — TM only

### ย้อนโมเดล WiFi/MQTT กลับ `loop()` — `serviceNetwork()` เธรดเดียว (จาก stash v3.83)

- **ทำไม:** ต้องการโมเดลเดิมที่ WiFi + `mqclient.loop()` รันใน **`loop()`** ไม่ผ่าน `taskWifiMqtt`
- **โมเดล:** `loop()` → `serviceNetwork()` ทุกลูป; `mqclient.loop()` ไม่ถูก mutex บล็อก; ไม่สร้าง `taskWifiMqtt`
- **คงจาก v3.83:** keepAlive 60s, revenue HTTP, backoff reconnect 5-5-10-10, `SET_LOOP_TASK_STACK_SIZE(16KB)`
- **ตัดออกจาก v3.84–3.87:** `taskWifiMqtt`, diagnostic log v3.85, reconnect 5-5-5-5 v3.86, Uptime topic v3.87
- ไฟล์: `src/main.cpp`, `src/main.h`, `src/varable.h`
- **ATD35 ไม่เปลี่ยน** — ยังใช้ `taskWifiMqtt` ตามเดิม

### Rollback

- ย้อนไป: **Version 3.84** — `git checkout ac7deda -- src/main.cpp src/main.h src/varable.h`
- หรือกลับ **taskWifiMqtt + log v3.87** จาก working tree ก่อนย้อน

---

## Version 3.87 (2026-07-07)

### MQTT Uptime + log UpdateState (Melody legacy sync)

- ส่ง **Uptime** คู่กับ UpdateState (`{ID, Title, Time}`) — ระบบเก่าใช้ topic นี้อัปเดตนับถอยหลัง
- log `[MQTT] UpdateState ->` / `[MQTT] Uptime ->` หลัง publish สำเร็จ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.86**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน

---

## Version 3.86 (2026-07-07)

### MQTT reconnect เร็วขึ้น — 5-5-5-5 วินาทีคงที่ก่อนหมุนพอร์ต

- **หลุด (edge):** reset timer → ลอง `connect` ทันทีรอบถัดไป (ไม่รอ 5s แรก)
- **connect fail:** retry ทุก **5s คงที่** (ตัด backoff 5→10→15) — fail ครบ **4 ครั้ง** ค่อย `mqtt_port1++` (4741–4744)
- log: `CONNECT_FAIL ... retry in 5s fail x/4`, `ROTATE port ->`
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.85**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 3.85 (2026-07-07)

### Serial log วินิจฉัย MQTT loop + หลุด (uptime + RTC)

- **`mqttPumpLoopLocked()`** — log ทุก 30s: `tag`, `rounds`, `total` pump count + `up=...s rtc=HH:MM:SS`
- **`logMqttDropped(reason)`** — log ตอนหลุดพร้อม `rc`, WiFi, RSSI, `failStreak` (reason: `edge` / `wifi_down` / `graceful_offline`)
- **connect สำเร็จ** — log พร้อม uptime + RTC
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.84**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 3.84 (2026-07-07)

### กลับไปโมเดลเน็ต v3.78 (ตอบสนองดี) + เก็บเฉพาะ HTTP revenue

- **ทำไม:** v3.80–3.83 (keepAlive 90s + ย้าย `mqclient.loop()` เข้า `loop()` แบบ single-thread + no-op mutex) ทำให้ **สั่งการ MQTT ไม่ตอบสนอง** ทั้งที่ broker รับคำสั่งแล้ว — ยืนยันจากภาคสนามว่า v3.78 ตอบสนองดีกว่าชัดเจน
- **ทำอะไร:** revert โมเดล WiFi/MQTT servicing กลับเป็น **v3.78 เป๊ะ** (`taskWifiMqtt` เดิม, `gNetMutex` recursive เดิม, keepAlive 60s, backoff/port เดิม) แล้ว **เติมกลับเฉพาะฟีเจอร์ HTTP revenue** จาก v3.79
- **ตัดออกจาก v3.79 (เพราะแตะ MQTT servicing):**
  - เอา `netLockTryEnter()` helper ออก
  - เอา pump `mqclient.loop()` ต้นรอบ `taskWifiMqtt` ออก → cadence keepalive กลับเท่า v3.78
- **เก็บไว้ (HTTP revenue ล้วน):** `sendRevenueHttp()` + `txnId` idempotent, persist `pendingBalance/sendAmt/sendTxn/txnSeq` ลง NVS `revenue`, `revenueRestore()` ตอน boot, throttle 4s, endpoint `POST /public/machines/device-revenue` (backend เดิม)
- ไฟล์: `src/main.cpp`, `src/varable.h`
- เครื่องเดิมที่ยังส่งรายรับผ่าน MQTT ไม่กระทบ (backend รองรับทั้งสองทาง)

### Rollback

- ย้อนไป: **Version 3.78** — `git checkout f34a4f2 -- src/main.cpp src/main.h src/varable.h`
- หรือย้อนไป **v3.79** (มี pump/tryEnter) = `ff3ed1e`

---

## Version 3.79 (2026-07-06)

### รายรับส่งทาง HTTP (idempotent) แทน MQTT postSQL + กัน MQTT flap ทำ loop() ขาด

- **อาการ:** เครื่องออนไลน์/ออฟไลน์ตลอด — MQTT `dropped rc=-4 wifi=3 rssi=-51` วนซ้ำ (RSSI ดี, connect สำเร็จทุกครั้ง = ping timeout ไม่ใช่สัญญาณ) → รายรับที่ส่งผ่าน MQTT `postSQL` (QoS0) เสี่ยงหายตอนหลุด และ HTTP fallback เดิมไม่เคยทำงานเพราะ connect สำเร็จ (streak ไม่ถึง 20)
- **A — รายรับผ่าน HTTP:**
  - Backend: เพิ่ม `POST /public/machines/device-revenue` (auth ด้วย device api_key เดียวกับ update-state) — reuse core `recordDeviceRevenue()` (dedup ±window เดิม) + **idempotency key `txnId`** (เก็บใน `description = "txn:<id>"`) กันรายรับซ้ำเวลา ESP retry
  - Firmware: รายรับส่ง HTTP เป็นหลัก (`sendRevenueHttp`) buffer+retry จนได้ 2xx, แนบ `txnId` (persistent `<Noserial>-<seq>`), persist `pendingBalance/sendAmt/sendTxn/txnSeq` ลง NVS namespace `revenue` (กู้หลัง reboot) — เลิกใช้ MQTT postSQL/UpdateBalanceV3(Azure) สำหรับรายรับ
- **B — กัน flap แย่ลง (ไม่แตะ keepAlive/socketTimeout/port ตาม regression-guard):**
  - throttle การส่งรายรับ HTTP ทุก 4s (กันถือ net lock ถี่จน `mqclient.loop()` ขาด)
  - pump `mqclient.loop()` ต้นรอบ `taskWifiMqtt` ด้วย `netLockTryEnter(30ms)` (lock สั้น) — รับประกัน cadence keepalive/อ่าน PINGRESP
- ไฟล์: `src/main.cpp`, `src/varable.h`; Backend: `mqtt.service.ts`, `public-api.controller.ts`, `dto/public-device-revenue.dto.ts`
- **ต้อง deploy backend คู่กัน** (endpoint ใหม่) — ถ้ายังไม่ deploy backend ESP จะ retry รายรับค้างไว้ (ไม่หาย)

### หมายเหตุ B (ยังเปิด)

- root cause `rc=-4` ตอน idle (broker `mawell.thddns.net` DDNS/NAT) ยังไม่ปิดสนิท — A ทำให้รายรับปลอดภัยแม้ flap; ถ้าต้องการปิด flap 100% ต้องเก็บ timing log (connect→drop) หรือดูฝั่ง broker/NAT

### Rollback

> **บันทึกสำคัญ:** ถ้า v3.79 ใช้งานไม่ดี **ย้อนกลับไป v3.78 ได้ทันที** — ทุก repo push v3.78 ไว้เป็นจุดย้อนกลับแล้ว

- ย้อนไป: **Version 3.78**
- Commit อ้างอิง:

  | Repo | v3.79 (ปัจจุบัน) | v3.78 (จุดย้อนกลับ) |
  |---|---|---|
  | ATD_TM | `ff3ed1e` | `f34a4f2` |
  | ATD35 | `c4cecc4` | `4c0b028` |
  | MelodyWebapp (backend) | `1d7c1f1` | `34b6f21` |

- วิธีย้อน (ต่อ repo): `git revert <v3.79 commit>` (ปลอดภัย เก็บประวัติ) หรือ `git checkout f34a4f2 -- src/main.cpp src/varable.h` แล้ว build/OTA เวอร์ชัน 3.78
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน + revert backend ด้วย (endpoint `device-revenue` เป็น additive — ถ้าย้อน firmware อย่างเดียว backend เก่ายังรับ MQTT postSQL ได้ ไม่จำเป็นต้อง revert backend)
- หมายเหตุ: NVS namespace `revenue` บนเครื่องจริงไม่ต้องล้าง (คีย์ใหม่ ไม่กระทบ config); firmware 3.78 จะกลับไปส่งรายรับทาง MQTT postSQL เหมือนเดิม

---

## Version 3.78 (2026-07-05)

### Mode 3/4 hier — จังหวะเปลี่ยนรีเลย์สม่ำเสมอ (อ่าน LDR แบบ blocking)

- **อาการ (3.77):** บางครั้งรอนานกว่าจะเปลี่ยนรีเลย์ จังหวะไม่เท่ากัน
- **สาเหตุ:** `SetFirstHier()` ใช้ `LdrAvgSampler` เก็บค่ากระจายข้าม loop หลายรอบ → เวลาระหว่าง "เริ่มอ่าน → ครบ → ขยับรีเลย์" ขึ้นกับ scheduler + งานอื่นใน taskProgram (ไม่คงที่)
- **แก้:** อ่าน LDR ครั้งเดียวแบบ blocking สั้น `readLDRAverage(LDR2_PIN, 4)` (~2ms, median 4 ค่า) → จังหวะคงที่ที่ ~Jok() (900ms) ซึ่ง deterministic; `Jok()` ยัง `vTaskDelay` yield ให้ task อื่นปกติ (ไม่กระทบ MQTT/WiFi คนละ task)
- ไฟล์: `src/main.cpp` (`SetFirstHier`)

### Rollback

- ย้อนไป: **Version 3.77** (sampler 4 ค่า) หรือ 3.76 (sampler 10 ค่า + gap 500)
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.77 (2026-07-05)

### Mode 3/4 (hier) เดินเร็วขึ้น — ลดหน่วงรอบเช็ก power ใน SetFirstHier()

- **อาการ:** โหมด 3/4 เช็ค LDR + ขยับ jok วนจนไฟสว่าง เดินช้า
- **แก้ (TM only, SetFirstHier ถูกเรียกเฉพาะ Mode 3/4):**
  - `actionGapMs` 500 → **100 ms**
  - LDR sampler 10 ค่า → **4 ค่า** (~16ms แทน ~100ms) ยังกรอง spike ด้วย median
- คงไว้: Jok()/JokBack() half-toggle เดิม, count_check_power 15, error ที่ count2 5
- ไฟล์: `src/main.cpp`

### Rollback

- ย้อนไป: **Version 3.76** (actionGap 500, sampler 10 ค่า)
- ไฟล์: `src/main.cpp`, `src/varable.h`

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
