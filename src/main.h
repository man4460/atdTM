#ifndef MAIN_H_
#define MAIN_H_

void checkQrpaymentRead();
void mqttreqest();
void checkQrpaymentRead();
// void callback(char* topic, byte* payload, unsigned int length);
void mqttreconnect();
void setProgram();

void modeSetting();
void settingMode1();
void settingMode2();
void settingMode3();
void Anothersetting();
void Drysetting();
void updateStepIcon();
void prepareRunMachine();

void setRelayType();

void printTocore();

void GetData();
void GetSetupData();
void PublishConfigViaMqtt();  // ส่ง config ปัจจุบันไป topic getdataResponse
void sentVarjson();
void commandApp();

void SetTimerSend();
void SetStatusControl();

void CheckPromotion();

//tm1637

void Button();
void Test1();
void CommandProgram();
void setMode();

void checkbuttonFirst();
void buttonReset();
void shootTemp();
void updateWiFiIcon();
void sentDatatoAdmin();

//oti
void otiUdate();
#endif