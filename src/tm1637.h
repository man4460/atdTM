#include <main.h>
#include <TM1637Display.h>

const uint8_t SEG_Bath[] = {
SEG_G  // -
};
const uint8_t wifi_text[] = {
  0x76, 0x06, 0x71, 0x06 // wifi
};
const uint8_t DOT_ON[]  = { 0x80, 0x00, 0x00, 0x00 }; // จุดเปิด
const uint8_t DOT_OFF[] = { 0x00, 0x00, 0x00, 0x00 }; // จุดปิด


// กำหนดตัวอักษร "OFF"
const uint8_t off[] = {0x3f, 0x71, 0x71, 0x00};
// กำหนดตัวอักษร "EROR"
const uint8_t eror[] = {0x79, 0x50, 0x50, 0x50};

const uint8_t SEG_DONE[] = {
	SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,           // d
	SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
	SEG_C | SEG_E | SEG_G,                           // n
	SEG_A | SEG_D | SEG_E | SEG_F | SEG_G            // E
	};

// อักษร "ANOT"
const uint8_t ANOT_TEXT[] = { 0x77, 0x37, 0x3F, 0x78 }; // "ANOT"

// อักษร "DRYE"
const uint8_t DRYE_TEXT[] = { 0x5E, 0x50, 0x6E, 0x79 };  // "DRYE"

// คำที่ต้องการแสดง
const uint8_t PRIC_TEXT[] = { 0x73, 0x50, 0x06, 0x39 }; // "PRIC"
const uint8_t PROG_TEXT[] = { 0x73, 0x50, 0x3F, 0x3D }; // "PROG"
const uint8_t TEMP_TEXT[] = { 0x78, 0x79, 0x37, 0x73 }; // "TEMP"
const uint8_t STAT_TEXT[] = { 0x6D, 0x78, 0x77, 0x78 }; // "STAT"
const uint8_t THRS_TEXT[] = { 0x78, 0x74, 0x50, 0x6D }; // "THRS"
const uint8_t TMIN_TEXT[] = { 0x78, 0x37, 0x06, 0x37 }; // "TMIN"

// คำที่ต้องการแสดง
const uint8_t RS_TEXT[] = { 0x50, 0x6D, 0x00, 0x00 }; // "RS"
const uint8_t RC_TEXT[] = { 0x50, 0x39, 0x00, 0x00 }; // "RC"
const uint8_t SP_TEXT[] = { 0x6D, 0x73, 0x00, 0x00 }; // "SP"
const uint8_t T1_TEXT[] = { 0x78, 0x06, 0x00, 0x00 }; // "T1"
const uint8_t T2_TEXT[] = { 0x78, 0x5B, 0x00, 0x00 }; // "T2"
const uint8_t T3_TEXT[] = { 0x78, 0x4F, 0x00, 0x00 }; // "T3"
const uint8_t LR_TEXT[] = { 0x38, 0x50, 0x00, 0x00 }; // "LR"
const uint8_t MQ_TEXT[] = { 0x37, 0x3F, 0x00, 0x00 }; // "MQ"
const uint8_t Mode_TEXT[] = { 0b00010100, 0b01000000, 0x00, 0x00 }; // "n-"
const uint8_t MWiFi_TEXT[] = { 0b01110001, 0b01000000, 0x00, 0x00 }; // "F-"

// คำที่ต้องการแสดง
const uint8_t P1_TEXT[] = { 0x73, 0x06, 0x00, 0x00 }; // "P1"
const uint8_t P2_TEXT[] = { 0x73, 0x5B, 0x00, 0x00 }; // "P2"
const uint8_t P3_TEXT[] = { 0x73, 0x4F, 0x00, 0x00 }; // "P3"
const uint8_t D1_TEXT[] = { 0x5E, 0x06, 0x00, 0x00 }; // "D1"
const uint8_t D2_TEXT[] = { 0x5E, 0x5B, 0x00, 0x00 }; // "D2"
const uint8_t D3_TEXT[] = { 0x5E, 0x4F, 0x00, 0x00 }; // "D3"


// อักษร "SL"
const uint8_t SL_TEXT[] = { 0x6D, 0x38, 0x00, 0x00 };  // "SL"

const uint8_t SEG_STANBY_1[] = {
    SEG_A | SEG_B | SEG_G | SEG_F, 
    SEG_G | SEG_C | SEG_D | SEG_E, 
    SEG_A | SEG_B | SEG_G | SEG_F, 
    SEG_G | SEG_C | SEG_D | SEG_E
};
const uint8_t SEG_STANBY_2[] = {
    SEG_G | SEG_C | SEG_D | SEG_E, 
    SEG_A | SEG_B | SEG_G | SEG_F,
    SEG_G | SEG_C | SEG_D | SEG_E, 
    SEG_A | SEG_B | SEG_G | SEG_F
};
const uint8_t SEG_Mode[] = {
    SEG_G,                                           // -
    SEG_G,                                           // -
    SEG_G,                                           // -
    SEG_G                                            // -
};
const uint8_t SEG_Temp[] = {
    SEG_B | SEG_A | SEG_G | SEG_F ,                  // o
    SEG_A | SEG_F | SEG_D | SEG_E                    // C
};
const uint8_t SEG_0Temp[] = {
    SEG_G ,                                          // -
    SEG_G ,                                          // -
    SEG_B | SEG_A | SEG_G | SEG_F ,                  // o
    SEG_A | SEG_F | SEG_D | SEG_E                    // C
};
const uint8_t SEG_40Temp[] = {
    SEG_G | SEG_B | SEG_F | SEG_C,                   // 4
    SEG_C | SEG_A | SEG_F | SEG_D | SEG_E | SEG_B,   // 0
    SEG_B | SEG_A | SEG_G | SEG_F ,                  // o
    SEG_A | SEG_F | SEG_D | SEG_E                    // C
};
const uint8_t SEG_95Temp[] = {
    SEG_C | SEG_A | SEG_F | SEG_D | SEG_G | SEG_B,   // 9
    SEG_C | SEG_A | SEG_F | SEG_D | SEG_G ,          // 5
    SEG_B | SEG_A | SEG_G | SEG_F ,                  // o
    SEG_A | SEG_F | SEG_D | SEG_E                    // C
};
const uint8_t SEG_WiFi[] = {
    SEG_B | SEG_F | SEG_E | SEG_C | SEG_D,           // U
    SEG_F | SEG_E ,                                  // i
    SEG_A | SEG_E | SEG_F | SEG_G,                   // F
    SEG_F | SEG_E                                    // i
};
const uint8_t SEG_it[] = {
    SEG_G,                                           // -
    SEG_F | SEG_E ,                                  // i
    SEG_E | SEG_F | SEG_G | SEG_D,                   // t
    SEG_G                                            // -
};
const uint8_t SEG_00[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_G                                            // -
};
const uint8_t SEG_01[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_B | SEG_C,                                   // 1
    SEG_G                                            // -
};
const uint8_t SEG_02[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_B | SEG_G | SEG_A | SEG_E | SEG_D,           // 2
    SEG_G                                            // -
};
const uint8_t SEG_03[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_B | SEG_G | SEG_A | SEG_C | SEG_D,           // 3
    SEG_G                                            // -
};
const uint8_t SEG_04[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_B | SEG_G | SEG_F | SEG_C,                   // 4
    SEG_G                                            // -
};
const uint8_t SEG_05[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_F | SEG_G | SEG_A | SEG_C | SEG_D,           // 5
    SEG_G                                            // -
};
const uint8_t SEG_06[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_F | SEG_G | SEG_A | SEG_C | SEG_D | SEG_E,   // 6
    SEG_G                                            // -
};
const uint8_t SEG_07[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_B | SEG_A | SEG_C,                           // 7
    SEG_G                                            // -
};const uint8_t SEG_08[] = {
    SEG_G,                                           // -
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D | SEG_G,   // 8
    SEG_G                                            // -
};
const uint8_t SEG_Up[] = {
    SEG_G ,                                          // -
    SEG_B | SEG_F | SEG_E | SEG_C | SEG_D,           // U
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,           // P
    SEG_G                                            // -
};
const uint8_t SEG_SMode[] = {
    SEG_B | SEG_F | SEG_E | SEG_C | SEG_A ,          // m
    SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
    SEG_E | SEG_B | SEG_G | SEG_C | SEG_D ,          // d
    SEG_A | SEG_D | SEG_E | SEG_F | SEG_G ,          // E
};
const uint8_t SEG_Pause[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,           // P
  SEG_C | SEG_F | SEG_A | SEG_E | SEG_G | SEG_B,   // A
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_D,           // U
  SEG_A | SEG_C | SEG_F | SEG_G | SEG_D            // S
};
const uint8_t SEG_Pr01[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,           // P
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_B | SEG_C                                    // 1
};
const uint8_t SEG_Pr02[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,           // P
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_B | SEG_G | SEG_A | SEG_E | SEG_D            // 2
};
const uint8_t SEG_Pr03[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,           // P
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_B | SEG_G | SEG_A | SEG_C | SEG_D            // 3
};
const uint8_t SEG_Pr04[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,           // P
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_B | SEG_G | SEG_F | SEG_C                    // 4
};
const uint8_t SEG_Pr05[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,           // P
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_A | SEG_G | SEG_F | SEG_C | SEG_D            // 5
};
const uint8_t SEG_Pr06[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,           // P
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_A | SEG_G | SEG_F | SEG_C | SEG_D | SEG_E    // 6
};
const uint8_t SEG_Prun[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G, //P
  SEG_E | SEG_F | SEG_A ,                //R
  SEG_F | SEG_E | SEG_D | SEG_C | SEG_B ,//U
  SEG_F | SEG_E | SEG_A | SEG_C | SEG_B  //N
};
// const uint8_t SEG_Pr04[] = { 0b01110011, 0b01010000, 0b00111111, 0b01100110 }; // P, r, 0, 4
// const uint8_t SEG_Pr05[] = { 0b01110011, 0b01010000, 0b00111111, 0b01100111 }; // P, r, 0, 5
// const uint8_t SEG_Pr06[] = { 0b01110011, 0b01010000, 0b00111111, 0b01011110 }; // P, r, 0, 6
// const uint8_t SEG_Prun[] = { 0b01110011, 0b01010000, 0b01111001, 0b01010100 }; // P, r, u, n
const uint8_t SEG_SS[] {
    SEG_A | SEG_C | SEG_F | SEG_G | SEG_D,          // S
    SEG_A | SEG_D | SEG_E | SEG_F | SEG_G ,         // E
    SEG_E | SEG_F | SEG_G | SEG_D,                  // t
    SEG_G                                           // -
};
const uint8_t SEG_test[] {
  SEG_E | SEG_F | SEG_G | SEG_D,                  // t
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G ,         // E
  SEG_A | SEG_C | SEG_F | SEG_G | SEG_D,          // S
  SEG_E | SEG_F | SEG_G | SEG_D                   // t
};
const uint8_t SEG_Jok[] = {
  SEG_B | SEG_C | SEG_D  ,                        // j
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,  // O
  SEG_G ,                                         // -
  SEG_B | SEG_C                                   // 1
};
const uint8_t SEG_Jok2[] = {
  SEG_B | SEG_C | SEG_D  ,                        // j
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,  // O
  SEG_G ,                                         // -
  SEG_B | SEG_G | SEG_A | SEG_E | SEG_D           // 2
};
const uint8_t SEG_Jok2S[] = {
  SEG_G ,                                         // -
  SEG_B | SEG_C | SEG_D ,                         // j
  SEG_A | SEG_C | SEG_F | SEG_G | SEG_D,          // S
  SEG_G                                           // -
};
const uint8_t SEG_J1BS[] = {
  SEG_B | SEG_C | SEG_D ,                           // j
  SEG_B | SEG_C,                                    // 1
  SEG_G,                                            // -
  SEG_D | SEG_E | SEG_F | SEG_G | SEG_C             // b
};
const uint8_t SEG_SP[] = {
  SEG_A | SEG_C | SEG_F | SEG_G | SEG_D,            // S
  SEG_G                                            // -
};
const uint8_t SEG_Relay5[] = {
  SEG_A | SEG_E | SEG_F,                          // r
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G ,         // E
  SEG_G                                           // -
};
const uint8_t SEG_T[] = {
  SEG_E | SEG_F | SEG_G | SEG_D                  // t
};
const uint8_t SEG_LDR[] = {
  SEG_E | SEG_F | SEG_D,                           // L
  SEG_G                                            // -
};const uint8_t SEG_LDRMI[] = {
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_A,           // n
  SEG_G                                            // -
};
const uint8_t SEG_Time[] = {
  SEG_E | SEG_F | SEG_G | SEG_D,                  // t
  SEG_B | SEG_C ,                                 // i
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_A ,         // m
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G           // E
};
const uint8_t SEG_ON[] = {
  SEG_G ,                                          // -
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_A,           // n
  SEG_G                                            // -
};
const uint8_t SEG_OFF[] = {
  SEG_G ,                                          // -
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_A | SEG_E | SEG_F | SEG_G,                   // F
  SEG_A | SEG_E | SEG_F | SEG_G                    // F
};
const uint8_t SEG_Drum[] = {
  SEG_E | SEG_B | SEG_G | SEG_C | SEG_D ,          // d
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_D,           // U
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_A            // m
};
const uint8_t SEG_P[] = {
  SEG_G ,                                         // -
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,          // P
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,  // O
  SEG_G 
};
// แสดง "PC" เมื่อแอดมินส่ง getdata สอบถาม
const uint8_t SEG_PC[] = {
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,          // P
  SEG_A | SEG_F | SEG_D | SEG_E,                  // C
  SEG_G,
  SEG_G
};
const uint8_t SEG_J[] = {
  SEG_G ,                                         // -
  SEG_B | SEG_C | SEG_D  ,                        // j
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,  // O
  SEG_G                                           // -
};
const uint8_t SEG_S[] = {
  SEG_G,                                          // -
  SEG_A | SEG_C | SEG_F | SEG_G | SEG_D,          // S
  SEG_E | SEG_F | SEG_G | SEG_D,                  // t
  SEG_G                                           // -
};
const uint8_t SEG_SPIN[] = {
  SEG_A | SEG_C | SEG_F | SEG_G | SEG_D,          // S
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G,          // P
  SEG_B | SEG_C,                                  // 1
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_A           // n
};
const uint8_t SEG_TEMP[] = {
  SEG_E | SEG_F | SEG_G | SEG_D,                  // t
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,           // E
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_A ,         // n
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_G          // P
};
const uint8_t SEG_LR[] = {
  SEG_B | SEG_C,                                    // 1
  SEG_E | SEG_B | SEG_G | SEG_C | SEG_D ,          // d
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_G                                           // -
};
const uint8_t SEG_Door[] = {
  SEG_E | SEG_B | SEG_G | SEG_C | SEG_D ,          // d
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,   // O
  SEG_A | SEG_E | SEG_F                            // r
};
const uint8_t SEG_Cost[] = {
  SEG_A | SEG_D | SEG_E | SEG_F ,                 // c
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,  // O
  SEG_A | SEG_C | SEG_F | SEG_G | SEG_D,          // S
  SEG_E | SEG_F | SEG_G | SEG_D                   // t
};
const uint8_t SEG_CS[] = {
  SEG_A | SEG_D | SEG_E | SEG_F ,                 // c
  SEG_G                                           // -
};
const uint8_t SEG_SL[] = {
  SEG_E | SEG_F | SEG_D ,                         // L
  SEG_G                                           // -                                          
};
const uint8_t SEG_boot[] = {
  SEG_E | SEG_F | SEG_G | SEG_C | SEG_D ,          // b
  SEG_C | SEG_D | SEG_G | SEG_E ,                  // o
  SEG_C | SEG_D | SEG_G | SEG_E ,                  // o 
  SEG_E | SEG_F | SEG_G | SEG_D                    // t                                       
};
const uint8_t SEG_Read[] = {
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_G                                            // -
};
const uint8_t SEG_d[] = {
    SEG_E | SEG_B | SEG_G | SEG_C | SEG_D          // d
};
const uint8_t SEG_Rin1[] = {
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_C                                    // 1
};
const uint8_t SEG_Rin2[] = {
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_G | SEG_A | SEG_E | SEG_D            // 2
};
const uint8_t SEG_Rin3[] = {
  SEG_A | SEG_E | SEG_F,                           // r
  SEG_B | SEG_G | SEG_A | SEG_C | SEG_D            // 3
};
const uint8_t SEG_WL[] = {
  SEG_B | SEG_F | SEG_E | SEG_C | SEG_D,           // U
  SEG_G                                            // -
};
const uint8_t SEG_Slot[] = {
  SEG_A | SEG_C | SEG_F | SEG_G | SEG_D,          // S
  SEG_E | SEG_F | SEG_D ,                         // L
  SEG_B | SEG_F | SEG_A | SEG_E | SEG_C | SEG_D,  // O
  SEG_E | SEG_F | SEG_G | SEG_D                   // t
};
const uint8_t SEG_CD[] = {
  SEG_G ,                                            // -
  SEG_A | SEG_D | SEG_E | SEG_F ,                 // c
  SEG_E | SEG_B | SEG_G | SEG_C | SEG_D ,          // d
  SEG_G                                            // -
};
const uint8_t SEG_RUN[] = {
  SEG_E | SEG_F | SEG_A , 
  SEG_F | SEG_E | SEG_D | SEG_C | SEG_B , 
  SEG_F | SEG_E | SEG_A | SEG_C | SEG_B , 
  SEG_G
};
const uint8_t SEG_1[] = {SEG_A ,false ,false ,SEG_D};
const uint8_t SEG_2[] = {SEG_F ,false ,false ,SEG_C};
const uint8_t SEG_3[] = {SEG_E ,false ,false ,SEG_B};
const uint8_t SEG_4[] = {SEG_D ,false ,false ,SEG_A};
const uint8_t SEG_5[] = {false ,SEG_D ,SEG_A ,false};
const uint8_t SEG_6[] = {false ,SEG_A ,SEG_D ,false};


// รูปแบบ segment สำหรับ "M" และ "-"
const uint8_t SEG_M[] = {
  SEG_G , // "-"
  SEG_G  // "-"
};

const uint8_t SEG_Cod[] = {
  SEG_C | SEG_E | SEG_G | SEG_A | SEG_D, // C
  SEG_G | SEG_C | SEG_D | SEG_E, // o (ใช้เลข 0 แทน)
  SEG_A | SEG_F | SEG_E | SEG_D | SEG_C // d
};

const uint8_t SEG_Lrmin[] = {
  SEG_E | SEG_F | SEG_D,               // L
  SEG_C | SEG_E | SEG_G               // n 
};

const uint8_t SEG_SlotPin[] = {
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D, // S
  SEG_E | SEG_F | SEG_D                 // L
};


const uint8_t SEG_SpinHier[] = {
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D, // S
  SEG_E | SEG_F | SEG_G | SEG_C         // h
};




  