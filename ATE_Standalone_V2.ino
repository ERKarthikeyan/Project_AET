// ================================================================
//  ATE Power Cycle Test — ESP32-S3 Standalone
//  Version  : ATE_Standalone_V2
//  Company  : Advantech Europe | Test & Validation Engineering
//  Engineer : Karthikeyan Thirunavukarasu
// ================================================================
//
//  FEATURES
//   1. WiFi AP ATE-Setup/ate12345 — always ON at 192.168.4.1
//   2. Optional router WiFi for email + ngrok remote access
//   3. 7-tab setup: Project / Test / DUTs / WiFi / Mode / Email / Remote
//   4. Live dashboard — fields update every 500ms without page refresh
//   5. 4x independent DUT — relay + opto per channel
//   6. Per-DUT: Pause / Resume / Stop / Reset with new name + schedule
//   7. Global Stop All / Reset All
//   8. Scheduled DUT start: now / +5m / +30m / +1h / custom / datetime
//   9. Overall elapsed time (freezes when all done)
//  10. Per-DUT elapsed time (freezes when that DUT done, resets on reset)
//  11. Crash resume — checkpoint after EVERY cycle (zero data loss)
//  12. ETA per DUT from rolling average cycle time
//  13. Last-10 PASS/FAIL sparkline — live update per DUT
//  14. 100-cycle PASS/FAIL block — live update
//  15. Boot time avg per DUT
//  17. Progress bar per DUT
//  18. USB 5V status live (via opto isolator)
//  19. Email retry queue — 3 attempts per email if WiFi temporarily down
//      Notifications: test start / boot error / DUT done / all done
//      Sender: ate.advantech@gmail.com  Port: 465 SSL
//  20. Per-DUT CSV download with full TEST SUMMARY at end of file
//  21. Test summary banner on completion — total PASS/FAIL, pass rate,
//      avg boot, total elapsed, outlier count
//  22. Scheduled DUT countdown — "WAIT 5m 30s" live in dashboard
//  23. RTC timestamps (DS3231) or software clock fallback
//  24. OLED 0.96" + OLED 0.91" status display
//  25. Buzzer — startup / start / error / done / pause / stop
//  26. Simulation mode — no relay or opto needed
//  27. Optional ngrok tunnel — view dashboard from internet
//  28. WiFi status pill — shows SSID + IP when connected
//  29. WiFi auto-reconnect every 30s if connection drops (non-blocking)
//  30. Auto page reload when all DUTs complete
//  31. Static IP support — fixed IP on lab router
//  32. Two preset WiFi networks selectable in setup form
//  33. /api/testemail — test email without running a test
//
// ================================================================
//  QUICK START
// ================================================================
//  1. Upload sketch with these Arduino IDE settings:
//       Board            : ESP32S3 Dev Module
//       Flash Size       : 16MB (128Mb)
//       Partition Scheme : 16M Flash (3MB APP/9.9MB FATFS)
//       PSRAM            : Disabled
//       USB CDC On Boot  : Enabled
//       Upload Speed     : 921600
//       Erase All Flash  : Enabled (first upload only, then Disabled)
//
//  2. Connect phone/laptop to WiFi: ATE-Setup  password: ate12345
//
//  3. Open browser: http://192.168.4.1
//
//  4. Fill setup tabs → click "Save and Start Test"
//
//  5. Dashboard link shown after start — works on ATE-Setup WiFi,
//     lab router WiFi, and internet (if ngrok enabled)
//
//  6. To test email without running: http://<IP>/api/testemail
//
// ================================================================
//  HARDWARE WIRING
// ================================================================
//  Relay CH1-4  : GPIO 4, 5, 16, 18  (HIGH=ON, LOW=OFF)
//                 PSU+ → COM,  NO → DUT power+
//  Opto CH1-4   : GPIO 6, 7, 17, 35  (INPUT_PULLUP, active LOW)
//                 DUT USB 5V → opto IN → opto OUT → GPIO
//  I2C-0 SDA/SCL: GPIO 8/9   — DS3231 RTC + OLED 0.96"
//  I2C-1 SDA/SCL: GPIO 10/11 — OLED 0.91"
//  Buzzer       : GPIO 2
//
// ================================================================
//  LIBRARIES  (Tools → Manage Libraries)
// ================================================================
//  Adafruit SSD1306, Adafruit GFX, RTClib, ArduinoJson v6.x
//  WebServer / FFat / WiFiClientSecure — built-in ESP32 core
//
#include <WiFi.h>
#include <WebServer.h>

#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <FFat.h>           // FFat for FATFS partition (16M Flash 3MB APP/9.9MB FATFS)
#include <math.h>           // F2: fabsf, sqrtf for boot outlier detection
#include <esp_log.h>        // for silencing WiFi SDK log spam
#include <esp_wifi.h>       // for esp_wifi_disconnect() — STA reset without AP drop

TwoWire I2C_1 = TwoWire(1);

// ── WiFi AP (always on) ───────────────────────────────────────
#define SETUP_SSID "ATE-Setup"
#define SETUP_PASS "ate12345"

// ── GPIO ──────────────────────────────────────────────────────
const int RELAY[4] = {4, 5, 16, 18};
const int OPTO[4]  = {6, 7, 17, 35};
#define I2C_SDA  8
#define I2C_SCL  9
#define I2C1_SDA 10
#define I2C1_SCL 11
#define BUZZER   2

// ── Hardware ──────────────────────────────────────────────────
RTC_DS3231       rtc;
Adafruit_SSD1306 oled1(128,64,&Wire,  -1);
Adafruit_SSD1306 oled2(128,32,&I2C_1,-1);
bool oled1OK=false, oled2OK=false, rtcOK=false;

// ── Config ────────────────────────────────────────────────────
char  cfgProject[32]    = "ATE";
char  cfgEngineer[32]   = "Karthikeyan";
char  cfgWifiSSID[64]   = "ADVANTECH-GUEST";  // default — editable in WiFi tab
char  cfgWifiPass[64]   = "Advantech66!";     // default — editable in WiFi tab
// Preset WiFi 2 — Karthik home network
#define WIFI2_SSID "Karthik.de"
#define WIFI2_PASS "karthik.er"
#define WIFI3_SSID "MagentaWLAN-BVBC"
#define WIFI3_PASS "82542166895510202173"
// Hardcoded ngrok authtoken — always available without entering in setup
#define NGROK_TOKEN_DEFAULT "3CH5Qi4m8pHTKw4Conh228VZteV_3L5ZzorDBYwXZhLJ7Cq8G"
// Static IP for lab WiFi — set all to 0.0.0.0 to use DHCP (dynamic)
// If set, ESP32 always gets same IP on lab WiFi regardless of reboots
char  cfgStaticIP[16]   = "0.0.0.0";   // e.g. "192.168.39.100" or "0.0.0.0" for DHCP
char  cfgGateway[16]    = "0.0.0.0";   // e.g. "192.168.39.1"
char  cfgSubnet[16]     = "255.255.255.0";
int   cfgCycles         = 1000;
int   cfgBootTO         = 120;
int   cfgOffGap         = 15;
int   cfgMaxRetry       = 0;
int   cfgDutCount       = 1;
bool  cfgDutEn[4]       = {false,false,false,false};
char  cfgDutName[4][32] = {"DUT1","DUT2","DUT3","DUT4"};
unsigned long cfgDelayMs[4] = {0,0,0,0};
bool  cfgSimMode = false;
int   cfgSimBoot = 5;
int   cfgSimRun  = 5;
bool  cfgBuzzer  = true;   // global buzzer on/off — silenced when false

// ── Email ─────────────────────────────────────────────────────
char cfgEmailSMTP[64]  = "smtp.gmail.com";
int  cfgEmailPort      = 465;   // SSL direct — more reliable on ESP32
char cfgEmailUser[64]  = "ate.advantech@gmail.com";
char cfgEmailPass[64]  = "wtqrypownonovwdz"; // Gmail app password (no spaces)
char cfgEmailTo[64]    = "karthikeyan.thirunavukarasu@advantech.com";
bool cfgEmailEnable    = true;  // enabled by default with above credentials
bool cfgEmailOnStart   = true;
bool cfgEmailOnError   = true;
bool cfgEmailOnDutDone = true;
bool cfgEmailOnAllDone = true;

// ── ngrok (optional remote access) ───────────────────────────
bool cfgNgrokEnable    = false;
char cfgNgrokToken[128]= "";
char cfgNgrokDomain[128]="";
String ngrokURL        = "";
bool   ngrokRunning    = false;

// ── Runtime ───────────────────────────────────────────────────
bool   testRunning    = false;
bool   announcedDone  = false;
bool   resumedFromChk = false;
String espIP          = "192.168.4.1";
unsigned long testStartMs = 0;
unsigned long testEndMs   = 0;   // set when all DUTs complete — freezes elapsed
static long   epochOffset = 0;
static long   tzOffsetSec = 0;  // timezone offset in seconds (e.g. +7200 for UTC+2)

// ── DUT struct ────────────────────────────────────────────────
struct DUT {
  bool  enabled    = false;
  bool  done       = false;
  bool  stopped    = false;
  bool  paused     = false;
  bool  waiting    = false;
  int   cycle      = 1;
  int   completed  = 0;
  int   pass       = 0;
  int   errors     = 0;
  int   retries    = 0;
  bool  usbActive  = false;
  bool  usbRaw     = false;
  unsigned long debMs        = 0;
  unsigned long stateMs      = 0;
  unsigned long startMs      = 0;
  unsigned long cycleStartMs = 0;
  unsigned long dutStartMs      = 0;  // when this DUT started cycling (millis())
  unsigned long dutElapsedMs    = 0;  // frozen elapsed when DUT completes (ms)
  unsigned long dutPausedTotalMs= 0;  // total time spent paused (ms)
  unsigned long dutPauseStartMs = 0;  // when current pause began (ms)
  // F8: Wall-clock timestamps for reports
  char dutStartTime[20] = "";      // RTC string when DUT started, e.g. "2026-05-10 16:30:45"
  char dutEndTime[20]   = "";      // RTC string when DUT finished
  char  state[16]  = "IDLE";
  float avgBoot    = 0;
  int   bootCount  = 0;
  float avgCycleS  = 0;
  float avgCycleMs = 0;
  int8_t last10[10];
  int    last10idx = 0;
  int    chkPass   = 0;
  int    chkFail   = 0;
  int    chkBlock  = 0;
  // Feature 2: Boot time stddev for outlier detection
  float  bootStddev = 0;
  int    bootOutliers = 0;  // count of rejected outliers
} duts[4];

// Feature 3: Email retry queue (max 8 pending)
struct EmailQueue {
  bool   active = false;
  char   subj[80];
  char   body[600];
  int    attempts = 0;
  unsigned long nextRetryMs = 0;
};
EmailQueue emailQueue[8];

WebServer server(80);
unsigned long lastOledMs     = 0;
unsigned long lastChkpointMs = 0;
bool pendingCsvSave[4]  = {false,false,false,false};  // deferred saveDutCsv
bool pendingConfigSave  = false;                      // deferred saveConfig after reset
int  dutTargetCycles[4] = {1000,1000,1000,1000};     // per-DUT cycle target
bool fsOK = false;   // set true after FFat.begin succeeds

// ── Forward declarations ──────────────────────────────────────
String rtcNow();
void   logEvent(int,const char*,const char*,int);
void   saveCheckpoint();
void   loadCheckpoint();
void   saveConfig();
void   loadSavedConfig();
void   runDUT(int);
void   updateOLEDs();
bool   readOpto(int);
bool   sendEmail(const char*,const char*);
void   queueEmail(const char*,const char*);
void   processEmailQueue();
void   emailTestStart();
void   emailDutDone(int);
void   emailAllDone();
void   emailError(int,int);
void   saveDutCsv(int);
bool   startNgrokTunnel();
void   stopNgrokTunnel();
void   handleSetup();
void   handleDashboard();
void   handleConfig();
void   handleApiStatus();
void   handleApiCmd();
void   handleApiLog();
void   handleLog();

// ================================================================
//  HELPERS
// ================================================================
String elapsedStr(unsigned long ms) {
  unsigned long s=ms/1000;
  unsigned long h=s/3600, m=(s%3600)/60, sec=s%60;
  char buf[20];
  if(h>0) snprintf(buf,sizeof(buf),"%luh %02lum %02lus",h,m,sec);
  else     snprintf(buf,sizeof(buf),"%lum %02lus",m,sec);
  return String(buf);
}

// ── Buzzer ────────────────────────────────────────────────────
void _b(int f,int d){if(!cfgBuzzer)return;tone(BUZZER,f,d);delay(d);noTone(BUZZER);}
void startupBeep()  {_b(988,80);delay(10);_b(1319,200);}
void testStartBeep(){_b(523,100);delay(30);_b(659,100);delay(30);_b(784,200);}
void pauseBeep()    {_b(600,200);}
void resumeBeep()   {_b(880,80);delay(60);_b(880,80);}
void stopBeep()     {_b(600,300);}
void errorBeep()    {_b(494,180);delay(20);_b(440,180);delay(20);_b(415,180);delay(20);_b(370,480);}
void dutDoneBeep()  {_b(784,120);delay(20);_b(1047,300);}
void successBeep()  {int n[]={784,523,659,784,659,784},d[]={120,120,120,120,120,300};
                     for(int i=0;i<6;i++){_b(n[i],d[i]);delay(20);}}

// ── RTC ───────────────────────────────────────────────────────
String rtcNow(){
  if(rtcOK){
    DateTime now=rtc.now();
    char buf[20];
    snprintf(buf,sizeof(buf),"%04d-%02d-%02d %02d:%02d:%02d",
             now.year(),now.month(),now.day(),now.hour(),now.minute(),now.second());
    return String(buf);
  }
  if(epochOffset>0){
    time_t t=(time_t)(epochOffset+(long)(millis()/1000)+tzOffsetSec);
    struct tm* ti=gmtime(&t);  // UTC+tzOffset = local time
    char buf[20];strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",ti);
    return String(buf);
  }
  unsigned long s=millis()/1000;
  char buf[20];
  snprintf(buf,sizeof(buf),"+%02lu:%02lu:%02lu",s/3600,(s%3600)/60,s%60);
  return String(buf);
}

// ── Opto debounce ─────────────────────────────────────────────
bool readOpto(int i){
  if(cfgSimMode){
    unsigned long el=millis()-duts[i].stateMs;
    if(strcmp(duts[i].state,"WAIT_BOOT")==0) return(el>=(unsigned long)cfgSimBoot*1000UL);
    if(strcmp(duts[i].state,"RUNNING")==0)   return(el< (unsigned long)cfgSimRun *1000UL);
    return false;
  }
  bool raw=(digitalRead(OPTO[i])==LOW);
  // Debounce: 50ms for fast USB response
  if(raw!=duts[i].usbRaw){duts[i].usbRaw=raw;duts[i].debMs=millis();}
  if(millis()-duts[i].debMs>=50) duts[i].usbActive=duts[i].usbRaw;
  // In WAIT_BOOT: also require minimum 2s after relay ON before accepting USB active
  // This prevents floating GPIO from triggering a false boot detect
  if(strcmp(duts[i].state,"WAIT_BOOT")==0){
    if(millis()-duts[i].stateMs < 500) return false;  // 500ms min after relay ON
  }
  return duts[i].usbActive;
}

// ── Log ───────────────────────────────────────────────────────
void logEvent(int i,const char* evt,const char* res,int cycle){
  if(!fsOK) return;
  File f=FFat.open("/log.csv","a");
  if(!f) return;
  f.printf("%s,DUT%d,%s,%d,%s,%s\n",rtcNow().c_str(),i+1,cfgDutName[i],cycle,evt,res);
  f.close();
}

// ── Per-DUT CSV with F6: summary row at the end ──────────────
void saveDutCsv(int idx){
  char fname[16]; snprintf(fname,sizeof(fname),"/dut%d.csv",idx+1);
  File out=FFat.open(fname,"w");
  if(!out) return;
  out.println("timestamp,dut,name,cycle,event,result");
  String tag="DUT"+String(idx+1);
  File in=FFat.open("/log.csv","r");
  if(in){
    while(in.available()){
      String line=in.readStringUntil('\n');line.trim();
      if(line.length()==0) continue;
      int c1=line.indexOf(','),c2=line.indexOf(',',c1+1);
      if(c1>=0&&c2>c1&&line.substring(c1+1,c2)==tag) out.println(line);
    }
    in.close();
  }
  // F6: Append summary row at the end
  out.println();
  out.println("=== TEST SUMMARY ===");
  out.printf("DUT,%d\n",idx+1);
  out.printf("Name,%s\n",cfgDutName[idx]);
  out.printf("Project,%s\n",cfgProject);
  out.printf("Engineer,%s\n",cfgEngineer);
  out.printf("Total cycles configured,%d\n",cfgCycles);
  out.printf("Cycles completed,%d\n",duts[idx].completed);
  out.printf("PASS,%d\n",duts[idx].pass);
  out.printf("FAIL,%d\n",duts[idx].errors);
  if(duts[idx].completed>0){
    out.printf("PASS rate,%.2f%%\n",duts[idx].pass*100.0/duts[idx].completed);
  }
  out.printf("Avg boot time (s),%.2f\n",duts[idx].avgBoot);
  out.printf("Boot stddev (s),%.2f\n",duts[idx].bootStddev);
  out.printf("Boot outliers rejected,%d\n",duts[idx].bootOutliers);
  out.printf("Avg cycle time (s),%.2f\n",duts[idx].avgCycleS);
  out.printf("Test start time,%s\n",(duts[idx].dutStartTime[0]&&duts[idx].dutStartTime[0]!='+')?duts[idx].dutStartTime:"-");
  out.printf("Test end time,%s\n",duts[idx].dutEndTime[0]?duts[idx].dutEndTime:"-");
  out.printf("Total elapsed time,%s\n",elapsedStr(duts[idx].dutElapsedMs).c_str());
  out.printf("Status,%s\n",duts[idx].stopped?"STOPPED":"COMPLETED");
  out.close();
}

// ================================================================
//  CHECKPOINT
// ================================================================
void saveCheckpoint(){
  if(!fsOK) return;
  StaticJsonDocument<2048> doc;
  // Global config
  doc["project"]   = cfgProject;   doc["engineer"]  = cfgEngineer;
  doc["cycles"]    = cfgCycles;    doc["boot_to"]   = cfgBootTO;
  doc["off_gap"]   = cfgOffGap;    doc["max_retry"] = cfgMaxRetry;
  doc["dut_count"] = cfgDutCount;  doc["running"]   = testRunning;
  doc["sim_mode"]  = cfgSimMode;   doc["buzzer"]    = cfgBuzzer;
  doc["epochOffset"] = epochOffset;
  doc["tzOffset"]    = tzOffsetSec;
  doc["elapsed_s"] = testRunning?(millis()-testStartMs)/1000:0;
  JsonArray arr = doc.createNestedArray("duts");
  for(int i=0;i<4;i++){
    JsonObject d = arr.createNestedObject();
    // Identity
    d["en"]       = cfgDutEn[i];
    d["name"]     = cfgDutName[i];
    d["delay_ms"] = cfgDelayMs[i];
    // Counters
    d["completed"]  = duts[i].completed;
    d["pass"]       = duts[i].pass;
    d["errors"]     = duts[i].errors;
    d["cycle"]      = duts[i].cycle;
    d["done"]       = duts[i].done;
    d["stopped"]    = duts[i].stopped;
    // Block tracker (100-cycle blocks)
    d["chkBlock"]   = duts[i].chkBlock;
    d["chkPass"]    = duts[i].chkPass;
    d["chkFail"]    = duts[i].chkFail;
    // Boot stats
    d["avgBoot"]    = duts[i].bootCount>0 ? duts[i].avgBoot : 0.0f;
    d["bootCount"]  = duts[i].bootCount;
    d["bootStddev"] = duts[i].bootStddev;
    d["bootOutliers"]= duts[i].bootOutliers;
    d["avgCycleMs"] = duts[i].avgCycleMs;
    // Last 10 sparkline as string e.g. "1,0,1,1,e,e,e,e,e,e"
    {
      char sp[24]=""; int pos=0;
      int n10=min(duts[i].last10idx,10);
      for(int j=0;j<10;j++){
        if(j>0) sp[pos++]=',';
        if(j>=n10) sp[pos++]='e';
        else sp[pos++]=(char)('0'+duts[i].last10[(duts[i].last10idx-n10+j)%10]);
      }
      sp[pos]=0; d["last10"]=sp;
    }
    // Timestamps
    d["startTime"] = duts[i].dutStartTime;
    d["endTime"]   = duts[i].dutEndTime;
    // Active elapsed (excludes paused time)
    if(duts[i].dutStartMs>0){
      unsigned long pausedSoFar = duts[i].dutPausedTotalMs;
      if(duts[i].paused && duts[i].dutPauseStartMs>0)
        pausedSoFar += millis()-duts[i].dutPauseStartMs;
      d["dutElapsed"] = (int32_t)((millis()-duts[i].dutStartMs-pausedSoFar)/1000);
    } else { d["dutElapsed"] = 0; }
    d["pausedTotal"] = (int32_t)(duts[i].dutPausedTotalMs/1000);
  }
  File f = FFat.open("/checkpoint.json","w");
  if(f){ serializeJson(doc,f); f.close(); }
}

void loadCheckpoint(){
  for(int i=0;i<4;i++) digitalWrite(RELAY[i],LOW);
  if(!fsOK) return;
  if(!FFat.exists("/checkpoint.json")) return;
  File f=FFat.open("/checkpoint.json","r");
  if(!f) return;
  StaticJsonDocument<1024> doc;
  if(deserializeJson(doc,f)){f.close();return;}
  f.close();
  if(!doc["running"].as<bool>()) return;
  JsonArray arr=doc["duts"];
  if(!arr) return;
  bool anyActive=false;
  for(JsonObject d:arr) if((d["en"]|false)&&!(d["done"]|false)){anyActive=true;break;}
  if(!anyActive){FFat.remove("/checkpoint.json");return;}
  // Restore global config
  strlcpy(cfgProject,  doc["project"] |"ATE",      sizeof(cfgProject));
  strlcpy(cfgEngineer, doc["engineer"]|"Engineer",  sizeof(cfgEngineer));
  cfgCycles   = doc["cycles"]    | 1000;
  cfgBootTO   = doc["boot_to"]   | 120;
  cfgOffGap   = doc["off_gap"]   | 10;
  cfgMaxRetry = doc["max_retry"] | 0;
  cfgDutCount = doc["dut_count"] | 1;
  cfgSimMode  = doc["sim_mode"]  | false;
  cfgBuzzer   = doc["buzzer"]    | true;
  if(doc.containsKey("epochOffset")){
    epochOffset=(long)doc["epochOffset"];
    if(doc.containsKey("tzOffset")) tzOffsetSec=(long)doc["tzOffset"];
    Serial.printf("[RTC] Restored: %s (tz=%+lds)\n",rtcNow().c_str(),tzOffsetSec);
  }

  int idx=0;
  for(JsonObject d : arr){
    if(idx>=4) break;
    cfgDutEn[idx] = d["en"] | false;
    strlcpy(cfgDutName[idx], d["name"]|"DUT", sizeof(cfgDutName[0]));
    cfgDelayMs[idx] = d["delay_ms"] | 0;

    if(cfgDutEn[idx]){
      duts[idx] = DUT();
      duts[idx].enabled = true;

      // Counters
      duts[idx].completed = d["completed"] | 0;
      duts[idx].pass      = d["pass"]      | 0;
      duts[idx].errors    = d["errors"]    | 0;
      duts[idx].cycle     = d["cycle"]     | 1;
      duts[idx].done      = d["done"]      | false;
      duts[idx].stopped   = d["stopped"]   | false;

      // 100-cycle block
      duts[idx].chkBlock  = d["chkBlock"]  | 0;
      duts[idx].chkPass   = d["chkPass"]   | 0;
      duts[idx].chkFail   = d["chkFail"]   | 0;

      // Boot stats
      duts[idx].avgBoot      = d["avgBoot"]     | 0.0f;
      duts[idx].bootCount    = d["bootCount"]   | 0;
      duts[idx].bootStddev   = d["bootStddev"]  | 0.0f;
      duts[idx].bootOutliers = d["bootOutliers"]| 0;
      duts[idx].avgCycleMs   = d["avgCycleMs"]  | 0.0f;

      // Restore last10 sparkline from saved string
      const char* sp = d["last10"] | "";
      duts[idx].last10idx = 0;
      for(int j=0; j<10 && sp[j*2]!=0; j++){
        char c = sp[j>0 ? j*2 : 0];
        if(j>0){ int ci=j*2; c=sp[ci]; }
        // parse comma-separated string
      }
      // Simple restore: re-parse the string
      {
        String s10 = String(sp);
        int pos=0, ji=0;
        while(ji<10){
          int comma = s10.indexOf(',',pos);
          String tok = (comma>=0) ? s10.substring(pos,comma) : s10.substring(pos);
          if(tok!="e" && tok.length()>0){
            duts[idx].last10[ji%10] = tok.toInt();
            duts[idx].last10idx++;
          }
          ji++;
          if(comma<0) break;
          pos=comma+1;
        }
      }

      // Timestamps
      const char* st = d["startTime"] | "";
      const char* et = d["endTime"]   | "";
      strlcpy(duts[idx].dutStartTime, st, sizeof(duts[idx].dutStartTime));
      strlcpy(duts[idx].dutEndTime,   et, sizeof(duts[idx].dutEndTime));

      // Restore active elapsed (dutStartMs = synthetic millis anchor)
      unsigned long dutEl = d["dutElapsed"] | 0;
      unsigned long pausedS = d["pausedTotal"] | 0;
      duts[idx].dutPausedTotalMs = pausedS * 1000UL;
      // Set dutStartMs so that (millis()-dutStartMs)-pausedTotal == dutEl
      duts[idx].dutStartMs = (dutEl>0) ? (millis() - dutEl*1000UL - duts[idx].dutPausedTotalMs) : millis();

      // Restore state
      if(duts[idx].stopped)     strcpy(duts[idx].state, "STOPPED");
      else if(duts[idx].done)   strcpy(duts[idx].state, "DONE");
      else                      strcpy(duts[idx].state, "RELAY_ON");  // resume cycling
    }
    idx++;
  }
  unsigned long el=doc["elapsed_s"]|0;
  testStartMs=millis()-el*1000UL;
  testRunning=true; announcedDone=false; resumedFromChk=true;
  Serial.printf("[RESUME] %s  elapsed:%lus\n",cfgProject,el);
  testStartBeep();
}

// ================================================================
//  CONFIG SAVE/LOAD
// ================================================================
void saveConfig(){
  if(!fsOK) return;
  StaticJsonDocument<512> doc;
  doc["project"]=cfgProject; doc["engineer"]=cfgEngineer;
  doc["wifi_ssid"]=cfgWifiSSID; doc["wifi_pass"]=cfgWifiPass;
  doc["static_ip"]=cfgStaticIP; doc["gateway"]=cfgGateway; doc["subnet"]=cfgSubnet;
  doc["cycles"]=cfgCycles; doc["boot_to"]=cfgBootTO;
  doc["off_gap"]=cfgOffGap; doc["max_retry"]=cfgMaxRetry;
  doc["dut_count"]=cfgDutCount;
  doc["email_smtp"]=cfgEmailSMTP; doc["email_port"]=cfgEmailPort;
  doc["buzzer"]=cfgBuzzer;
  doc["email_user"]=cfgEmailUser; doc["email_pass"]=cfgEmailPass;
  doc["email_to"]=cfgEmailTo;
  doc["ngrok_enable"]=cfgNgrokEnable;
  doc["ngrok_token"]=cfgNgrokToken; doc["ngrok_domain"]=cfgNgrokDomain;
  JsonArray arr=doc.createNestedArray("duts");
  for(int i=0;i<4;i++){JsonObject d=arr.createNestedObject();d["name"]=cfgDutName[i];}
  File f=FFat.open("/lastconfig.json","w");
  if(f){serializeJson(doc,f);f.close();}
}

void loadSavedConfig(){
  if(!fsOK) return;
  if(!FFat.exists("/lastconfig.json")) return;
  File f=FFat.open("/lastconfig.json","r");
  if(!f) return;
  StaticJsonDocument<512> doc;
  if(deserializeJson(doc,f)){f.close();return;}
  f.close();
  strlcpy(cfgProject, doc["project"] |"ATE",           sizeof(cfgProject));
  strlcpy(cfgEngineer,doc["engineer"]|"Engineer",       sizeof(cfgEngineer));
  strlcpy(cfgWifiSSID,  doc["wifi_ssid"] |"ADVANTECH-GUEST", sizeof(cfgWifiSSID));
  strlcpy(cfgWifiPass,  doc["wifi_pass"] |"Advantech66!",   sizeof(cfgWifiPass));
  strlcpy(cfgStaticIP,  doc["static_ip"] |"0.0.0.0",        sizeof(cfgStaticIP));
  strlcpy(cfgGateway,   doc["gateway"]   |"0.0.0.0",        sizeof(cfgGateway));
  strlcpy(cfgSubnet,    doc["subnet"]    |"255.255.255.0",   sizeof(cfgSubnet));
  cfgCycles=doc["cycles"]|1000; cfgBootTO=doc["boot_to"]|120;
  cfgOffGap=doc["off_gap"]|15;  cfgMaxRetry=doc["max_retry"]|0;
  cfgDutCount=doc["dut_count"]|1;
  strlcpy(cfgEmailSMTP,doc["email_smtp"]|"smtp.gmail.com",sizeof(cfgEmailSMTP));
  cfgEmailPort=doc["email_port"]|465;
  cfgBuzzer=doc["buzzer"]|true;
  strlcpy(cfgEmailUser,doc["email_user"]|"ate.advantech@gmail.com",sizeof(cfgEmailUser));
  strlcpy(cfgEmailPass,doc["email_pass"]|"wtqrypownonovwdz",sizeof(cfgEmailPass));
  strlcpy(cfgEmailTo,  doc["email_to"]  |"karthikeyan.thirunavukarasu@advantech.com",sizeof(cfgEmailTo));
  cfgNgrokEnable=doc["ngrok_enable"]|false;
  strlcpy(cfgNgrokToken, doc["ngrok_token"] |"",sizeof(cfgNgrokToken));
  strlcpy(cfgNgrokDomain,doc["ngrok_domain"]|"",sizeof(cfgNgrokDomain));
  JsonArray arr=doc["duts"];
  if(arr){int i=0;for(JsonObject d:arr){if(i>=4)break;strlcpy(cfgDutName[i],d["name"]|"DUT",sizeof(cfgDutName[0]));i++;}}
}

// ================================================================
//  DUT STATE MACHINE
// ================================================================
void runDUT(int i){
  DUT& d=duts[i];
  if(!d.enabled||d.done||d.paused||d.stopped) return;
  unsigned long now=millis();

  if(d.waiting){
    if(now>=d.startMs){
      d.waiting=false; d.dutStartMs=now;
      strcpy(d.state,"RELAY_ON");
      logEvent(i,"SCHEDULED_START","OK",d.cycle);
    } else strcpy(d.state,"SCHEDULED");
    return;
  }

  if(strcmp(d.state,"RELAY_ON")==0){
    if(!cfgSimMode) digitalWrite(RELAY[i],HIGH);
    d.stateMs=now;
    d.cycleStartMs=now;  // always reset cycle timer at relay ON
    if(d.dutStartMs==0){
      d.dutStartMs=now;
      // F8: record wall-clock start time (first time this DUT runs)
      if(d.dutStartTime[0]==0) strlcpy(d.dutStartTime, rtcNow().c_str(), sizeof(d.dutStartTime));
    }
    logEvent(i,"RELAY_ON","OK",d.cycle);
    strcpy(d.state,"WAIT_BOOT");
    return;
  }

  if(strcmp(d.state,"WAIT_BOOT")==0){
    if(readOpto(i)){
      float bt=(now-d.stateMs)/1000.0f;
      // Simple boot stats (no outlier filter — removed for speed)
      d.bootCount++;
      d.avgBoot=(d.avgBoot*(d.bootCount-1)+bt)/d.bootCount;
      // USB_DETECTED event removed — reduces flash writes per cycle
      strcpy(d.state,"RUNNING"); d.stateMs=now;
    } else if(now-d.stateMs>(unsigned long)cfgBootTO*1000UL){
      // ── RELAY BUG FIX ───────────────────────────────────────
      // maxRetry=0 → count every timeout as FAIL, always advance
      // maxRetry=N → retry silently N times, then count as FAIL
      if(!cfgSimMode) digitalWrite(RELAY[i],LOW);
      d.retries++;
      bool countAsFail=(cfgMaxRetry==0)||(d.retries>=cfgMaxRetry);
      if(countAsFail){
        logEvent(i,"BOOT_TIMEOUT","FAIL",d.cycle);
        d.errors++; d.chkFail++;
        d.last10[d.last10idx%10]=0; d.last10idx++;
        d.retries=0; errorBeep();
        if(cfgEmailEnable && d.errors==1) emailError(i,d.cycle);  // only first error
        d.completed++; d.cycle++;
        if(d.completed%100==0){d.chkBlock++;d.chkPass=0;d.chkFail=0;}
        if(d.completed>=dutTargetCycles[i]){
          strcpy(d.state,"DONE"); d.done=true; dutDoneBeep();
          if(d.dutStartMs>0){
            d.dutElapsedMs=(now-d.dutStartMs)-d.dutPausedTotalMs;
          }
          strlcpy(d.dutEndTime, rtcNow().c_str(), sizeof(d.dutEndTime));
          pendingCsvSave[i]=true;  // defer CSV write
          if(cfgEmailEnable) emailDutDone(i);
        } else {strcpy(d.state,"WAIT_GAP");d.stateMs=now;}
        // F1: save at done state always; otherwise every 10 cycles
        if(d.done || d.completed%10==0) saveCheckpoint();
      } else {
        logEvent(i,"BOOT_TIMEOUT","RETRYING",d.cycle);
        strcpy(d.state,"WAIT_GAP"); d.stateMs=now;
      }
    }
    return;
  }

  if(strcmp(d.state,"RUNNING")==0){
    if(!readOpto(i)){
      unsigned long fc=d.cycleStartMs>0?(now-d.cycleStartMs):(now-d.stateMs);
      // Clamp cycle time to realistic range (1s min, 1 hour max)
      if(fc > 0 && fc < 3600000UL){
        if(d.avgCycleMs==0) d.avgCycleMs=fc;
        else d.avgCycleMs=d.avgCycleMs*0.85f+(float)fc*0.15f;
        d.avgCycleS=d.avgCycleMs/1000.0f;
      }
      logEvent(i,"USB_DROPPED","OK",d.cycle);
      if(!cfgSimMode) digitalWrite(RELAY[i],LOW);
      logEvent(i,"CYCLE_COMPLETE","PASS",d.cycle);
      d.pass++; d.chkPass++;
      d.last10[d.last10idx%10]=1; d.last10idx++;
      d.retries=0; d.completed++; d.cycle++;
      if(d.completed%100==0){d.chkBlock++;d.chkPass=0;d.chkFail=0;}
      if(d.completed>=dutTargetCycles[i]){
        strcpy(d.state,"DONE"); d.done=true; dutDoneBeep();
        if(d.dutStartMs>0){
          d.dutElapsedMs=(now-d.dutStartMs)-d.dutPausedTotalMs;
        }
        strlcpy(d.dutEndTime, rtcNow().c_str(), sizeof(d.dutEndTime));
        pendingCsvSave[i]=true;  // defer CSV write
        if(cfgEmailEnable) emailDutDone(i);
      } else {strcpy(d.state,"WAIT_GAP");d.stateMs=now;}
      // F1: save checkpoint at done state + every 10 cycles (not every cycle — too slow)
      if(d.done || d.completed%10==0) saveCheckpoint();
    }
    return;
  }

  if(strcmp(d.state,"WAIT_GAP")==0){
    if(now-d.stateMs>=(unsigned long)cfgOffGap*1000UL)
      strcpy(d.state,"RELAY_ON");
    return;
  }
}

// ================================================================
//  OLED
// ================================================================
int totalPassCount(){
  int n=0; for(int i=0;i<4;i++) n+=duts[i].pass; return n;
}

void updateOLEDs(){
  if(oled1OK){
    oled1.clearDisplay();
    oled1.setTextColor(SSD1306_WHITE);
    oled1.setTextSize(1);
    if(!testRunning){
      oled1.setCursor(0,0);  oled1.print("ATE Standalone V2");
      oled1.setCursor(0,10); oled1.print("AP: " SETUP_SSID);
      oled1.setCursor(0,20); oled1.print("PW: " SETUP_PASS);
      oled1.setCursor(0,30); oled1.print("192.168.4.1");
      if(ngrokRunning&&ngrokURL.length()>0){
        String u=ngrokURL; u.replace("https://","");
        if(u.length()>21) u=u.substring(0,21);
        oled1.setCursor(0,42); oled1.print(u);
      }
      if(rtcOK){oled1.setCursor(0,54);oled1.print(rtcNow().substring(11));}
    } else {
      oled1.setCursor(0,0); oled1.print("ATE "); oled1.print(cfgProject);
      if(rtcOK){String t=rtcNow().substring(11,19);oled1.setCursor(128-t.length()*6,0);oled1.print(t);}
      oled1.drawLine(0,9,127,9,SSD1306_WHITE);
      int y=12;
      for(int i=0;i<4&&y<64;i++){
        if(!duts[i].enabled) continue;
        int pct=cfgCycles>0?min(100,duts[i].completed*100/cfgCycles):0;
        char st[6]=""; strncpy(st,duts[i].state,5); st[5]=0;
        char buf[22];
        snprintf(buf,sizeof(buf),"D%d %-5s %3d/%3d %3d%%",i+1,st,duts[i].completed,cfgCycles,pct);
        oled1.setCursor(0,y); oled1.print(buf);
        if(duts[i].usbActive) oled1.fillCircle(124,y+3,3,SSD1306_WHITE);
        else                   oled1.drawCircle(124,y+3,3,SSD1306_WHITE);
        y+=13;
      }
    }
    oled1.display();
  }
  if(oled2OK){
    oled2.clearDisplay();
    oled2.setTextColor(SSD1306_WHITE);
    oled2.setTextSize(1);
    oled2.setCursor(0,0);
    if(!testRunning){
      oled2.print("Advantech ATE V2");
      oled2.setCursor(0,11); oled2.print("192.168.4.1");
    } else {
      oled2.print(cfgProject);
      for(int i=0;i<4;i++){
        if(!duts[i].enabled) continue;
        int fill=cfgCycles>0?(duts[i].completed*20/cfgCycles):0;
        oled2.setCursor(0,12);
        for(int j=0;j<20;j++) oled2.print(j<fill?"|":".");
        char s[24];
        if(duts[i].avgCycleMs>0&&duts[i].completed<cfgCycles){
          unsigned long rem=(unsigned long)(cfgCycles-duts[i].completed)*(unsigned long)duts[i].avgCycleMs;
          snprintf(s,sizeof(s),"P:%d E:%d ETA:%lum",duts[i].pass,duts[i].errors,rem/60000);
        } else snprintf(s,sizeof(s),"P:%d E:%d",duts[i].pass,duts[i].errors);
        oled2.setCursor(0,22); oled2.print(s);
        break;
      }
    }
    oled2.display();
  }
}

// ================================================================
//  EMAIL  (port 465=SSL direct, port 587=STARTTLS)
//  App password: enter WITHOUT spaces (16 chars, no gaps)
// ================================================================
static String b64c="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String b64enc(const char* in){
  String o=""; int len=strlen(in);
  for(int i=0;i<len;i+=3){
    uint32_t b=((uint32_t)(uint8_t)in[i])<<16;
    if(i+1<len) b|=((uint32_t)(uint8_t)in[i+1])<<8;
    if(i+2<len) b|=(uint8_t)in[i+2];
    o+=b64c[(b>>18)&0x3F]; o+=b64c[(b>>12)&0x3F];
    o+=(i+1<len)?b64c[(b>>6)&0x3F]:'=';
    o+=(i+2<len)?b64c[b&0x3F]:'=';
  }
  return o;
}
String sRecv(WiFiClient& c){
  unsigned long t=millis(); String r="";
  while(!c.available()&&millis()-t<8000) delay(10);
  while(c.available()){r+=(char)c.read();delay(1);}
  r.trim(); Serial.println("[SMTP]<< "+r); return r;
}
String sRecvS(WiFiClientSecure& c){
  unsigned long t=millis(); String r="";
  while(!c.available()&&millis()-t<8000) delay(10);
  while(c.available()){r+=(char)c.read();delay(1);}
  r.trim(); Serial.println("[SMTP]<< "+r); return r;
}
void sSend(WiFiClient& c,const String& s){Serial.println("[SMTP]>> "+s);c.println(s);}
void sSendS(WiFiClientSecure& c,const String& s){Serial.println("[SMTP]>> "+s);c.println(s);}

void smtpWriteMsg(WiFiClientSecure& sc,const char* subj,const char* body){
  sc.println(String("From: ATE Standalone <")+cfgEmailUser+">");
  sc.println(String("To: ")+cfgEmailTo);
  sc.println(String("Subject: ")+subj);
  sc.println("MIME-Version: 1.0");
  sc.println("Content-Type: text/plain; charset=UTF-8");
  sc.println(); sc.println(body);
}

bool sendEmail(const char* subj, const char* body){
  if(!cfgEmailEnable || strlen(cfgEmailSMTP)==0){
    Serial.println("[EMAIL] Disabled or no SMTP server");
    return false;
  }
  if(WiFi.status()!=WL_CONNECTED){
    Serial.println("[EMAIL] No WiFi — skipping");
    return false;
  }
  // Always use port 465 SSL — most reliable on ESP32
  // Gmail: smtp.gmail.com:465 works without STARTTLS complexity
  int smtpPort = 465;
  Serial.printf("[EMAIL] %s:%d user=%s\n", cfgEmailSMTP, smtpPort, cfgEmailUser);

  WiFiClientSecure sc;
  sc.setInsecure();
  sc.setTimeout(20);

  if(!sc.connect(cfgEmailSMTP, smtpPort)){
    Serial.printf("[EMAIL] Cannot connect to %s:%d\n", cfgEmailSMTP, smtpPort);
    return false;
  }
  Serial.println("[EMAIL] TCP connected");

  // Read greeting
  String r = sRecvS(sc);
  if(!r.startsWith("2")){
    Serial.println("[EMAIL] Bad server greeting: "+r);
    sc.stop(); return false;
  }
  sSendS(sc, "EHLO ate-esp32");       r = sRecvS(sc);
  sSendS(sc, "AUTH LOGIN");           r = sRecvS(sc);
  if(!r.startsWith("3")){
    Serial.println("[EMAIL] AUTH LOGIN rejected: "+r);
    sc.stop(); return false;
  }
  sSendS(sc, b64enc(cfgEmailUser));   r = sRecvS(sc);
  if(!r.startsWith("3")){
    Serial.println("[EMAIL] Username rejected: "+r);
    sc.stop(); return false;
  }
  sSendS(sc, b64enc(cfgEmailPass));   r = sRecvS(sc);
  if(!r.startsWith("2")){
    Serial.println("[EMAIL] Password rejected — check app password has NO spaces: "+r);
    sc.stop(); return false;
  }
  Serial.println("[EMAIL] Auth OK");
  sSendS(sc, String("MAIL FROM:<")+cfgEmailUser+">"); r = sRecvS(sc);
  sSendS(sc, String("RCPT TO:<")+cfgEmailTo+">");     r = sRecvS(sc);
  sSendS(sc, "DATA");                                  r = sRecvS(sc);
  smtpWriteMsg(sc, subj, body);
  sSendS(sc, ".");                                     r = sRecvS(sc);
  sSendS(sc, "QUIT");
  sc.stop();
  if(r.startsWith("2")){
    Serial.println("[EMAIL] Sent OK ✓");
    return true;
  } else {
    Serial.println("[EMAIL] Server rejected message: "+r);
    return false;
  }
}
// ── F3: Email retry queue ──────────────────────────────────────
// Queue an email for sending. Tries 3 times with 10s delay between retries.
void queueEmail(const char* subj, const char* body){
  if(!cfgEmailEnable) return;
  for(int i=0;i<8;i++){
    if(!emailQueue[i].active){
      emailQueue[i].active=true;
      emailQueue[i].attempts=0;
      emailQueue[i].nextRetryMs=millis();  // try immediately
      strlcpy(emailQueue[i].subj, subj, sizeof(emailQueue[i].subj));
      strlcpy(emailQueue[i].body, body, sizeof(emailQueue[i].body));
      Serial.printf("[EMAIL-Q] Queued slot %d: %s\n", i, subj);
      return;
    }
  }
  Serial.println("[EMAIL-Q] Queue full — email dropped");
}

// Process pending emails in queue. Called from loop().
void processEmailQueue(){
  static unsigned long lastProcessMs=0;
  if(millis()-lastProcessMs<2000) return;  // check queue every 2s
  lastProcessMs=millis();
  if(WiFi.status()!=WL_CONNECTED) return;  // need WiFi to send
  for(int i=0;i<8;i++){
    if(!emailQueue[i].active) continue;
    if(millis()<emailQueue[i].nextRetryMs) continue;
    Serial.printf("[EMAIL-Q] Sending slot %d (attempt %d/3): %s\n",
      i, emailQueue[i].attempts+1, emailQueue[i].subj);
    bool ok=sendEmail(emailQueue[i].subj, emailQueue[i].body);
    emailQueue[i].attempts++;
    if(ok){
      Serial.printf("[EMAIL-Q] Slot %d sent OK\n", i);
      emailQueue[i].active=false;
    } else if(emailQueue[i].attempts>=3){
      Serial.printf("[EMAIL-Q] Slot %d failed after 3 tries — dropped\n", i);
      emailQueue[i].active=false;
    } else {
      // Retry in 10 seconds
      emailQueue[i].nextRetryMs=millis()+10000;
      Serial.printf("[EMAIL-Q] Slot %d will retry in 10s\n", i);
    }
    return;  // process only one per call to avoid blocking
  }
}

void emailTestStart(){
  if(!cfgEmailOnStart) return;
  char subj[80]; String body;
  snprintf(subj,sizeof(subj),"[ATE] Test Started: %s",cfgProject);
  body+="ATE Power Cycle Test — TEST STARTED\r\n";
  body+="=============================================\r\n";
  body+="Project   : "+String(cfgProject)+"\r\n";
  body+="Engineer  : "+String(cfgEngineer)+"\r\n";
  body+="Started   : "+String(rtcNow().c_str())+"\r\n";
  body+="Mode      : "+String(cfgSimMode?"SIMULATION":"REAL HARDWARE")+"\r\n";
  body+="Cycles    : "+String(cfgCycles)+"\r\n";
  body+="DUTs      : "+String(cfgDutCount)+"\r\n";
  body+="---------------------------------------------\r\n";
  body+="DASHBOARD ACCESS\r\n";
  body+="  Local  : http://192.168.4.1/dashboard\r\n";
  if(WiFi.status()==WL_CONNECTED && espIP!="192.168.4.1")
    body+="  Lab    : http://"+espIP+"/dashboard\r\n";
  if(ngrokRunning && ngrokURL.length()>0){
    body+="---------------------------------------------\r\n";
    body+="  REMOTE : "+ngrokURL+"/dashboard\r\n";
    body+="  (open this link from anywhere in the world)\r\n";
    body+="---------------------------------------------\r\n";
  }
  queueEmail(subj,body.c_str());
}

void emailDutDone(int idx){
  if(!cfgEmailOnDutDone) return;
  char subj[80]; String body;
  bool hasErr = duts[idx].errors>0;
  snprintf(subj,sizeof(subj),"[ATE] DUT%d [%s] — %s",
    idx+1,cfgDutName[idx],hasErr?"COMPLETED WITH ERRORS":"COMPLETED OK");
  float rate = duts[idx].completed>0?(duts[idx].pass*100.0f/duts[idx].completed):0;
  body+="ATE Power Cycle Test — DUT COMPLETE\r\n";
  body+="=============================================\r\n";
  body+="Project   : "+String(cfgProject)+"\r\n";
  body+="Engineer  : "+String(cfgEngineer)+"\r\n";
  if(duts[idx].dutEndTime[0])
    body+="Completed : "+String(duts[idx].dutEndTime)+"\r\n";
  body+="Status    : "+String(hasErr?"COMPLETED WITH ERRORS":"COMPLETED OK")+"\r\n";
  body+="---------------------------------------------\r\n";
  body+="RESULTS:\r\n";
  body+="  DUT "+String(idx+1)+" ["+cfgDutName[idx]+"]\r\n";
  body+="    PASS  = "+String(duts[idx].pass)+" ("+String(rate,1)+"%)\r\n";
  body+="    ERROR = "+String(duts[idx].errors)+"\r\n";
  body+="---------------------------------------------\r\n";
  body+="Total cycles  : "+String(duts[idx].completed)+"\r\n";
  body+="Total PASS    : "+String(duts[idx].pass)+"\r\n";
  body+="Total ERROR   : "+String(duts[idx].errors)+"\r\n";
  body+="Avg boot time : "+String(duts[idx].avgBoot,1)+"s\r\n";
  body+="Avg cycle time: "+String(duts[idx].avgCycleS,1)+"s\r\n";
  body+="Elapsed       : "+elapsedStr(duts[idx].dutElapsedMs)+"\r\n";
  body+="---------------------------------------------\r\n";
  body+="Log: http://192.168.4.1/log.csv?dut="+String(idx+1)+"\r\n";
  queueEmail(subj,body.c_str());
}

void emailAllDone(){
  if(!cfgEmailOnAllDone) return;
  char subj[80]; String body;
  int totPass=0,totFail=0,totCyc=0;
  bool anyErr=false;
  for(int i=0;i<4;i++){
    if(!duts[i].enabled) continue;
    totPass+=duts[i].pass; totFail+=duts[i].errors; totCyc+=duts[i].completed;
    if(duts[i].errors>0) anyErr=true;
  }
  snprintf(subj,sizeof(subj),"[ATE] ALL DONE: %s — %s",
    cfgProject, anyErr?"COMPLETED WITH ERRORS":"ALL PASS");
  float rate=totCyc>0?(totPass*100.0f/totCyc):0;
  body+="ATE Power Cycle Test — TEST COMPLETE\r\n";
  body+="=============================================\r\n";
  body+="Project   : "+String(cfgProject)+"\r\n";
  body+="Engineer  : "+String(cfgEngineer)+"\r\n";
  body+="Completed : "+String(rtcNow().c_str())+"\r\n";
  body+="Status    : "+String(anyErr?"COMPLETED WITH ERRORS":"ALL PASS")+"\r\n";
  body+="---------------------------------------------\r\n";
  body+="RESULTS:\r\n";
  for(int i=0;i<4;i++){
    if(!duts[i].enabled) continue;
    float r=duts[i].completed>0?(duts[i].pass*100.0f/duts[i].completed):0;
    body+="  DUT "+String(i+1)+" ["+cfgDutName[i]+"]";
    body+="   PASS="+String(duts[i].pass)+" ("+String(r,1)+"%)";
    body+="   ERROR="+String(duts[i].errors)+"\r\n";
  }
  body+="---------------------------------------------\r\n";
  body+="Total cycles  : "+String(totCyc)+"\r\n";
  body+="Total PASS    : "+String(totPass)+"\r\n";
  body+="Total ERROR   : "+String(totFail)+"\r\n";
  body+="Pass rate     : "+String(rate,1)+"%\r\n";
  body+="---------------------------------------------\r\n";
  body+="Log: http://192.168.4.1/log.csv\r\n";
  queueEmail(subj,body.c_str());
}

void emailError(int idx,int cycle){
  if(!cfgEmailOnError) return;
  char subj[80]; String body;
  snprintf(subj,sizeof(subj),"[ATE] BOOT TIMEOUT: DUT%d [%s] Cycle %d",
    idx+1,cfgDutName[idx],cycle);
  body+="ATE Power Cycle Test — BOOT TIMEOUT\r\n";
  body+="=============================================\r\n";
  body+="Project   : "+String(cfgProject)+"\r\n";
  body+="Engineer  : "+String(cfgEngineer)+"\r\n";
  body+="Time      : "+String(rtcNow().c_str())+"\r\n";
  body+="---------------------------------------------\r\n";
  body+="DUT "+String(idx+1)+" ["+cfgDutName[idx]+"]\r\n";
  body+="Cycle     : "+String(cycle)+"\r\n";
  body+="Errors so far: "+String(duts[idx].errors)+"\r\n";
  body+="PASS so far  : "+String(duts[idx].pass)+"\r\n";
  body+="---------------------------------------------\r\n";
  body+="Dashboard: http://192.168.4.1/dashboard\r\n";
  queueEmail(subj,body.c_str());
}

// ================================================================
//  NGROK
// ================================================================
bool startNgrokTunnel(){
  if(!cfgNgrokEnable||strlen(cfgNgrokToken)==0) return false;
  if(WiFi.status()!=WL_CONNECTED){Serial.println("[NGROK] No WiFi");return false;}
  Serial.println("[NGROK] Connecting to api.ngrok.com...");
  WiFiClientSecure c; c.setInsecure();
  if(!c.connect("api.ngrok.com",443)){Serial.println("[NGROK] Connect failed");return false;}
  // Build JSON body using char array to avoid escaping issues
  char body[256];
  if(strlen(cfgNgrokDomain)>0){
    snprintf(body,sizeof(body),
      "{\"addr\":\"http://localhost:80\",\"proto\":\"http\",\"domain\":\"%s\"}",
      cfgNgrokDomain);
  } else {
    strcpy(body,"{\"addr\":\"http://localhost:80\",\"proto\":\"http\"}");
  }
  c.println("POST /api/tunnels HTTP/1.1");
  c.println("Host: api.ngrok.com");
  c.println("Content-Type: application/json");
  c.println("Ngrok-Version: 2");
  c.println("Authorization: Bearer "+String(cfgNgrokToken));
  c.println("Content-Length: "+String(strlen(body)));
  c.println("Connection: close");
  c.println(); c.print(body);
  unsigned long t=millis(); String resp="";
  while(millis()-t<10000){while(c.available()) resp+=(char)c.read();if(!c.connected())break;delay(10);}
  c.stop();
  Serial.println("[NGROK] Resp(200): "+resp.substring(0,min((int)resp.length(),200)));
  int idx=resp.indexOf("\"public_url\":");
  if(idx<0){Serial.println("[NGROK] No public_url — check token");ngrokURL="";ngrokRunning=false;return false;}
  int q1=resp.indexOf('"',idx+14),q2=resp.indexOf('"',q1+1);
  if(q1<0||q2<0){Serial.println("[NGROK] Parse fail");return false;}
  ngrokURL=resp.substring(q1+1,q2); ngrokRunning=true;
  Serial.println("[NGROK] URL: "+ngrokURL);
  Serial.println("[NGROK] Dashboard: "+ngrokURL+"/dashboard");
  return true;
}

void stopNgrokTunnel(){ngrokURL="";ngrokRunning=false;}

// ================================================================
//  SETUP PAGE  (7 tabs)
// ================================================================
void handleSetup(){
  // If test is running, show a "Test Running" info page with all access URLs
  if(testRunning && !resumedFromChk){
    bool wOK = (WiFi.status()==WL_CONNECTED);
    String h="<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>ATE V2 — Test Running</title>"
      "<style>"
      "*{box-sizing:border-box;margin:0;padding:0;font-family:Arial,sans-serif}"
      "body{background:#eef1f7;padding:14px;color:#1F3864}"
      ".card{background:#fff;max-width:540px;margin:0 auto;border-radius:12px;"
      "box-shadow:0 4px 20px rgba(0,0,0,.1);overflow:hidden}"
      ".hdr{background:linear-gradient(135deg,#1F3864,#2a4a7f);color:#fff;padding:16px 18px}"
      ".hdr h1{font-size:16px;font-weight:700;margin-bottom:3px}"
      ".hdr p{font-size:11px;color:#93c5fd}"
      ".badge{display:inline-flex;align-items:center;gap:5px;background:#86efac;color:#14532d;"
      "border-radius:4px;padding:3px 9px;font-size:10px;font-weight:700;margin-top:7px;"
      "text-transform:uppercase;letter-spacing:.4px}"
      ".led{width:7px;height:7px;border-radius:50%;background:#16a34a;animation:pulse 1.4s infinite}"
      "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}"
      ".body{padding:16px 18px}"
      ".sec{margin-bottom:16px}"
      ".sec h3{font-size:10px;color:#6b7280;text-transform:uppercase;letter-spacing:.5px;"
      "font-weight:700;margin-bottom:7px}"
      ".info{background:#f8fafc;border:1px solid #e2e8f0;border-radius:8px;padding:11px 13px;font-size:13px}"
      ".info b{color:#1F3864}"
      ".linkbox{display:block;background:#f0f9ff;border:1.5px solid #bae6fd;border-radius:8px;"
      "padding:11px 13px;margin-bottom:7px;text-decoration:none;color:#0c4a6e;font-size:13px;transition:all .15s}"
      ".linkbox:hover{background:#e0f2fe;border-color:#7dd3fc}"
      ".linkbox.green{background:#f0fdf4;border-color:#86efac;color:#14532d}"
      ".linkbox.green:hover{background:#dcfce7}"
      ".linkbox.purple{background:#f5f3ff;border-color:#c4b5fd;color:#4c1d95}"
      ".linkbox.purple:hover{background:#ede9fe}"
      ".linkbox.gray{background:#f9fafb;border-color:#e5e7eb;color:#6b7280}"
      ".linkbox-title{font-weight:700;font-size:11px;text-transform:uppercase;letter-spacing:.4px;"
      "margin-bottom:3px;display:flex;align-items:center;gap:6px}"
      ".linkbox-url{font-family:monospace;font-size:13px;font-weight:600}"
      ".linkbox-hint{font-size:10px;opacity:.7;margin-top:2px}"
      ".bigbtn{display:block;text-align:center;background:linear-gradient(135deg,#1F3864,#2E75B6);"
      "color:#fff;padding:13px;border-radius:8px;font-size:14px;font-weight:700;text-decoration:none;"
      "margin-top:10px}"
      ".bigbtn:hover{filter:brightness(1.1)}"
      ".note{font-size:11px;color:#92400e;background:#FEF3C7;border-left:3px solid #f59e0b;"
      "padding:9px 12px;border-radius:0 6px 6px 0;margin-top:10px;line-height:1.5}"
      "</style></head><body><div class='card'>";

    // Header
    h+="<div class='hdr'><h1>&#9881; ATE Standalone V2</h1>"
       "<p>Test &amp; Validation Engineering &mdash; Advantech Europe</p>"
       "<div class='badge'><span class='led'></span>Test Running</div></div>";

    // Body
    h+="<div class='body'>";

    // Project info section
    h+="<div class='sec'><h3>Project</h3>"
       "<div class='info'>";
    h+="<div><b>"+String(cfgProject)+"</b> &middot; "+String(cfgEngineer)+"</div>";
    h+="<div style='font-size:11px;color:#6b7280;margin-top:4px'>";
    h+=String(cfgCycles)+" cycles &middot; "+String(cfgDutCount)+" DUT(s) &middot; ";
    h+=String(cfgSimMode?"Simulation":"Real hardware");
    h+="</div></div></div>";

    // Access URLs section
    h+="<div class='sec'><h3>Dashboard Access</h3>";

    // Always available — local AP
    h+="<a class='linkbox green' href='/dashboard'>";
    h+="<div class='linkbox-title'>&#128241; Local (this device)</div>";
    h+="<div class='linkbox-url'>http://192.168.4.1/dashboard</div>";
    h+="<div class='linkbox-hint'>Always available via ATE-Setup WiFi</div></a>";

    // Lab WiFi if connected
    if(wOK && espIP!="192.168.4.1"){
      h+="<a class='linkbox' href='http://"+espIP+"/dashboard'>";
      h+="<div class='linkbox-title'>&#127970; Lab WiFi ("+String(cfgWifiSSID)+")</div>";
      h+="<div class='linkbox-url'>http://"+espIP+"/dashboard</div>";
      h+="<div class='linkbox-hint'>Accessible from any device on the same WiFi</div></a>";
    } else {
      h+="<div class='linkbox gray'>";
      h+="<div class='linkbox-title'>&#127970; Lab WiFi</div>";
      h+="<div class='linkbox-url'>Not connected to router WiFi</div>";
      h+="<div class='linkbox-hint'>Configure in WiFi tab and restart test</div></div>";
    }

    // ngrok if running
    if(ngrokRunning && ngrokURL.length()>0){
      h+="<a class='linkbox purple' href='"+ngrokURL+"/dashboard' target='_blank'>";
      h+="<div class='linkbox-title'>&#127760; Internet (ngrok)</div>";
      h+="<div class='linkbox-url'>"+ngrokURL+"/dashboard</div>";
      h+="<div class='linkbox-hint'>Accessible from anywhere with internet</div></a>";
    } else {
      h+="<div class='linkbox gray'>";
      h+="<div class='linkbox-title'>&#127760; Internet (ngrok)</div>";
      h+="<div class='linkbox-url'>Not enabled</div>";
      h+="<div class='linkbox-hint'>Enable in Remote tab to access from internet</div></div>";
    }

    h+="</div>";  // end Access section

    // Test email link
    h+="<div class='sec'><h3>Test Email</h3>";
    h+="<a class='linkbox' href='/api/testemail' target='_blank'>";
    h+="<div class='linkbox-title'>&#9993; Send test email now</div>";
    h+="<div class='linkbox-hint'>Verifies SMTP credentials. Result shown in browser.</div></a>";
    h+="</div>";

    // Big dashboard button
    h+="<a class='bigbtn' href='/dashboard'>&#9654; Open Live Dashboard</a>";

    // Note
    h+="<div class='note'><b>Note:</b> Setup form is locked while a test is running. "
       "Stop all DUTs from the dashboard if you need to change settings.</div>";

    h+="</div></div></body></html>";  // close body, card, html

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200,"text/html","");
    server.sendContent(h);
    server.sendContent("");
    return;
  }

  // Build DUT name array for JS
  String SN="[";
  for(int i=0;i<4;i++){
    if(i) SN+=",";
    SN+="\""+String(cfgDutName[i])+"\"";
  }
  SN+="]";

  // Build saved WiFi selector value for JS
  String wSelVal="0";
  if(String(cfgWifiSSID)=="ADVANTECH-GUEST")    wSelVal="1";
  else if(String(cfgWifiSSID)=="Karthik.de")    wSelVal="2";
  else if(String(cfgWifiSSID)=="MagentaWLAN-BVBC") wSelVal="3";
  else if(String(cfgWifiSSID)!="")              wSelVal="c";

  // Email port options
  String ePort465 = (cfgEmailPort==465) ? " selected" : "";
  String ePort587 = (cfgEmailPort==587) ? " selected" : "";

  String h;
  h.reserve(8000);  // chunked — never need more than 8KB at once

  h="<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ATE Standalone V2 Setup</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#eef1f7;padding:10px}"
    ".card{background:#fff;border-radius:12px;max-width:540px;margin:0 auto;box-shadow:0 4px 20px rgba(0,0,0,.1);overflow:hidden}"
    ".hdr{background:linear-gradient(135deg,#1F3864,#2a4a7f);padding:14px 18px}"
    ".hdr h1{font-size:15px;font-weight:700;color:#fff}"
    ".hdr p{font-size:11px;color:#93c5fd;margin-top:2px}"
    ".badge{display:inline-flex;align-items:center;gap:5px;background:rgba(46,117,182,.3);border:1px solid rgba(91,163,224,.3);border-radius:4px;padding:2px 8px;font-size:9px;color:#bfdbfe;margin-top:6px;font-family:monospace}"
    ".led{width:6px;height:6px;border-radius:50%;background:#4ade80;animation:pulse 2s infinite}"
    "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}"
    ".rbanner{background:#E2EFDA;border-bottom:2px solid #86efac;padding:9px 16px;font-size:12px;color:#375623;display:flex;align-items:center;justify-content:space-between}"
    ".rbanner a{background:#375623;color:#fff;padding:5px 11px;border-radius:5px;text-decoration:none;font-size:11px;font-weight:600}"
    ".tabs{display:flex;background:#f8f9fa;border-bottom:2px solid #e5e7eb;overflow-x:auto}"
    ".tab{flex:0 0 auto;min-width:64px;padding:8px 4px 7px;text-align:center;font-size:9px;font-weight:700;color:#6b7280;cursor:pointer;border:none;background:none;border-bottom:3px solid transparent;margin-bottom:-2px;text-transform:uppercase;letter-spacing:.3px;font-family:Arial}"
    ".tab.on{color:#1F3864;border-bottom-color:#1F3864;background:#fff}"
    ".ti{font-size:14px;display:block;margin-bottom:2px}"
    ".pane{display:none;padding:14px 16px}"
    ".pane.on{display:block}"
    ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:11px}"
    ".row.one{grid-template-columns:1fr}"
    "label{font-size:10px;color:#6b7280;display:block;margin-bottom:3px;text-transform:uppercase;letter-spacing:.3px;font-weight:700}"
    "input,select{width:100%;border:1.5px solid #e5e7eb;border-radius:7px;padding:9px 10px;font-size:13px;color:#111;background:#fff;font-family:Arial}"
    "input:focus,select:focus{border-color:#2E75B6;outline:none}"
    ".hint{font-size:10px;color:#aaa;margin-top:3px;line-height:1.4}"
    ".nav{display:flex;gap:8px;margin-top:12px}"
    ".nb{flex:1;padding:9px;border:1.5px solid #e5e7eb;border-radius:7px;font-size:12px;font-weight:600;cursor:pointer;background:#fff;color:#1F3864;font-family:Arial}"
    ".nb.nx{background:#1F3864;color:#fff;border-color:#1F3864}"
    ".infobox{background:#f0f9ff;border:1.5px solid #bae6fd;border-radius:7px;padding:10px;font-size:11px;color:#0369a1;margin-bottom:11px;line-height:1.5}"
    ".dbox{border:1.5px solid #dbeafe;border-radius:9px;padding:11px;margin-bottom:9px;background:#fafcff;border-left:3px solid #2E75B6}"
    ".dtitle{font-size:11px;font-weight:700;color:#1F3864;margin-bottom:8px;font-family:monospace}"
    ".dlybox{background:#f0f9ff;border:1.5px solid #bae6fd;border-radius:7px;padding:9px;margin-top:7px}"
    ".simbox{background:#FEF3C7;border:1.5px solid #fcd34d;border-radius:8px;padding:11px;margin-top:8px}"
    ".simbox label{color:#92400e}"
    ".ebox{background:#f0fdf4;border:1.5px solid #bbf7d0;border-radius:9px;padding:12px;margin-top:8px}"
    ".ebox label{color:#15803d}"
    ".ebox input,.ebox select{border-color:#86efac}"
    ".etrig{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin-top:9px}"
    ".etrig-item{background:#fff;border:1px solid #bbf7d0;border-radius:6px;padding:8px}"
    ".etrig-item label{color:#166534;font-size:9px}"
    ".ngbox{background:#f0f4ff;border:1.5px solid #c7d7fd;border-radius:9px;padding:12px;margin-top:8px}"
    ".ngbox label{color:#3730a3}"
    ".ngbox input{border-color:#a5b4fc}"
    ".sipbox{background:#fff7ed;border:1.5px solid #fed7aa;border-radius:8px;padding:11px;margin-bottom:11px}"
    ".sipbox label{color:#92400e}"
    ".fbar{padding:12px 16px;border-top:1px solid #f0f0f0;display:flex;flex-direction:column;gap:8px;background:#fafafa}"
    ".startbtn{padding:13px;background:linear-gradient(135deg,#1F3864,#2E75B6);color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:700;cursor:pointer;width:100%;font-family:Arial}"
    ".startbtn:disabled{opacity:.4;cursor:not-allowed}"
    ".dashbtn{display:none;padding:11px;text-align:center;background:#375623;color:#fff;border-radius:8px;font-size:13px;font-weight:600;text-decoration:none}"
    ".msg{display:none;padding:10px;border-radius:6px;font-size:12px;text-align:center;background:#E2EFDA;color:#375623}"
    ".msg.err{background:#FCEBEB;color:#791F1F}"
    "</style></head><body><div class='card'>";

  // Header
  h+="<div class='hdr'><h1>&#9881; ATE Standalone V2</h1>"
     "<p>Advantech Europe &mdash; Test &amp; Validation Engineering</p>"
     "<div class='badge'><span class='led'></span>ESP32-S3 &nbsp;&middot;&nbsp; 192.168.4.1</div></div>";

  if(resumedFromChk&&testRunning)
    h+="<div class='rbanner'>&#9654; Test resumed: "+String(cfgProject)
       +" <a href='/dashboard'>Dashboard &rsaquo;</a></div>";

  // Tabs
  h+="<div class='tabs'>"
     "<button class='tab on' id='t0' onclick='T(0)'><span class='ti'>&#128196;</span>PROJECT</button>"
     "<button class='tab' id='t1' onclick='T(1)'><span class='ti'>&#9881;</span>TEST</button>"
     "<button class='tab' id='t2' onclick='T(2)'><span class='ti'>&#128268;</span>DUTS</button>"
     "<button class='tab' id='t3' onclick='T(3)'><span class='ti'>&#128246;</span>WIFI</button>"
     "<button class='tab' id='t4' onclick='T(4)'><span class='ti'>&#9654;</span>MODE</button>"
     "<button class='tab' id='t5' onclick='T(5)'><span class='ti'>&#9993;</span>EMAIL</button>"
     "<button class='tab' id='t6' onclick='T(6)'><span class='ti'>&#127760;</span>REMOTE</button>"
     "</div>";

  // P0 Project
  h+="<div class='pane on' id='p0'>"
     "<div class='row'>"
     "<div><label>Project name</label><input id='proj' value='";
  h+=String(cfgProject);
  h+="'></div>"
     "<div><label>Engineer</label><input id='eng' value='";
  h+=String(cfgEngineer);
  h+="'></div></div>"
     "<div class='nav'><button class='nb nx' onclick='T(1)'>Test &#8250;</button></div>"
     "</div>";

  // P1 Test
  h+="<div class='pane' id='p1'>"
     "<div class='row'>"
     "<div><label>Total cycles</label><input id='cyc' type='number' value='";
  h+=String(cfgCycles);
  h+="'></div>"
     "<div><label>Boot timeout (s)</label><input id='bto' type='number' value='";
  h+=String(cfgBootTO);
  h+="'><div class='hint'>Min 2s enforced</div></div></div>"
     "<div class='row'>"
     "<div><label>OFF gap (s)</label><input id='gap' type='number' value='";
  h+=String(cfgOffGap);
  h+="'><div class='hint'>Relay OFF between cycles</div></div>"
     "<div><label>Max retry (0=fail&amp;advance)</label><input id='retry' type='number' value='";
  h+=String(cfgMaxRetry);
  h+="'><div class='hint'>0=every timeout=FAIL</div></div></div>"
     "<div class='nav'>"
     "<button class='nb' onclick='T(0)'>&#8249; Project</button>"
     "<button class='nb nx' onclick='T(2)'>DUTs &#8250;</button>"
     "</div></div>";

  // P2 DUTs
  h+="<div class='pane' id='p2'>"
     "<div class='row one'><div><label>Number of DUTs (1-4)</label>"
     "<select id='dc' onchange='BD()'>"
     "<option value='1'"; h+=(cfgDutCount==1?" selected":""); h+=">1</option>"
     "<option value='2'"; h+=(cfgDutCount==2?" selected":""); h+=">2</option>"
     "<option value='3'"; h+=(cfgDutCount==3?" selected":""); h+=">3</option>"
     "<option value='4'"; h+=(cfgDutCount==4?" selected":""); h+=">4</option>"
     "</select></div></div>"
     "<div id='dr'></div>"
     "<div class='nav'>"
     "<button class='nb' onclick='T(1)'>&#8249; Test</button>"
     "<button class='nb nx' onclick='T(3)'>WiFi &#8250;</button>"
     "</div></div>";

  // P3 WiFi
  h+="<div class='pane' id='p3'>"
     "<div class='infobox'><strong>ATE-Setup</strong> AP always ON at <strong>192.168.4.1</strong>.<br>"
     "Router WiFi needed only for email or ngrok.</div>"
     "<div class='row one'><div><label>Select WiFi network</label>"
     "<select id='wsel' onchange='WS()'>"
     "<option value='0'>-- None (ATE-Setup AP only) --</option>"
     "<option value='1'>Advantech Office (ADVANTECH-GUEST)</option>"
     "<option value='2'>Karthik Home (Karthik.de)</option>"
     "<option value='3'>Home WiFi (MagentaWLAN-BVBC)</option>"
     "<option value='c'>Custom...</option>"
     "</select>"
     "<div class='hint'>Presets have hardcoded passwords. Custom lets you type manually.</div>"
     "</div></div>"
     "<div id='wPreset' style='display:none'>"
     "<div class='row one'><div><label>SSID (auto)</label>"
     "<input id='ssid' readonly style='background:#f5f5f5'></div></div>"
     "<div class='row one'><div><label>Password (auto)</label>"
     "<input id='wpass' type='password' readonly style='background:#f5f5f5'></div></div>"
     "</div>"
     "<div id='wCustom' style='display:none'>"
     "<div class='row one'><div><label>Custom SSID</label>"
     "<input id='ssid_c'></div></div>"
     "<div class='row one'><div><label>Custom password</label>"
     "<input id='wpass_c' type='password'></div></div>"
     "</div>"
     "<div class='sipbox'>"
     "<div style='font-size:10px;font-weight:700;color:#92400e;margin-bottom:8px'>&#128204; Static IP (optional)</div>"
     "<div class='row'>"
     "<div><label>Static IP</label><input id='sip' value='";
  h+=String(cfgStaticIP);
  h+="' placeholder='0.0.0.0=DHCP'><div class='hint'>e.g. 192.168.39.100</div></div>"
     "<div><label>Gateway</label><input id='sgw' value='";
  h+=String(cfgGateway);
  h+="' placeholder='e.g. 192.168.39.1'><div class='hint'>Router IP</div></div>"
     "</div>"
     "<div class='row one'><div><label>Subnet</label><input id='ssub' value='";
  h+=String(cfgSubnet);
  h+="'></div></div></div>"
     "<div class='nav'>"
     "<button class='nb' onclick='T(2)'>&#8249; DUTs</button>"
     "<button class='nb nx' onclick='T(4)'>Mode &#8250;</button>"
     "</div></div>";

  // P4 Mode
  h+="<div class='pane' id='p4'>"
     "<div class='row one'><div><label>Test mode</label>"
     "<select id='mode' onchange='CM()'>"
     "<option value='real'>Real hardware (relay + opto)</option>"
     "<option value='sim'>Simulation (no hardware needed)</option>"
     "</select><div class='hint'>Simulation runs full state machine without relay or opto</div></div></div>"
     "<div id='simbox' class='simbox' style='display:none'>"
     "<div class='row'>"
     "<div><label>Sim boot time (s)</label><input id='sb' type='number' value='5' min='1'>"
     "<div class='hint'>Simulated USB detect delay</div></div>"
     "<div><label>Sim run time (s)</label><input id='sr' type='number' value='5' min='1'>"
     "<div class='hint'>USB ON duration</div></div>"
     "</div></div>"
     "<div class='row one'><div><label>Buzzer</label>"
     "<select id='bz'>"
     "<option value='1'";  h+=(cfgBuzzer?" selected":"");
  h+=">Enabled (beeps on test events)</option>"
     "<option value='0'";  h+=(!cfgBuzzer?" selected":"");
  h+=">Disabled (silent operation)</option>"
     "</select><div class='hint'>Mute all buzzer beeps for quiet lab environments</div></div></div>"
     "<div class='nav'>"
     "<button class='nb' onclick='T(3)'>&#8249; WiFi</button>"
     "<button class='nb nx' onclick='T(5)'>Email &#8250;</button>"
     "</div></div>";

  // P5 Email
  h+="<div class='pane' id='p5'>"
     "<div class='row one'><div><label>Email notifications</label>"
     "<select id='emen' onchange='CE()'>"
     "<option value='1'";  h+=(cfgEmailEnable?" selected":"");
  h+=">Enabled</option>"
     "<option value='0'";  h+=(!cfgEmailEnable?" selected":"");
  h+=">Disabled</option>"
     "</select><div class='hint'>Pre-filled with ATE credentials &mdash; port 465 SSL</div></div></div>"
     "<div id='ebox' class='ebox'>"
     "<div style='font-size:10px;font-weight:700;color:#15803d;margin:9px 0 6px'>&#128225; SMTP</div>"
     "<div class='row'>"
     "<div><label>SMTP server</label><input id='esmtp' value='";
  h+=String(cfgEmailSMTP);
  h+="'></div>"
     "<div><label>Port</label>"
     "<select id='eport'>"
     "<option value='465'"; h+=ePort465; h+=">465 (SSL)</option>"
     "<option value='587'"; h+=ePort587; h+=">587 (STARTTLS)</option>"
     "</select></div></div>"
     "<div class='row one'><div><label>Sender email</label>"
     "<input id='euser' type='email' value='";
  h+=String(cfgEmailUser);
  h+="'></div></div>"
     "<div class='row one'><div><label>App password (no spaces)</label>"
     "<input id='epass' type='password' value='";
  h+=String(cfgEmailPass);
  h+="'><div class='hint'>Gmail &rarr; Account &rarr; Security &rarr; App Passwords</div></div></div>"
     "<div class='row one'><div><label>Recipient email</label>"
     "<input id='eto' type='email' value='";
  h+=String(cfgEmailTo);
  h+="'></div></div>"
     "<div style='font-size:10px;font-weight:700;color:#15803d;margin:9px 0 6px'>&#128276; Send on</div>"
     "<div class='etrig'>"
     "<div class='etrig-item'><label>Test start</label><select id='est'>"
     "<option value='1'"; h+=(cfgEmailOnStart?" selected":""); h+=">Yes</option>"
     "<option value='0'"; h+=(!cfgEmailOnStart?" selected":""); h+=">No</option>"
     "</select></div>"
     "<div class='etrig-item'><label>Boot error</label><select id='eerr'>"
     "<option value='1'"; h+=(cfgEmailOnError?" selected":""); h+=">Yes</option>"
     "<option value='0'"; h+=(!cfgEmailOnError?" selected":""); h+=">No</option>"
     "</select></div>"
     "<div class='etrig-item'><label>DUT done</label><select id='edn'>"
     "<option value='1'"; h+=(cfgEmailOnDutDone?" selected":""); h+=">Yes</option>"
     "<option value='0'"; h+=(!cfgEmailOnDutDone?" selected":""); h+=">No</option>"
     "</select></div>"
     "<div class='etrig-item'><label>All done</label><select id='ead'>"
     "<option value='1'"; h+=(cfgEmailOnAllDone?" selected":""); h+=">Yes</option>"
     "<option value='0'"; h+=(!cfgEmailOnAllDone?" selected":""); h+=">No</option>"
     "</select></div>"
     "</div></div>"
     "<div class='nav'>"
     "<button class='nb' onclick='T(4)'>&#8249; Mode</button>"
     "<button class='nb nx' onclick='T(6)'>Remote &#8250;</button>"
     "</div></div>";

  // P6 Remote/ngrok
  h+="<div class='pane' id='p6'>"
     "<div class='row one'><div><label>Remote access via ngrok</label>"
     "<select id='ngen' onchange='CN()'>"
     "<option value='0'"; h+=(!cfgNgrokEnable?" selected":""); h+=">Disabled</option>"
     "<option value='1'"; h+=(cfgNgrokEnable?" selected":""); h+=">Enabled (needs router WiFi)</option>"
     "</select><div class='hint'>Free account: dashboard.ngrok.com &rarr; Authtokens</div></div></div>"
     "<div id='ngbox' class='ngbox' style='display:none'>"
     "<div class='row one'><div><label>Authtoken</label>"
     "<input id='ngtok' type='password' value='";
  h+=String(cfgNgrokToken);
  h+="' placeholder='Paste from dashboard.ngrok.com/authtokens'></div></div>"
     "<div class='row one'><div><label>Static domain (paid plan only)</label>"
     "<input id='ngdom' value='";
  h+=String(cfgNgrokDomain);
  h+="' placeholder='leave blank for random URL'></div></div>"
     "</div>"
     "<div class='nav'><button class='nb' onclick='T(5)'>&#8249; Email</button></div>"
     "</div>";

  // Footer
  h+="<div class='fbar'>"
     "<button class='startbtn' id='sbtn' onclick='GO()'>&#9654; Save and Start Test</button>"
     "<a class='dashbtn' id='dbtn' href='/dashboard'>&#9654; Open Live Dashboard</a>"
     "<div id='msg' class='msg'></div>"
     "</div></div>";

  // JavaScript — ALL in one block, no mixing
  h+="<script>";
  h+="var SN="+SN+";";
  h+="var _ss='"+String(cfgWifiSSID)+"';";
  h+="var _sp='"+String(cfgWifiPass)+"';";
  h+="var _wsel='"+wSelVal+"';";
  h+="function T(n){"
     "for(var i=0;i<7;i++){"
     "document.getElementById('t'+i).className='tab'+(i==n?' on':'');"
     "document.getElementById('p'+i).className='pane'+(i==n?' on':'');}"
     "}";
  h+="function CM(){"
     "var s=document.getElementById('mode').value==='sim';"
     "document.getElementById('simbox').style.display=s?'block':'none';"
     "}";
  h+="function CE(){"
     "var on=document.getElementById('emen').value==='1';"
     "document.getElementById('ebox').style.display=on?'block':'none';"
     "}";
  h+="function CN(){"
     "var on=document.getElementById('ngen').value==='1';"
     "document.getElementById('ngbox').style.display=on?'block':'none';"
     "}";
  h+="function WS(){"
     "var v=document.getElementById('wsel').value;"
     "var wp=document.getElementById('wPreset');"
     "var wc=document.getElementById('wCustom');"
     "var si=document.getElementById('ssid');"
     "var pi=document.getElementById('wpass');"
     "if(v==='0'){if(wp)wp.style.display='none';if(wc)wc.style.display='none';"
     "if(si)si.value='';if(pi)pi.value='';}"
     "else if(v==='1'){if(wp)wp.style.display='block';if(wc)wc.style.display='none';"
     "if(si)si.value='ADVANTECH-GUEST';if(pi)pi.value='Advantech66!';}"
     "else if(v==='2'){if(wp)wp.style.display='block';if(wc)wc.style.display='none';"
     "if(si)si.value='Karthik.de';if(pi)pi.value='karthik.er';}"
     "else if(v==='3'){if(wp)wp.style.display='block';if(wc)wc.style.display='none';"
     "if(si)si.value='MagentaWLAN-BVBC';if(pi)pi.value='82542166895510202173';}"
     "else{if(wp)wp.style.display='none';if(wc)wc.style.display='block';}"
     "}";
  h+="function g(id){"
     "var e=document.getElementById(id);"
     "return e?e.value.trim():'';"
     "}";
  h+="function BD(){"
     "var n=parseInt(g('dc'))||1,h='';"
     "var now=new Date();"
     "var pad=function(x){return x<10?'0'+x:String(x);};"
     "var dt=now.getFullYear()+'-'+pad(now.getMonth()+1)+'-'+pad(now.getDate())"
     "+'T'+pad(now.getHours())+':'+pad(now.getMinutes());"
     "for(var i=1;i<=n;i++){"
     "var nm=SN[i-1]||('DUT'+i);"
     "h+='<div class=\"dbox\"><div class=\"dtitle\">DUT '+i+'</div>';"
     "h+='<div class=\"row\">';"
     "h+='<div><label>DUT Name</label><input id=\"dn'+i+'\" value=\"'+nm+'\"></div>';"
     "h+='<div><label>Start</label><select id=\"ds'+i+'\" onchange=\"TD('+i+')\">';"
     "h+='<option value=\"0\">Immediately</option>';"
     "h+='<option value=\"300\">+5 min</option>';"
     "h+='<option value=\"1800\">+30 min</option>';"
     "h+='<option value=\"3600\">+1 hour</option>';"
     "h+='<option value=\"c\">Custom delay (s)</option>';"
     "h+='<option value=\"dt\">Date &amp; time</option>';"
     "h+='</select></div></div>';"
     "h+='<div id=\"dc'+i+'\" style=\"display:none\" class=\"dlybox\">';"
     "h+='<label>Seconds</label><input id=\"dv'+i+'\" type=\"number\" min=\"1\" value=\"3600\">';"
     "h+='</div>';"
     "h+='<div id=\"dd'+i+'\" style=\"display:none\" class=\"dlybox\">';"
     "h+='<label>Start at</label><input id=\"ddt'+i+'\" type=\"datetime-local\" value=\"'+dt+'\">';"
     "h+='</div>';"
     "h+='</div>';}"
     "document.getElementById('dr').innerHTML=h;"
     "}";
  h+="function TD(i){"
     "var v=document.getElementById('ds'+i).value;"
     "document.getElementById('dc'+i).style.display=(v==='c')?'block':'none';"
     "document.getElementById('dd'+i).style.display=(v==='dt')?'block':'none';"
     "}";
  h+="function showOK(r){"
     "document.getElementById('sbtn').disabled=true;"
     "document.getElementById('sbtn').textContent='Test running...';"
     "document.getElementById('dbtn').style.display='block';"
     "var m=document.getElementById('msg');"
     "m.style.display='block';m.className='msg';"
     "var txt='Started! ';"
     "txt+='Local: 192.168.4.1/dashboard';"
     "if(r&&r.router_ip&&r.router_ip!='192.168.4.1')txt+='  |  Lab: '+r.router_ip+'/dashboard';"
     "if(r&&r.ngrok_url&&r.ngrok_url.length>0)txt+='  |  Internet: '+r.ngrok_url+'/dashboard';"
     "m.textContent=txt;"
     "}";
  h+="function GO(){"
     "var b=document.getElementById('sbtn');"
     "b.disabled=true;b.textContent='Starting...';"
     "var n=parseInt(g('dc'))||1,duts=[];"
     "for(var i=1;i<=n;i++){"
     "var ds=document.getElementById('ds'+i).value,dl=0;"
     "if(ds==='c'){dl=parseInt(g('dv'+i))||60;}"
     "else if(ds==='dt'){"
     "var dtv=document.getElementById('ddt'+i).value;"
     "if(dtv)dl=Math.max(0,Math.floor(new Date(dtv).getTime()/1000)-Math.floor(Date.now()/1000));}"
     "else dl=parseInt(ds)||0;"
     "duts.push({id:i,name:g('dn'+i)||'DUT'+i,delay_s:dl});}"
     "var sm=document.getElementById('mode').value==='sim';"
     "var ee=document.getElementById('emen').value==='1';"
     "var ne=document.getElementById('ngen').value==='1';"
     "var wv=document.getElementById('wsel').value;"
     "var wss=(wv==='c')?g('ssid_c'):(wv==='0'?'':g('ssid'));"
     "var wpp=(wv==='c')?(document.getElementById('wpass_c')?document.getElementById('wpass_c').value:''):(document.getElementById('wpass')?document.getElementById('wpass').value:'');"
     "var d={"
     "project:g('proj'),engineer:g('eng'),"
     "wifi_ssid:wss,wifi_pass:wpp,"
     "static_ip:g('sip')||'0.0.0.0',gateway:g('sgw')||'0.0.0.0',subnet:g('ssub')||'255.255.255.0',"
     "cycle_count:parseInt(g('cyc'))||1000,"
     "boot_timeout:parseInt(g('bto'))||120,"
     "off_gap:parseInt(g('gap'))||15,"
     "max_retry:parseInt(g('retry'))||0,"
     "sim_mode:sm,sim_boot:sm?parseInt(g('sb'))||5:5,sim_run:sm?parseInt(g('sr'))||5:5,"
     "buzzer:document.getElementById('bz')?(document.getElementById('bz').value==='1'):true,"
     "email_enable:ee,"
     "email_smtp:g('esmtp')||'smtp.gmail.com',"
     "email_port:parseInt(g('eport'))||465,"
     "email_user:g('euser'),"
     "email_pass:document.getElementById('epass')?document.getElementById('epass').value:'',"
     "email_to:g('eto'),"
     "email_start:document.getElementById('est').value==='1',"
     "email_error:document.getElementById('eerr').value==='1',"
     "email_dutdone:document.getElementById('edn').value==='1',"
     "email_alldone:document.getElementById('ead').value==='1',"
     "ngrok_enable:ne,"
     "ngrok_token:ne?(document.getElementById('ngtok')?document.getElementById('ngtok').value:''):'',"
     "ngrok_domain:ne?g('ngdom'):'',"
     "dut_count:n,duts:duts,epoch:Math.floor(Date.now()/1000),";
  h+="tz_offset:new Date().getTimezoneOffset()};"
     "fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json','ngrok-skip-browser-warning':'true'},body:JSON.stringify(d)})"
     ".then(function(r){return r.json();})"
     ".then(function(r){"
     "if(r.ok)showOK(r);"
     "else{"
     "b.disabled=false;b.textContent='Save and Start Test';"
     "var m=document.getElementById('msg');"
     "m.style.display='block';m.className='msg err';"
     "m.textContent='Error: '+(r.error||'unknown');}})"
     ".catch(function(e){"
     "b.disabled=false;b.textContent='Save and Start Test';"
     "var m=document.getElementById('msg');"
     "m.style.display='block';m.className='msg err';"
     "m.textContent='Connect error: '+e;});"
     "}";
  // Init on page load
  h+="BD();";
  h+="document.getElementById('wsel').value=_wsel;WS();";
  h+="if(_wsel==='c'){"
     "var sc=document.getElementById('ssid_c');if(sc)sc.value=_ss;"
     "var pc=document.getElementById('wpass_c');if(pc)pc.value=_sp;"
     "}";
  h+="if(!"+String(cfgEmailEnable?String("true"):String("false"))+")CE();";
  h+="if("+String(cfgNgrokEnable?String("true"):String("false"))+")CN();";
  h+="fetch('/api/status',{headers:{'ngrok-skip-browser-warning':'true'}}).then(function(r){return r.json();})"
     ".then(function(r){"
     "if(r.running){"
     "document.getElementById('sbtn').disabled=true;"
     "document.getElementById('sbtn').textContent='Test running...';"
     "document.getElementById('dbtn').style.display='block';"
     "}}).catch(function(){});";
  h+="</script></body></html>";

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"text/html","");
  server.sendContent(h);
  server.sendContent("");
}

void handleDashboard(){
  unsigned long now=millis();
  // Freeze overall elapsed when test done — use testEndMs if available
  unsigned long overallEl=0;
  if(testRunning) overallEl=now-testStartMs;
  else if(announcedDone&&testEndMs>0) overallEl=testEndMs-testStartMs;
  int fPct=FFat.totalBytes()>0?(int)(FFat.usedBytes()*100/FFat.totalBytes()):0;
  float chipT=temperatureRead();

  // Stream dashboard in chunks to avoid large heap allocation
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"text/html","");
  String h="<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ATE Dashboard</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#eef1f7;padding:10px}"
    ".hdr{background:#1F3864;color:#fff;padding:11px 14px;border-radius:10px;margin-bottom:9px}"
    ".hdr h1{font-size:14px;font-weight:600;margin-bottom:2px}"
    ".hdr p{font-size:11px;color:#B0C4DE;line-height:1.7}"
    ".hdr .sim{background:#f59e0b;color:#1c1917;padding:1px 7px;border-radius:3px;font-size:10px;font-weight:700;margin-left:5px}"
    ".pills{display:flex;flex-wrap:wrap;gap:5px;margin-bottom:9px}"
    ".pill{background:#fff;border-radius:6px;padding:4px 9px;font-size:11px;box-shadow:0 1px 4px rgba(0,0,0,.07)}"
    ".pill b{color:#1F3864}"
    ".pill.warn b{color:#dc2626}"
    ".pill.remote{border:1px solid #c7d7fd;background:#f0f4ff}"
    ".pill.remote b{color:#3730a3}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(245px,1fr));gap:10px}"
    ".card{background:#fff;border-radius:10px;padding:13px;"
    "box-shadow:0 2px 8px rgba(0,0,0,.07);border-top:3px solid #2E75B6}"
    ".card.done{border-top-color:#16a34a}"
    ".card.err{border-top-color:#dc2626}"
    ".card.paused{border-top-color:#d97706}"
    ".card.stopped{border-top-color:#6b7280}"
    ".card.sched{border-top-color:#94a3b8}"
    ".ct{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:9px}"
    ".cn{font-size:12px;font-weight:700;color:#1F3864;line-height:1.3}"
    ".cn small{font-size:9px;font-weight:400;color:#6b7280;display:block}"
    ".st{font-size:9px;font-weight:700;padding:2px 7px;border-radius:4px;"
    "background:#dbeafe;color:#1d4ed8;white-space:nowrap}"
    ".st.done{background:#dcfce7;color:#15803d}"
    ".st.err{background:#fee2e2;color:#991b1b}"
    ".st.paused{background:#fef3c7;color:#92400e}"
    ".st.stopped{background:#f3f4f6;color:#374151}"
    ".dr{display:flex;justify-content:space-between;padding:3px 0;"
    "border-bottom:1px solid #f5f5f5;font-size:11px}"
    ".dr:last-child{border:none}"
    ".g{color:#16a34a;font-weight:700}.r{color:#dc2626;font-weight:700}.b{color:#2563eb;font-weight:600}"
    ".prog{background:#e5e7eb;border-radius:4px;height:7px;margin-top:7px}"
    ".progf{height:7px;border-radius:4px;background:#16a34a;transition:width .5s}"
    ".progf.blue{background:#2563eb}"
    ".spark{display:flex;gap:2px;margin-top:5px;align-items:center}"
    ".sp{width:10px;height:10px;border-radius:2px}"
    ".sp.p{background:#22c55e}.sp.f{background:#f87171}.sp.e{background:#e5e7eb}"
    ".chk{background:#f8f9fa;border-left:3px solid #2E75B6;border-radius:0 6px 6px 0;"
    "padding:7px 9px;margin:6px 0}"
    ".chk.w{border-left-color:#d97706}"
    ".chk-t{font-size:9px;font-weight:700;color:#1F3864;margin-bottom:3px;text-transform:uppercase;letter-spacing:.4px}"
    ".chk.w .chk-t{color:#92400e}"
    ".chk-n{display:flex;gap:10px;font-size:11px;font-weight:700}"
    ".btns{display:grid;grid-template-columns:1fr 1fr 1fr;gap:4px;margin-top:8px}"
    ".btn{padding:6px 3px;border-radius:5px;font-size:10px;font-weight:700;"
    "cursor:pointer;border:none;width:100%;font-family:Arial}"
    ".btn-p{background:#fef3c7;color:#92400e;border:1px solid #fcd34d}"
    ".btn-r{background:#dcfce7;color:#15803d;border:1px solid #86efac}"
    ".btn-s{background:#fee2e2;color:#991b1b;border:1px solid #fca5a5}"
    ".btn-rs{background:#ede9fe;color:#5b21b6;border:1px solid #c4b5fd;grid-column:1/-1}"
    ".btn-dl{display:block;text-align:center;background:#14532d;color:#bbf7d0;"
    "padding:6px;border-radius:5px;font-size:10px;font-weight:700;margin-top:5px;text-decoration:none}"
    ".gbtn{padding:11px;border-radius:8px;font-size:13px;font-weight:700;"
    "cursor:pointer;border:none;width:100%;font-family:Arial}"
    ".gbtn-s{background:#fee2e2;color:#991b1b;border:1.5px solid #fca5a5}"
    ".gbtn-r{background:#1F3864;color:#fff;border:none}"
    ".msg2{text-align:center;font-size:11px;color:#6b7280;margin-top:5px;min-height:15px}"
    ".dl{display:block;text-align:center;background:#1F3864;color:#fff;"
    "padding:11px;border-radius:8px;text-decoration:none;margin-top:9px;"
    "font-size:13px;font-weight:600}"
    ".footer{text-align:center;color:#9ca3af;font-size:10px;margin-top:9px;font-family:monospace}"
    ".conn{width:7px;height:7px;border-radius:50%;background:#22c55e;"
    "display:inline-block;margin-right:4px;box-shadow:0 0 5px #22c55e}"
    ".conn.off{background:#ef4444;box-shadow:none}"
    "</style></head><body>";

  // Header
  String simBadge=cfgSimMode?"<span class='sim'>SIM</span>":"";
  h+="<div class='hdr'><h1>ATE Standalone V2 &mdash; ";
  h+=String(cfgProject); h+=simBadge; h+="</h1><p>";
  h+="<span class='conn' id='conn'></span>";
  h+="Engineer: "+String(cfgEngineer);
  h+=" &nbsp;|&nbsp; Overall: <span id='ov-el'>"+elapsedStr(overallEl)+"</span>";
  h+=" &nbsp;|&nbsp; Chip: <span id='chipT'>"+String(chipT,1)+"</span>&deg;C";
  h+=" &nbsp;|&nbsp; Flash: "+String(fPct)+"%";
  h+=" &nbsp;|&nbsp; <a href='/' style='color:#7dd3fc'>Setup</a>";
  h+="</p></div>";

  // Pills — access URLs
  h+="<div class='pills'>";
  h+="<div class='pill'>Local <b><a href='http://192.168.4.1/dashboard' style='color:#1F3864'>192.168.4.1/dashboard</a></b></div>";
  if(espIP!="192.168.4.1")
    h+="<div class='pill'>Lab WiFi <b><a href='http://"+espIP+"/dashboard' style='color:#1F3864'>"+espIP+"/dashboard</a></b></div>";
  if(ngrokRunning&&ngrokURL.length()>0)
    h+="<div class='pill remote'>&#127760; Internet <b><a href='"+ngrokURL+"/dashboard' target='_blank' style='color:#3730a3'>"+ngrokURL+"/dashboard</a></b></div>";
  h+="<div class='pill'>Project <b>"+String(cfgProject)+"</b></div>";
  h+="<div class='pill'>Cycles <b>"+String(cfgCycles)+"</b></div>";
  h+="<div class='pill'>Boot TO <b>"+String(cfgBootTO)+"s</b></div>";
  h+="<div class='pill'>OFF gap <b>"+String(cfgOffGap)+"s</b></div>";
  h+="<div class='pill'>Retry <b>"+String(cfgMaxRetry)+"</b></div>";
  if(fPct>=80) h+="<div class='pill warn'>Flash <b>"+String(fPct)+"%</b></div>";
  // WiFi status pill — has id='wfpill' so AJAX can update it live
  if(strlen(cfgWifiSSID)>0){
    bool wOK=(WiFi.status()==WL_CONNECTED);
    if(wOK){
      h+="<div id='wfpill' class='pill' style='border:1px solid #86efac;background:#f0fdf4'>";
      h+="&#128246; <b style='color:#15803d'>"+String(cfgWifiSSID)+" ("+espIP+")</b></div>";
    } else {
      h+="<div id='wfpill' class='pill warn'>&#9888; <b>"+String(cfgWifiSSID)+" not connected</b></div>";
    }
  } else {
    h+="<div id='wfpill'></div>";  // updated by AJAX
  // ngrok pill — hidden until ngrok connects
  if(ngrokRunning && ngrokURL.length()>0){
    h+="<div id='ngrokpill' class='pill remote'>&#127760; <b><a href='";
    h+=ngrokURL;
    h+="/dashboard' target='_blank' style='color:#3730a3'>";
    h+=ngrokURL;
    h+="/dashboard</a></b></div>";
  } else {
    h+="<div id='ngrokpill' class='pill remote' style='display:none'></div>";
  }
  }
  h+="</div>";

  // All done banner — aggregate totals + per-DUT table
  if(announcedDone&&!testRunning){
    int totalPass=0,totalFail=0,totalCompleted=0;
    for(int i=0;i<4;i++){
      if(!duts[i].enabled) continue;
      totalPass+=duts[i].pass; totalFail+=duts[i].errors; totalCompleted+=duts[i].completed;
    }
    float passRate=totalCompleted>0?(totalPass*100.0f/totalCompleted):0;
    String overallElapsed=elapsedStr(testEndMs>0?testEndMs-testStartMs:0);

    // ── Banner wrapper ──
    h+="<div id='done-banner' style='background:#dcfce7;border:1.5px solid #86efac;"
       "border-radius:10px;padding:14px 16px;margin-bottom:9px'>";

    // ── Title ──
    h+="<div style='text-align:center;font-size:17px;font-weight:700;color:#15803d;"
       "margin-bottom:10px'>&#10003; All DUTs Complete</div>";

    // ── Aggregate row ──
    h+="<div style='display:grid;grid-template-columns:repeat(4,1fr);gap:7px;margin-bottom:12px'>";
    // Total PASS
    h+="<div style='background:#fff;border-radius:7px;padding:8px;text-align:center;border:1px solid #bbf7d0'>"
       "<div style='font-size:9px;color:#6b7280;text-transform:uppercase;font-weight:700;letter-spacing:.3px'>Total PASS</div>"
       "<div style='font-size:22px;font-weight:700;color:#16a34a;margin-top:2px'>"+String(totalPass)+"</div></div>";
    // Total FAIL
    h+="<div style='background:#fff;border-radius:7px;padding:8px;text-align:center;border:1px solid #bbf7d0'>"
       "<div style='font-size:9px;color:#6b7280;text-transform:uppercase;font-weight:700;letter-spacing:.3px'>Total FAIL</div>"
       "<div style='font-size:22px;font-weight:700;color:"+String(totalFail>0?"#dc2626":"#16a34a")+";margin-top:2px'>"+String(totalFail)+"</div></div>";
    // Pass rate
    h+="<div style='background:#fff;border-radius:7px;padding:8px;text-align:center;border:1px solid #bbf7d0'>"
       "<div style='font-size:9px;color:#6b7280;text-transform:uppercase;font-weight:700;letter-spacing:.3px'>Pass Rate</div>"
       "<div style='font-size:22px;font-weight:700;color:#1F3864;margin-top:2px'>"+String(passRate,1)+"%</div></div>";
    // Total time
    h+="<div style='background:#fff;border-radius:7px;padding:8px;text-align:center;border:1px solid #bbf7d0'>"
       "<div style='font-size:9px;color:#6b7280;text-transform:uppercase;font-weight:700;letter-spacing:.3px'>Total Time</div>"
       "<div style='font-size:15px;font-weight:700;color:#1F3864;margin-top:5px'>"+overallElapsed+"</div></div>";
    h+="</div>";

    // ── Per-DUT table ──
    h+="<div style='background:#fff;border-radius:8px;border:1px solid #bbf7d0;overflow:hidden'>";
    // Header row
    h+="<div style='display:grid;grid-template-columns:1.2fr .6fr .6fr .6fr .6fr 1fr 1.3fr 1.3fr;"
       "background:#f0fdf4;padding:6px 10px;font-size:9px;font-weight:700;color:#6b7280;"
       "text-transform:uppercase;letter-spacing:.3px;border-bottom:1px solid #bbf7d0'>"
       "<span>DUT</span><span style='text-align:center'>PASS</span>"
       "<span style='text-align:center'>FAIL</span><span style='text-align:center'>RATE</span>"
       "<span style='text-align:center'>AVG BOOT</span><span style='text-align:center'>ELAPSED</span>"
       "<span>STARTED AT</span><span>FINISHED AT</span></div>";
    // One row per DUT
    for(int i=0;i<4;i++){
      if(!duts[i].enabled) continue;
      float r=duts[i].completed>0?(duts[i].pass*100.0f/duts[i].completed):0;
      bool ok=(duts[i].errors==0);
      String rColor=ok?"#16a34a":"#dc2626";
      String rowBg=(i%2==0)?"#fff":"#f9fffe";
      h+="<div style='display:grid;grid-template-columns:1.2fr .6fr .6fr .6fr .6fr 1fr 1.3fr 1.3fr;"
         "padding:8px 10px;font-size:12px;align-items:center;background:"+rowBg+";"
         "border-bottom:1px solid #e5e7eb'>";
      // DUT name
      h+="<span style='font-weight:700;color:#1F3864'>DUT"+String(i+1)+" &mdash; "
         +String(cfgDutName[i])+"</span>";
      // PASS
      h+="<span style='text-align:center;font-weight:700;color:#16a34a'>"+String(duts[i].pass)+"</span>";
      // FAIL
      h+="<span style='text-align:center;font-weight:700;color:"+String(duts[i].errors>0?"#dc2626":"#16a34a")+"'>"
         +String(duts[i].errors)+"</span>";
      // Rate
      h+="<span style='text-align:center;font-weight:700;color:#1F3864'>"+String(r,1)+"%</span>";
      // Avg boot
      h+="<span style='text-align:center;color:#374151'>"+String(duts[i].avgBoot,1)+"s</span>";
      // Elapsed
      h+="<span style='text-align:center;color:#374151'>"+elapsedStr(duts[i].dutElapsedMs)+"</span>";
      // STARTED AT — only show real timestamps, not millis fallback (+HH:MM:SS)
      if(duts[i].dutStartTime[0] && duts[i].dutStartTime[0]!='+'){
        h+="<span style='font-size:10px;color:#374151;font-family:monospace'>";
        h+=String(duts[i].dutStartTime);
        h+="</span>";
      } else { h+="<span style='color:#9ca3af;font-size:10px'>—</span>"; }
      // FINISHED AT
      if(duts[i].dutEndTime[0]){
        h+="<span style='font-size:10px;color:#374151;font-family:monospace'>";
        h+=String(duts[i].dutEndTime);
        h+="</span>";
      } else if(duts[i].done||duts[i].stopped){
        h+="<span style='color:#9ca3af;font-size:10px'>no RTC</span>";
      } else {
        h+="<span style='color:#9ca3af;font-size:10px'>running...</span>";
      }
      h+="</div>";
    }
    h+="</div>";  // close table

    h+="<div style='font-size:11px;color:#166534;margin-top:8px;text-align:center'>"
       "Download per-DUT CSV below for full boot stats and summary</div>";
    h+="</div>";  // close banner
  }

  if(!testRunning&&!announcedDone&&testStartMs==0){
    h+="<div style='background:#fff;border-radius:10px;padding:18px;text-align:center;color:#888'>"
       "Test not started &mdash; <a href='/'>Go to Setup</a></div>";
  } else {
    h+="<div class='grid'>";
    for(int i=0;i<4;i++){
      // Show "Add & Start" placeholder for disabled slots when test is running
      if(!duts[i].enabled){
        if(testRunning||announcedDone){
          h+="<div class='card' style='border-style:dashed;opacity:.7'>";
          h+="<div class='ct'><div class='cn'>DUT "+String(i+1)+" &mdash; <span style='color:#9ca3af'>not running</span></div></div>";
          h+="<div style='padding:20px;text-align:center;color:#9ca3af;font-size:12px'>This DUT slot is idle</div>";
          h+="<button class='btn btn-g' style='margin:0 8px 8px' onclick='SA("+String(i+1)+")'>&#9654; Add &amp; Start DUT "+String(i+1)+"</button>";
          h+="</div>";
        }
        continue;
      }
      DUT& d=duts[i];
      int pct=cfgCycles>0?min(100,d.completed*100/cfgCycles):0;
      bool isDone=d.done, isPaused=d.paused, isStopped=d.stopped;
      bool isSched=(strcmp(d.state,"SCHEDULED")==0);
      String cc=isDone?"done":(isStopped?"stopped":(isPaused?"paused":((d.errors>0)?"err":"")));
      String sc=isDone?"done":(isStopped?"stopped":(isPaused?"paused":((d.errors>0)?"err":"")));
      String stLabel=String(d.state);
      if(isSched&&d.startMs>now){unsigned long rm=(d.startMs-now)/1000;stLabel="WAIT "+String(rm/60)+"m"+String(rm%60)+"s";}

      // Per-DUT elapsed — clean logic using separate frozen field
      unsigned long dutEl=0;
      if(d.done||d.stopped){
        dutEl = d.dutElapsedMs;   // frozen at completion time
      } else if(d.dutStartMs>0){
        // Active time = total elapsed minus paused time
        unsigned long pausedSoFar = d.dutPausedTotalMs;
        if(d.paused && d.dutPauseStartMs>0) pausedSoFar+=now-d.dutPauseStartMs;
        dutEl = (now - d.dutStartMs) - pausedSoFar;
      }

      // ETA — use avgCycleS (seconds, float) for accuracy
      String eta="";
      if(!isDone&&!isStopped&&d.avgCycleS>0.1f&&d.completed<cfgCycles){
        unsigned long remCycles=(unsigned long)(cfgCycles-d.completed);
        unsigned long remSec=(unsigned long)(remCycles * d.avgCycleS);
        unsigned long remMin=remSec/60;
        if(remSec<60) eta=String(remSec)+"s";
        else if(remMin<60) eta=String(remMin)+"m "+String(remSec%60)+"s";
        else eta=String(remMin/60)+"h "+String(remMin%60)+"m";
      }

      h+="<div class='card "+cc+"' id='card"+String(i)+"'>";
      h+="<div class='ct'><div class='cn'>DUT "+String(i+1)+" &mdash; "+String(cfgDutName[i]);
      h+="<small>Elapsed: <span id='del"+String(i)+"'>"+elapsedStr(dutEl)+"</span></small>";
      // Per-DUT wall-clock timestamps (independent of other DUTs)
      h+="<small style='display:block;margin-top:1px;color:#94a3b8;font-size:10px'>";
      if(d.dutStartTime[0])
        h+="<span id='dst"+String(i)+"'>&#9654; "+String(d.dutStartTime)+"</span>";
      else
        h+="<span id='dst"+String(i)+"'></span>";
      if(d.dutEndTime[0]){
        h+="<span id='det"+String(i)+"'> &rarr; "+String(d.dutEndTime)+"</span>";
      } else {
        h+=" <span id='det"+String(i)+"'></span>";
      }
      h+="</small></div>";
      h+="<span class='st "+sc+"' id='st"+String(i)+"'>"+stLabel+"</span></div>";

      h+="<div class='dr'><span>Cycles</span><span class='b' id='cyc"+String(i)+"'>"+String(d.completed)+"/"+String(cfgCycles)+" ("+String(pct)+"%)</span></div>";
      h+="<div class='dr'><span>PASS</span><span class='g' id='pass"+String(i)+"'>"+String(d.pass)+"</span></div>";
      h+="<div class='dr'><span>FAIL</span><span class='"+(d.errors>0?String("r"):String("g"))+"' id='err"+String(i)+"'>"+String(d.errors)+"</span></div>";
      h+="<div class='dr'><span>USB 5V</span><span id='usb"+String(i)+"'>"+String(d.usbActive?"&#9679; Active":"&#9675; Off")+"</span></div>";
      h+="<div class='dr'><span>Avg boot</span><span id='boot"+String(i)+"'>"+String(d.avgBoot,1)+"s</span></div>";
      h+="<div class='dr'><span>ETA</span><span id='eta"+String(i)+"'>"+eta+"</span></div>";

      // Checkpoint block
      bool hasFail=d.chkFail>0;
      int blkS=d.chkBlock*100+1;
      h+="<div class='chk"+(hasFail?String(" w"):String(""))+"' id='chk"+String(i)+"'>";
      h+="<div class='chk-t'>Block "+String(blkS)+"-"+String(blkS+99)+"</div>";
      h+="<div class='chk-n'>";
      h+="<span class='g' id='cp"+String(i)+"'>PASS: "+String(d.chkPass)+"</span>";
      h+="<span class='"+(hasFail?String("r"):String("g"))+"' id='cf"+String(i)+"'>FAIL: "+String(d.chkFail)+"</span>";
      h+="</div></div>";

      // Sparkline
      h+="<div class='spark' id='sk"+String(i)+"'>";
      h+="<span style='font-size:9px;color:#9ca3af;margin-right:3px'>Last 10:</span>";
      int n10=min(d.last10idx,10);
      for(int j=0;j<10;j++){
        if(j>=n10) h+="<div class='sp e'></div>";
        else{int v=d.last10[(d.last10idx-n10+j)%10];h+="<div class='sp "+(v==1?String("p"):String("f"))+"'></div>";}
      }
      h+="</div>";


      // Progress
      h+="<div class='prog'><div class='progf"+(isDone?String(""):String(" blue"))+"' id='pf"+String(i)+"' style='width:"+String(pct)+"%'></div></div>";

      // Buttons — always show Reset. Show Pause/Stop only when running.
      if(!isDone&&!isStopped){
        h+="<div class='btns'>";
        if(isPaused) h+="<button class='btn btn-r' onclick='DC("+String(i+1)+",\"resume\")'>&#9654; Resume</button>";
        else         h+="<button class='btn btn-p' onclick='DC("+String(i+1)+",\"pause\")'>&#9646;&#9646; Pause</button>";
        h+="<button class='btn btn-s' onclick='DC("+String(i+1)+",\"stop\")'>&#9632; Stop</button>";
        h+="<button class='btn btn-rs' onclick='RM("+String(i+1)+")'>";
        h+="&#8635; Reset DUT "+String(i+1)+"</button>";
        h+="</div>";
      } else {
        // DONE or STOPPED — still show Reset so user can start a new DUT independently
        h+="<div class='btns'>";
        h+="<button class='btn btn-rs' style='background:#1F3864;color:#fff;border:none;' onclick='RM("+String(i+1)+")'>";
        h+="&#8635; Start New Test on DUT "+String(i+1)+"</button>";
        h+="</div>";
      }

      // Per-DUT CSV download
      h+="<a class='btn-dl' href='/log.csv?dut="+String(i+1)+"'>&#11123; Download DUT "+String(i+1)+" Log ("+String(cfgDutName[i])+")</a>";
      h+="</div>";
    }
    h+="</div>";
  }

  // Global controls
  if(testRunning){
    h+="<div style='display:grid;grid-template-columns:1fr 1fr;gap:9px;margin-top:11px'>"
       "<button class='gbtn gbtn-s' onclick='GC(\"stop\")'>&#9632; Stop All DUTs</button>"
       "<button class='gbtn gbtn-r' onclick='GC(\"reset\")'>&#8635; Reset ALL DUTs</button>"
       "</div><div class='msg2' id='gmsg'></div>";
  }

  server.sendContent(h); h="";  // flush cards to free heap
  h+="<a class='dl' href='/log.csv'>&#11123; Download Full Log (all DUTs)</a>";
  h+="<div class='footer'>ATE Standalone V2 &middot; Advantech Europe &middot; ";
  h+="Overall: <span id='ft-el'>"+elapsedStr(overallEl)+"</span> &middot; ";
  h+="<a href='/' style='color:#9ca3af'>Setup</a></div>";

  // Flush footer before sending JS — keeps browser responsive
  server.sendContent(h); h="";

  // AJAX JavaScript
  h+="<script>";
  h+="function DC(n,c){"
     "var st=document.getElementById('st'+n);if(st)st.textContent='...';"
     "_fetching=false;"
     "fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json','ngrok-skip-browser-warning':'true'},"
     "body:JSON.stringify({cmd:c+'_dut'+n})})"
     ".then(function(r){return r.json();})"
     ".then(function(r){var m=document.getElementById('gmsg');if(m)m.textContent=r.msg||'';"
     "setTimeout(function(){location.reload();},700);});}";

  h+="var _mn=null;";
  h+="function RM(n){"
     "_mn=n;"
     "var lb=document.getElementById('ml');if(lb)lb.textContent=n;"
     "var now=new Date(Date.now()+300000);"
     "var pad=function(x){return x<10?'0'+x:String(x);};"
     "var dv=now.getFullYear()+'-'+pad(now.getMonth()+1)+'-'+pad(now.getDate())"
     "+'T'+pad(now.getHours())+':'+pad(now.getMinutes());"
     "var de=document.getElementById('mdt');if(de)de.value=dv;"
     "var r0=document.getElementById('mr0');if(r0)r0.checked=true;"
     "var mn=document.getElementById('mname');if(mn)mn.value='';"
     // Pre-fill test params from current /api/status data
     "fetch('/api/status',{headers:{'ngrok-skip-browser-warning':'true'}}).then(function(r){return r.json();}).then(function(d){"
     "var mcyc=document.getElementById('mcyc');if(mcyc)mcyc.value=d.totalCycles||d.bootTO||1000;"     "var bto=document.getElementById('mbto');if(bto)bto.value=d.bootTO||120;"
     "var gap=document.getElementById('mgap');if(gap)gap.value=d.offGap||15;"
     "var ret=document.getElementById('mret');if(ret)ret.value=d.maxRetry||0;"
     "var bz=document.getElementById('mbz');if(bz)bz.value=d.buzzer?'1':'0';"
     "}).catch(function(){});"
     "var mo=document.getElementById('modal');if(mo)mo.style.display='flex';}";
  h+="function MC(){var mo=document.getElementById('modal');if(mo)mo.style.display='none';_mn=null;}";
  h+="function MO(){"
     "if(!_mn)return;"
     "var sel=document.querySelector('input[name=mw]:checked');"
     "var w=sel?sel.value:'now',ds=0,ep=0;"
     "if(w==='5m')ds=300;else if(w==='30m')ds=1800;else if(w==='1h')ds=3600;"
     "else if(w==='c'){ds=parseInt(document.getElementById('mcs').value)||60;}"
     "else if(w==='dt'){var dv2=document.getElementById('mdt').value;"
     "if(dv2)ep=Math.floor(new Date(dv2).getTime()/1000);}"
     "var newName=document.getElementById('mname').value.trim();"
     "var mcyc=parseInt(document.getElementById('mcyc').value)||0;"     "var bto=parseInt(document.getElementById('mbto').value)||0;"
     "var gap=parseInt(document.getElementById('mgap').value);if(isNaN(gap))gap=-1;"
     "var ret=parseInt(document.getElementById('mret').value);if(isNaN(ret))ret=-1;"
     "var bzs=document.getElementById('mbz').value;"
     "var bzv=(bzs==='1')?1:0;"
     "var n=_mn;MC();"
     "fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json','ngrok-skip-browser-warning':'true'},"
     "body:JSON.stringify({cmd:'reset_dut'+n,delay_s:ds,epoch:ep,new_name:newName,"     "cycle_count:mcyc,"
     "boot_to:bto,off_gap:gap,max_retry:ret,buzzer:bzv})})"
     ".then(function(r){return r.json();})"
     ".then(function(r){var m=document.getElementById('gmsg');if(m)m.textContent=r.msg||'';"
     "setTimeout(function(){location.reload();},1000);});}";

  h+="function GC(c){"
     "if(!confirm(c==='stop'?'Stop ALL DUTs now?':'Reset ALL DUTs to cycle 1?'))return;"
     "fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json','ngrok-skip-browser-warning':'true'},"
     "body:JSON.stringify({cmd:c})})"
     ".then(function(r){return r.json();})"
     ".then(function(r){var m=document.getElementById('gmsg');if(m)m.textContent=r.msg||'';"
     "setTimeout(function(){location.reload();},1200);});}";

  server.sendContent(h); h="";  // flush JS helpers before poll function
  // ── Live poll — clean rewrite ─────────────────────────────────────────
  h+="var _pollErr=0;";
  h+="function sv(id,v){var e=document.getElementById(id);if(e)e.textContent=(v!==null&&v!==undefined)?v:'';}"; 
  h+="function _setLive(ok){var c=document.getElementById('conn');if(!c)return;c.style.background=ok?'#4ade80':'#f87171';if(ok)_pollErr=0;}";
  h+="function poll(){";
  h+="return fetch('/api/status',{headers:{'ngrok-skip-browser-warning':'true'}})";
  h+=".then(function(r){if(!r.ok)throw new Error(r.status);return r.json();})";
  h+=".then(function(d){";
  h+="_setLive(true);";
  h+="if(d.elapsed){sv('ov-el',d.elapsed);sv('ft-el',d.elapsed);}";
  h+="if(d.chipTemp!==undefined)sv('chipT',parseFloat(d.chipTemp).toFixed(1));";
  h+="var dts=d.duts;if(!dts||!dts.length){return;}";
  h+="for(var _i=0;_i<dts.length;_i++){";
  h+="var dd=dts[_i];";
  h+="var ix=(dd.id!==undefined)?(dd.id-1):_i;";
  h+="try{";
  h+="var pct=dd.total>0?Math.round(dd.completed*100/dd.total):0;";
  h+="sv('cyc'+ix,dd.completed+'/'+dd.total+'  ('+pct+'%)');";
  h+="sv('pass'+ix,dd.pass); sv('err'+ix,dd.errors);";
  h+="sv('st'+ix,dd.state); sv('eta'+ix,dd.eta||'');";
  h+="if(dd.avgBoot&&parseFloat(dd.avgBoot)>0)sv('boot'+ix,parseFloat(dd.avgBoot).toFixed(1)+'s');";
  h+="var cp=document.getElementById('cp'+ix);if(cp)cp.textContent='PASS: '+dd.chkPass;";
  h+="var cf=document.getElementById('cf'+ix);";
  h+="if(cf){cf.textContent='FAIL: '+dd.chkFail;cf.className=dd.chkFail>0?'r':'g';}";
  h+="var ue=document.getElementById('usb'+ix);if(ue)ue.textContent=dd.usbActive?'USB ON':'USB Off';";
  h+="if(!dd.done&&!dd.stopped){var de=document.getElementById('del'+ix);if(de&&dd.dut_elapsed)de.textContent=dd.dut_elapsed;}";
  h+="var pf=document.getElementById('pf'+ix);if(pf)pf.style.width=pct+'%';";
  h+="var cd=document.getElementById('card'+ix);";
  h+="if(cd){var cc='card';if(dd.done)cc+=' done';else if(dd.stopped)cc+=' stopped';else if(dd.paused)cc+=' paused';else if(dd.errors>0)cc+=' err';cd.className=cc;}";
  h+="if(dd.last10){var sk=document.getElementById('sk'+ix);if(sk){";
  h+="var pp=dd.last10.split(','),sh='';";
  h+="for(var j=0;j<pp.length;j++){var cl=pp[j]==='1'?'p':(pp[j]==='0'?'f':'e');sh+='<div class=\"sp '+cl+'\"></div>';}";
  h+="sk.innerHTML=sh;}}";
  h+="}catch(ex){console.warn('DUT'+ix,ex);}";
  h+="}"; // end DUT loop
  h+="try{var wp=document.getElementById('wfpill');";
  h+="if(wp&&d.wifi_ssid){if(d.wifi_ok){";
  h+="wp.className='pill';wp.style.cssText='border:1px solid #86efac;background:#f0fdf4';";
  h+="wp.innerHTML='&#128246; <b style=\'color:#15803d\'>'+d.wifi_ssid+' ('+d.wifi_ip+')</b>';";
  h+="}else{wp.className='pill warn';wp.style.cssText='';";
  h+="wp.innerHTML='&#9888; <b>'+d.wifi_ssid+' not connected</b>';}}}catch(ex){}";  
  h+="try{var np=document.getElementById('ngrokpill');";
  h+="if(np){if(d.ngrok_url&&d.ngrok_url.length>0){";
  h+="np.style.display='';";
  h+="np.innerHTML='&#127760; <b><a href=\"'+d.ngrok_url+'/dashboard\" target=\"_blank\" style=\"color:#3730a3\">'+d.ngrok_url+'</a></b>';";
  h+="}else np.style.display='none';}}catch(ex){}";
  h+="var allDone=dts.length>0&&dts.every(function(x){return x.done||x.stopped;});";
  h+="if(allDone&&!d.running&&!document.getElementById('done-banner')){location.reload();}";
  h+="})";
  h+=".catch(function(e){_pollErr++;_setLive(false);console.warn('poll:',e);if(_pollErr>=10){_pollErr=0;location.reload();}});";
  h+="}"; // end poll()

  h+="var _fetching=false,_lastPollMs=0;";
  h+="function pollSafe(){";
  h+="var now=Date.now();";
  h+="if(_fetching&&(now-_lastPollMs)>3000){_fetching=false;}";
  h+="if(_fetching)return;";
  h+="_fetching=true;_lastPollMs=now;";
  h+="poll().finally(function(){_fetching=false;});";
  h+="}";
  h+="setInterval(pollSafe,800);pollSafe();";
  // SA() — Add & Start a new DUT independently while other DUTs run
  h+="var _saDut=null;";
  h+="function SA(n){";
  h+="_saDut=n;";
  h+="var lb=document.getElementById('sal');if(lb)lb.textContent=n;";
  h+="var nc=document.getElementById('sacyc');if(nc)nc.value='";
  h+=String(cfgCycles);
  h+="';";
  h+="var nb=document.getElementById('sabto');if(nb)nb.value='";
  h+=String(cfgBootTO);
  h+="';";
  h+="var mo=document.getElementById('samodal');if(mo)mo.style.display='flex';";
  h+="var ds=document.getElementById('sads');if(ds)ds.onchange=function(){";
  h+="var cv=document.getElementById('sadcust');if(cv)cv.style.display=(ds.value==='c')?'block':'none';};";
  h+="}";
  h+="function SAC(){var mo=document.getElementById('samodal');if(mo)mo.style.display='none';_saDut=null;}";
  h+="function SAO(){";
  h+="if(!_saDut)return;";
  h+="var nm=document.getElementById('saname').value.trim();";
  h+="var cyc=parseInt(document.getElementById('sacyc').value)||1000;";
  h+="var bto=parseInt(document.getElementById('sabto').value)||120;";
  h+="var gap=parseInt(document.getElementById('sagap').value)||0;";
  h+="var bz=document.getElementById('sabz')?document.getElementById('sabz').value:'1';";
  h+="var dsv=document.getElementById('sads')?document.getElementById('sads').value:'0';";
  h+="var dl=0;";
  h+="if(dsv==='c'){dl=parseInt(document.getElementById('sadval')?document.getElementById('sadval').value:'0')||0;}";
  h+="else dl=parseInt(dsv)||0;";
  h+="var n=_saDut;SAC();";
  h+="_fetching=false;";
  h+="fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json','ngrok-skip-browser-warning':'true'},";
  h+="body:JSON.stringify({cmd:'start_dut'+n,new_name:nm,cycle_count:cyc,boot_to:bto,off_gap:gap,buzzer:parseInt(bz),delay_s:dl,epoch:Math.floor(Date.now()/1000)})})";
  h+=".then(function(r){return r.json();})";
  h+=".then(function(r){_fetching=false;pollSafe();})";
  h+=".catch(function(){_fetching=false;});";
  h+="};";

  h+="</script>";

  // ── Add & Start DUT modal — each h+= on ONE line, properly terminated ──
  h+="<div id='samodal' onclick='if(event.target===this)SAC()' style='display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);z-index:999;align-items:flex-start;justify-content:center;overflow-y:auto;padding:20px 10px'>";
  h+="<div style='background:#fff;border-radius:12px;padding:18px;max-width:360px;width:94%;box-shadow:0 12px 40px rgba(0,0,0,.3);margin:auto'>";
  h+="<h3 style='font-size:14px;font-weight:700;color:#1F3864;margin-bottom:10px'>&#9654; Add &amp; Start DUT <span id='sal'></span></h3>";
  h+="<div style='margin-bottom:8px'>";
  h+="<label style='font-size:10px;font-weight:700;color:#6b7280;display:block;margin-bottom:4px;text-transform:uppercase'>DUT Name</label>";
  h+="<input id='saname' style='width:100%;border:1.5px solid #e5e7eb;border-radius:7px;padding:8px 10px;font-size:13px' placeholder='e.g. DUT2_SN002'>";
  h+="</div>";
  h+="<div style='display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px'>";
  h+="<div><label style='font-size:10px;font-weight:700;color:#6b7280;display:block;margin-bottom:3px;text-transform:uppercase'>Total cycles</label>";
  h+="<input id='sacyc' style='width:100%;border:1.5px solid #e5e7eb;border-radius:7px;padding:8px 10px;font-size:13px' type='number' min='1'></div>";
  h+="<div><label style='font-size:10px;font-weight:700;color:#6b7280;display:block;margin-bottom:3px;text-transform:uppercase'>Boot timeout (s)</label>";
  h+="<input id='sabto' style='width:100%;border:1.5px solid #e5e7eb;border-radius:7px;padding:8px 10px;font-size:13px' type='number' min='2'></div>";
  h+="</div>";
  h+="<div style='display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px'>";
  h+="<div><label style='font-size:10px;font-weight:700;color:#6b7280;display:block;margin-bottom:3px;text-transform:uppercase'>OFF gap (s)</label>";
  h+="<input id='sagap' style='width:100%;border:1.5px solid #e5e7eb;border-radius:7px;padding:8px 10px;font-size:13px' type='number' min='0' value='";
  h+=String(cfgOffGap);
  h+="'></div>";
  h+="<div><label style='font-size:10px;font-weight:700;color:#6b7280;display:block;margin-bottom:3px;text-transform:uppercase'>Buzzer</label>";
  h+="<select id='sabz' style='width:100%;border:1.5px solid #e5e7eb;border-radius:7px;padding:8px 10px;font-size:13px'>";
  h+="<option value='1'>Enabled</option><option value='0'";
  h+=(cfgBuzzer?String(""):String(" selected"));
  h+=">Disabled</option></select></div></div>";
  h+="<div style='margin-bottom:8px'><label style='font-size:10px;font-weight:700;color:#6b7280;display:block;margin-bottom:3px;text-transform:uppercase'>When to start</label>";
  h+="<select id='sads'><option value='0'>Immediately</option>";
  h+="<option value='300'>After 5 min</option>";
  h+="<option value='1800'>After 30 min</option>";
  h+="<option value='3600'>After 1 hour</option>";
  h+="<option value='c'>Custom (s)</option></select></div>";
  h+="<div id='sadcust' style='display:none;margin-bottom:8px'>";
  h+="<input id='sadval' style='width:100%;border:1.5px solid #e5e7eb;border-radius:7px;padding:8px 10px;font-size:13px' type='number' min='1' placeholder='Delay in seconds'></div>";
  h+="<div style='display:flex;gap:8px;margin-top:4px'>";
  h+="<button onclick='SAC()' style='flex:1;padding:10px;border:1.5px solid #e5e7eb;border-radius:7px;background:#fff;font-size:13px;font-weight:600;cursor:pointer'>Cancel</button>";
  h+="<button onclick='SAO()' style='flex:1.4;padding:10px;background:#1F3864;color:#fff;border:none;border-radius:7px;font-size:13px;font-weight:700;cursor:pointer'>&#9654; Start DUT</button>";
  h+="</div></div></div>";

  // Reset modal — comprehensive: name + start time + test params + buzzer
  h+="<style>"
     ".sect{border-top:1px solid #e5e7eb;margin-top:12px;padding-top:10px}"
     ".sect-h{font-size:9px;font-weight:700;color:#6b7280;text-transform:uppercase;letter-spacing:.4px;margin-bottom:7px}"
     ".rl{display:flex;align-items:center;gap:7px;padding:7px 10px;"
     "border:1.5px solid #e5e7eb;border-radius:6px;font-size:12px;cursor:pointer;margin-bottom:4px}"
     ".rl:hover{background:#f9fafb}"
     ".rl input[type=radio]:checked+span{font-weight:700;color:#1F3864}"
     ".rl input[type=number],.rl input[type=datetime-local]{font-size:11px;padding:3px 6px;border:1px solid #d1d5db;border-radius:4px}"
     ".pin{width:100%;border:1.5px solid #e5e7eb;border-radius:6px;padding:8px 10px;font-size:13px}"
     ".prow{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}"
     ".prow label{font-size:10px;color:#6b7280;font-weight:700;display:block;margin-bottom:3px;text-transform:uppercase;letter-spacing:.3px}"
     "</style>"
     "<div id='modal' onclick='if(event.target===this)MC()'"
     " style='display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);"
     "z-index:999;align-items:flex-start;justify-content:center;overflow-y:auto;padding:20px 10px'>"
     "<div style='background:#fff;border-radius:12px;padding:18px;"
     "max-width:380px;width:94%;box-shadow:0 12px 40px rgba(0,0,0,.3);margin:auto'>"
     "<h3 style='font-size:14px;font-weight:700;color:#1F3864;margin-bottom:3px'>"
     "&#8635; New Test &mdash; DUT <span id='ml'></span></h3>"
     "<p style='font-size:11px;color:#6b7280;margin-bottom:10px'>Counters reset for this DUT only. Other DUTs unaffected.</p>"

     // ── DUT Name ──
     "<div class='sect-h' style='border:none;margin:0 0 5px'>DUT Name</div>"
     "<input id='mname' class='pin' type='text' placeholder='Leave blank to keep current name'>"

     // ── Test parameters ──
     "<div class='sect'><div class='sect-h'>Test parameters (this DUT only)</div>"
     "<div class='prow'>"
     "<div><label>Total cycles</label>"
     "<input id='mcyc' class='pin' type='number' min='1' placeholder='e.g. 1000'></div>"
     "<div><label>Boot timeout (s)</label>"
     "<input id='mbto' class='pin' type='number' min='2'></div>"
     "</div>"
     "<div class='prow'>"
     "<div><label>OFF gap (s)</label>"
     "<input id='mgap' class='pin' type='number' min='0'></div>"
     "<div><label>Max retry</label>"
     "<input id='mret' class='pin' type='number' min='0'></div>"
     "</div>"
     "<div class='prow'>"
     "<div><label>Buzzer</label><select id='mbz' class='pin'>"
     "<option value='1'>Enabled</option><option value='0'>Disabled</option>"
     "</select></div><div></div></div></div>"

     // ── When to start ──
     "<div class='sect'><div class='sect-h'>When to start?</div>"
     "<label class='rl'><input id='mr0' type='radio' name='mw' value='now' checked><span>Immediately</span></label>"
     "<label class='rl'><input type='radio' name='mw' value='5m'><span>After 5 min</span></label>"
     "<label class='rl'><input type='radio' name='mw' value='30m'><span>After 30 min</span></label>"
     "<label class='rl'><input type='radio' name='mw' value='1h'><span>After 1 hour</span></label>"
     "<label class='rl'><input type='radio' name='mw' value='c'><span>Custom:</span>"
     "<input id='mcs' type='number' min='1' value='3600' style='width:75px'><span>s</span></label>"
     "<label class='rl'><input type='radio' name='mw' value='dt'><span>Date &amp; time:</span>"
     "<input id='mdt' type='datetime-local'></label>"
     "</div>"

     // ── Buttons ──
     "<div style='display:flex;gap:8px;margin-top:14px'>"
     "<button onclick='MC()' style='flex:1;padding:10px;border:1.5px solid #e5e7eb;"
     "border-radius:7px;background:#fff;font-size:13px;font-weight:600;cursor:pointer'>Cancel</button>"
     "<button onclick='MO()' style='flex:1.4;padding:10px;background:#1F3864;color:#fff;"
     "border:none;border-radius:7px;font-size:13px;font-weight:700;cursor:pointer'>"
     "&#9654; Start Test</button></div></div></div>";

  h+="</body></html>";
  server.sendContent(h);
  server.sendContent("");
}


// ================================================================
//  /api/status
// ================================================================
void handleApiStatus(){
  unsigned long now=millis();
  unsigned long el=0;
  if(testRunning) el=now-testStartMs;
  else if(announcedDone&&testEndMs>0) el=testEndMs-testStartMs;
  char elBuf[20]; strcpy(elBuf,elapsedStr(el).c_str());

  // Use DynamicJsonDocument on heap — 4 DUTs × 20 fields needs ~1800 bytes minimum
  DynamicJsonDocument doc(2560);
  doc["running"]=testRunning; doc["elapsed"]=elBuf;
  doc["chipTemp"]=temperatureRead();
  doc["ngrok_url"]=ngrokURL; doc["ngrok_ok"]=ngrokRunning;
  bool wifiOK=(WiFi.status()==WL_CONNECTED);
  doc["wifi_ok"]=wifiOK;
  doc["buzzer"]=cfgBuzzer;
  doc["ngrok_url"]=ngrokRunning?ngrokURL:String("");
  doc["bootTO"]=cfgBootTO;
  doc["offGap"]=cfgOffGap;
  doc["maxRetry"]=cfgMaxRetry;
  doc["totalCycles"]=cfgCycles;
  doc["wifi_ssid"]=wifiOK?String(cfgWifiSSID):String("not connected");
  doc["wifi_ip"]=wifiOK?espIP:String("not connected");
  doc["router_ip"]=espIP;

  JsonArray arr=doc.createNestedArray("duts");
  for(int i=0;i<4;i++){
    if(!duts[i].enabled) continue;
    JsonObject d=arr.createNestedObject();
    d["id"]=i+1; d["name"]=cfgDutName[i];
    d["startTime"]=duts[i].dutStartTime;
    d["endTime"]=duts[i].dutEndTime;
    // F7: For scheduled DUTs, format state with countdown "WAIT 5m 30s"
    if(strcmp(duts[i].state,"SCHEDULED")==0 && duts[i].startMs>now){
      unsigned long rm=(duts[i].startMs-now)/1000;
      char schedBuf[24];
      snprintf(schedBuf,sizeof(schedBuf),"WAIT %lum %02lus",rm/60,rm%60);
      d["state"]=schedBuf;
    } else {
      d["state"]=duts[i].state;
    }
    d["completed"]=duts[i].completed;
    d["total"]=(dutTargetCycles[i]>0)?dutTargetCycles[i]:cfgCycles;
    d["pass"]=duts[i].pass; d["errors"]=duts[i].errors;
    d["avgBoot"]=String(duts[i].avgBoot,1);
    d["usbActive"]=duts[i].usbActive;
    d["paused"]=duts[i].paused; d["stopped"]=duts[i].stopped; d["done"]=duts[i].done;
    d["chkPass"]=duts[i].chkPass;
    d["chkFail"]=duts[i].chkFail;
    d["chkBlock"]=duts[i].chkBlock;
    // Per-DUT elapsed — clean with frozen field
    unsigned long dutEl=0;
    if(duts[i].done||duts[i].stopped){
      dutEl=duts[i].dutElapsedMs;
    } else if(duts[i].dutStartMs>0){
      dutEl=now-duts[i].dutStartMs;
    }
    d["dut_elapsed"]=elapsedStr(dutEl).c_str();
    // Last 10 sparkline — compact char array, no String heap alloc
    {
      char sp[20]=""; int pos=0;
      int n10=min(duts[i].last10idx,10);
      for(int j=0;j<10;j++){
        if(j>0)sp[pos++]=',';
        if(j>=n10)sp[pos++]='e';
        else sp[pos++]=(char)('0'+duts[i].last10[(duts[i].last10idx-n10+j)%10]);
      }
      sp[pos]=0;
      d["last10"]=sp;
    }
    d["bootOutliers"]=duts[i].bootOutliers;

    // ETA
    if(!duts[i].done&&!duts[i].stopped&&duts[i].avgCycleS>0.1f&&duts[i].completed<cfgCycles){
      unsigned long remCycles=(unsigned long)(cfgCycles-duts[i].completed);
      unsigned long remSec=(unsigned long)(remCycles*duts[i].avgCycleS);
      unsigned long remMin=remSec/60;
      char etabuf[16];
      if(remSec<60)      snprintf(etabuf,sizeof(etabuf),"%lus",remSec);
      else if(remMin<60) snprintf(etabuf,sizeof(etabuf),"%lum %02lus",remMin,remSec%60);
      else               snprintf(etabuf,sizeof(etabuf),"%luh %02lum",remMin/60,remMin%60);
      d["eta"]=etabuf;
    } else d["eta"]="";
  }
  String out; serializeJson(doc,out);
  server.sendHeader("Cache-Control","no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma","no-cache");
  server.sendHeader("Expires","0");
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.sendHeader("ngrok-skip-browser-warning","true");
  server.send(200,"application/json",out);
}

// ================================================================
//  /api/cmd
// ================================================================
void handleApiCmd(){
  if(server.method()!=HTTP_POST){server.send(405);return;}
  StaticJsonDocument<256> doc;
  if(deserializeJson(doc,server.arg("plain"))){server.send(400,"application/json","{\"ok\":false}");return;}
  const char* cmd=doc["cmd"]|"";
  StaticJsonDocument<128> resp;
  unsigned long now=millis();

  if(strcmp(cmd,"stop")==0){
    for(int i=0;i<4;i++) if(duts[i].enabled&&!duts[i].done){
      if(!cfgSimMode) digitalWrite(RELAY[i],LOW);
      duts[i].dutElapsedMs=(duts[i].dutStartMs>0)?(millis()-duts[i].dutStartMs):0;
      strlcpy(duts[i].dutEndTime, rtcNow().c_str(), sizeof(duts[i].dutEndTime));
      strcpy(duts[i].state,"STOPPED"); duts[i].stopped=true; duts[i].done=true;
      pendingCsvSave[i]=true;  // defer CSV write — do after response sent
    }
    announcedDone=true;
    testRunning=false; testEndMs=millis(); stopBeep();  // checkpoint saved via pendingCsvSave in loop()
    if(ngrokRunning) stopNgrokTunnel();
    resp["ok"]=true; resp["msg"]="All DUTs stopped";

  } else if(strncmp(cmd,"start_dut",9)==0){
    // Start a previously-disabled DUT independently while others run
    int idx=atoi(cmd+9)-1;
    if(idx>=0 && idx<4 && !duts[idx].enabled){
      // Apply epoch if sent
      if(doc.containsKey("epoch")){
        long ep=(long)doc["epoch"];
        if(ep>0){ epochOffset=ep-(long)(millis()/1000); }
      }
      // Init the DUT
      duts[idx]=DUT();
      duts[idx].enabled=true;
      duts[idx].startMs=millis();
      duts[idx].waiting=false;
      duts[idx].dutStartMs=millis();
      strcpy(duts[idx].state,"RELAY_ON");
      strlcpy(duts[idx].dutStartTime, rtcNow().c_str(), sizeof(duts[idx].dutStartTime));
      // Apply name
      const char* nn=doc["new_name"]|"";
      if(strlen(nn)>0) strlcpy(cfgDutName[idx],nn,sizeof(cfgDutName[0]));
      else {
        char dn[12]; snprintf(dn,sizeof(dn),"DUT%d",idx+1);
        strlcpy(cfgDutName[idx],dn,sizeof(cfgDutName[0]));
      }
      // Apply per-DUT params
      int cyc=doc["cycle_count"]|0; if(cyc>=1) dutTargetCycles[idx]=cyc; else dutTargetCycles[idx]=cfgCycles;
      int bto=doc["boot_to"]|0;     if(bto>=2) cfgBootTO=bto;
      int gap=doc["off_gap"]|-1;    if(gap>=0) cfgOffGap=gap;
      if(doc.containsKey("buzzer"))  cfgBuzzer=(doc["buzzer"]|1)!=0;
      // Apply start delay
      if(doc.containsKey("delay_s")&&(long)doc["delay_s"]>0){
        unsigned long dl=(unsigned long)(long)doc["delay_s"]*1000UL;
        duts[idx].startMs=millis()+dl; duts[idx].waiting=true;
        strcpy(duts[idx].state,"SCHEDULED");
      }
      cfgDutEn[idx]=true;
      cfgDutCount=0; for(int j=0;j<4;j++) if(cfgDutEn[j]) cfgDutCount++;
      // Make sure test is running
      if(!testRunning){ testRunning=true; testStartMs=millis(); announcedDone=false; }
      saveConfig();
      char msg[40]; snprintf(msg,sizeof(msg),"DUT%d started",idx+1);
      resp["ok"]=true; resp["msg"]=msg;
      Serial.printf("[CMD] start_dut%d: %s cycles=%d\n",idx+1,cfgDutName[idx],dutTargetCycles[idx]);
    } else if(duts[idx].enabled){
      resp["ok"]=false; resp["msg"]="DUT already running";
    } else {
      resp["ok"]=false; resp["msg"]="Invalid DUT";
    }

  } else if(strncmp(cmd,"stop_dut",8)==0){
    int idx=atoi(cmd+8)-1;
    if(idx>=0&&idx<4&&duts[idx].enabled&&!duts[idx].done){
      if(!cfgSimMode) digitalWrite(RELAY[idx],LOW);
      strcpy(duts[idx].state,"STOPPED"); duts[idx].stopped=true; duts[idx].done=true;
      duts[idx].dutElapsedMs=(duts[idx].dutStartMs>0)?(millis()-duts[idx].dutStartMs):0;
      strlcpy(duts[idx].dutEndTime, rtcNow().c_str(), sizeof(duts[idx].dutEndTime));
      pendingCsvSave[idx]=true;  // defer CSV write — do after response sent
      stopBeep();
      // Check if ALL DUTs are now done/stopped — if so, announce completion
      bool allDoneNow=true;
      for(int i=0;i<4;i++) if(duts[i].enabled&&!duts[i].done) allDoneNow=false;
      if(allDoneNow){
        announcedDone=true; testRunning=false;
        testEndMs=millis();
        // saveCheckpoint deferred — pendingCsvSave will trigger it
        Serial.println("[TEST] All DUTs done/stopped — test complete");
      }
      char msg[40]; snprintf(msg,sizeof(msg),"DUT%d stopped",idx+1);
      resp["ok"]=true; resp["msg"]=msg;
    } else {resp["ok"]=false;resp["msg"]="Cannot stop";}

  } else if(strcmp(cmd,"reset")==0){
    for(int i=0;i<4;i++) if(cfgDutEn[i]){
      if(!cfgSimMode) digitalWrite(RELAY[i],LOW);
      duts[i]=DUT(); duts[i].enabled=true;
      duts[i].startMs=now+cfgDelayMs[i]; duts[i].waiting=(cfgDelayMs[i]>0);
      duts[i].dutStartMs=now;
      if(!duts[i].waiting) strcpy(duts[i].state,"RELAY_ON");
      else                 strcpy(duts[i].state,"SCHEDULED");
    }
    FFat.remove("/log.csv");
    File f=FFat.open("/log.csv","w");
    if(f){f.println("timestamp,dut,name,cycle,event,result");f.close();}
    testRunning=true; testStartMs=now; announcedDone=false;
    saveCheckpoint(); testStartBeep();
    resp["ok"]=true; resp["msg"]="All DUTs reset";

  } else if(strncmp(cmd,"reset_dut",9)==0){
    int idx=atoi(cmd+9)-1;
    // Cooldown: prevent crash from rapid repeated resets (< 1.5s apart)
    static unsigned long lastResetMs[4]={0,0,0,0};
    if(idx>=0&&idx<4 && (millis()-lastResetMs[idx])<1500){
      resp["ok"]=false; resp["msg"]="Too fast — wait 1.5s between resets";
    } else
    // Allow reset even if DUT is done — enables independent DUT testing
    if(idx>=0&&idx<4&&cfgDutEn[idx]){
      lastResetMs[idx]=millis();
      char sn[32]; strlcpy(sn,cfgDutName[idx],sizeof(sn));
      // Accept optional new DUT name from reset modal
      if(doc.containsKey("new_name")){
        const char* nn=doc["new_name"]|"";
        if(strlen(nn)>0) strlcpy(sn,nn,sizeof(sn));
      }
      pendingConfigSave=true;   // defer flash write — do after response sent
      if(!cfgSimMode) digitalWrite(RELAY[idx],LOW);
      duts[idx]=DUT(); duts[idx].enabled=true;
      strlcpy(cfgDutName[idx],sn,sizeof(cfgDutName[0]));
      // Apply params AFTER DUT() reset so they stick
      dutTargetCycles[idx] = cfgCycles;  // default
      if(doc.containsKey("cycle_count")){
        int v=doc["cycle_count"]|0;
        if(v>=1) dutTargetCycles[idx]=v;
      }
      if(doc.containsKey("boot_to")){int v=doc["boot_to"]|0;if(v>=2) cfgBootTO=v;}
      if(doc.containsKey("off_gap")){int v=doc["off_gap"]|-1;if(v>=0) cfgOffGap=v;}
      if(doc.containsKey("max_retry")){int v=doc["max_retry"]|-1;if(v>=0) cfgMaxRetry=v;}
      if(doc.containsKey("buzzer")){cfgBuzzer=(doc["buzzer"]|1)!=0;}
      // Capture fresh start time for this new test run
      strlcpy(duts[idx].dutStartTime, rtcNow().c_str(), sizeof(duts[idx].dutStartTime));
      duts[idx].dutEndTime[0]=0;  // clear end time
      unsigned long startDelay=0;
      if(doc.containsKey("epoch")&&(long)doc["epoch"]>0){
        long ep=(long)doc["epoch"];
        long cur=(epochOffset>0)?(long)(epochOffset+now/1000):(long)(now/1000);
        long diff=ep-cur; if(diff>0) startDelay=(unsigned long)diff*1000UL;
      } else if(doc.containsKey("delay_s")&&(long)doc["delay_s"]>0){
        startDelay=(unsigned long)(long)doc["delay_s"]*1000UL;
      }
      duts[idx].startMs=now+startDelay; duts[idx].waiting=(startDelay>0);
      duts[idx].dutStartMs=now;   // ← reset per-DUT elapsed time
      if(!duts[idx].waiting) strcpy(duts[idx].state,"RELAY_ON");
      else                   strcpy(duts[idx].state,"SCHEDULED");
      announcedDone=false;
      if(!testRunning){testRunning=true;testStartMs=now;}
      dutDoneBeep();
      char msg[56];
      if(startDelay>0){unsigned long s=startDelay/1000;
        if(s>=60) snprintf(msg,sizeof(msg),"DUT%d reset - starts in %lum%02lus",idx+1,s/60,s%60);
        else      snprintf(msg,sizeof(msg),"DUT%d reset - starts in %lus",idx+1,s);
      } else snprintf(msg,sizeof(msg),"DUT%d reset - starting now",idx+1);
      resp["ok"]=true; resp["msg"]=msg;
    } else {resp["ok"]=false;resp["msg"]="Invalid DUT";}

  } else if(strncmp(cmd,"pause_dut",9)==0){
    int idx=atoi(cmd+9)-1;
    if(idx>=0&&idx<4&&duts[idx].enabled&&!duts[idx].done&&!duts[idx].stopped){
      duts[idx].paused=true; strcpy(duts[idx].state,"PAUSED");
      duts[idx].dutPauseStartMs=millis();  // record pause start
      pauseBeep();
      char msg[40]; snprintf(msg,sizeof(msg),"DUT%d paused",idx+1);
      resp["ok"]=true; resp["msg"]=msg;
    } else {resp["ok"]=false;resp["msg"]="Cannot pause";}

  } else if(strncmp(cmd,"resume_dut",10)==0){
    int idx=atoi(cmd+10)-1;
    if(idx>=0&&idx<4&&duts[idx].paused){
      if(duts[idx].dutPauseStartMs>0)
        duts[idx].dutPausedTotalMs+=millis()-duts[idx].dutPauseStartMs;
      duts[idx].dutPauseStartMs=0;
      duts[idx].paused=false; strcpy(duts[idx].state,"RELAY_ON"); resumeBeep();
      char msg[40]; snprintf(msg,sizeof(msg),"DUT%d resumed",idx+1);
      resp["ok"]=true; resp["msg"]=msg;
    } else {resp["ok"]=false;resp["msg"]="Not paused";}

  } else {resp["ok"]=false;resp["msg"]="Unknown command";}

  String out; serializeJson(resp,out);
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.send(200,"application/json",out);
}

// ================================================================
//  /api/log
// ================================================================
void handleApiLog(){
  int n=server.hasArg("n")?server.arg("n").toInt():20;
  n=constrain(n,1,100);
  File f=FFat.open("/log.csv","r");
  if(!f){server.send(404,"application/json","[]");return;}
  String lines[100]; int count=0;
  while(f.available()){String l=f.readStringUntil('\n');l.trim();if(l.length()==0)continue;lines[count%100]=l;count++;}
  f.close();
  int si=count>n?count-n:0;
  String out="["; bool first=true;
  for(int i=si;i<count;i++){
    String& l=lines[i%100]; if(!first)out+=",";
    String e="\""; for(int j=0;j<(int)l.length();j++){char c=l[j];if(c=='"')e+="\\\"";else if(c=='\\')e+="\\\\";else e+=c;}
    e+="\""; out+=e; first=false;
  }
  out+="]"; server.send(200,"application/json",out);
}

// ================================================================
//  /log.csv
// ================================================================
void handleLog(){
  String dut=server.hasArg("dut")?"DUT"+server.arg("dut"):"";
  if(dut.length()==0){
    File f=FFat.open("/log.csv","r");
    if(!f){server.send(404,"text/plain","No log");return;}
    String fn=String(cfgProject)+"_all_log.csv";
    server.sendHeader("Content-Disposition","attachment; filename="+fn);
    server.streamFile(f,"text/csv"); f.close(); return;
  }
  File f=FFat.open("/log.csv","r");
  if(!f){server.send(404,"text/plain","No log");return;}
  int di=dut.substring(3).toInt()-1;
  String dn=(di>=0&&di<4)?String(cfgDutName[di]):dut;
  String fn=String(cfgProject)+"_"+dn+"_log.csv";
  server.sendHeader("Content-Disposition","attachment; filename="+fn);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"text/csv","");
  server.sendContent("timestamp,dut,name,cycle,event,result\n");
  while(f.available()){
    String l=f.readStringUntil('\n');l.trim();if(l.length()==0)continue;
    int c1=l.indexOf(','),c2=l.indexOf(',',c1+1);
    if(c1>=0&&c2>c1&&l.substring(c1+1,c2)==dut) server.sendContent(l+"\n");
  }
  f.close();
}

// ================================================================
//  /api/config
// ================================================================
// Safe STA connect — resets STA state without ever touching the AP
// Uses esp_wifi_disconnect() (IDF level) instead of WiFi.disconnect()
// which would briefly kill the AP too
static void staConnect(const char* ssid, const char* pass){
  if(strlen(ssid)==0) return;
  wl_status_t st = WiFi.status();
  // If STA is stuck (failed/lost/idle), reset it at IDF level first
  if(st==WL_CONNECT_FAILED || st==WL_CONNECTION_LOST || st==WL_DISCONNECTED){
    esp_wifi_disconnect();   // STA only — AP stays up 100%
    delay(100);
  }
  Serial.printf("[WiFi] Connecting to '%s'\n", ssid);
  WiFi.begin(ssid, pass);
}

void handleConfig(){
  if(server.method()!=HTTP_POST){server.send(405);return;}
  // DynamicJsonDocument on heap — avoids stack overflow for large POST body
  DynamicJsonDocument doc(4096);
  DeserializationError jerr=deserializeJson(doc,server.arg("plain"));
  if(jerr){
    Serial.printf("[CONFIG] JSON parse error: %s (body len=%d)\n",
                  jerr.c_str(),server.arg("plain").length());
    server.send(400,"application/json","{\"ok\":false,\"error\":\"bad JSON\"}");return;
  }
  // Verify epoch was received (if missing, doc was likely truncated)
  if(!doc.containsKey("epoch")){
    Serial.println("[CONFIG] WARNING: epoch missing from POST — JSON may be truncated");
  }
  strlcpy(cfgProject, doc["project"] |"ATE",      sizeof(cfgProject));
  strlcpy(cfgEngineer,doc["engineer"]|"Engineer",  sizeof(cfgEngineer));
  strlcpy(cfgWifiSSID,  doc["wifi_ssid"] |"",              sizeof(cfgWifiSSID));
  strlcpy(cfgWifiPass,  doc["wifi_pass"] |"",              sizeof(cfgWifiPass));
  strlcpy(cfgStaticIP,  doc["static_ip"] |"0.0.0.0",       sizeof(cfgStaticIP));
  strlcpy(cfgGateway,   doc["gateway"]   |"0.0.0.0",       sizeof(cfgGateway));
  strlcpy(cfgSubnet,    doc["subnet"]    |"255.255.255.0",  sizeof(cfgSubnet));
  cfgCycles  =doc["cycle_count"] |1000;
  cfgBootTO  =doc["boot_timeout"]|120;
  cfgOffGap  =doc["off_gap"]     |15;
  cfgMaxRetry=doc["max_retry"]   |0;
  cfgDutCount=doc["dut_count"]   |1;
  cfgSimMode =doc["sim_mode"]    |false;
  cfgBuzzer  =doc["buzzer"]      |true;
  cfgSimBoot =doc["sim_boot"]    |5;
  cfgSimRun  =doc["sim_run"]     |5;
  cfgEmailEnable   =doc["email_enable"]  |true;   // default true — pre-filled credentials
  cfgEmailOnStart  =doc["email_start"]  |true;
  cfgEmailOnError  =doc["email_error"]  |true;
  cfgEmailOnDutDone=doc["email_dutdone"]|true;
  cfgEmailOnAllDone=doc["email_alldone"]|true;
  strlcpy(cfgEmailSMTP,doc["email_smtp"]|"smtp.gmail.com",sizeof(cfgEmailSMTP));
  cfgEmailPort=doc["email_port"]|465;
  cfgBuzzer=doc["buzzer"]|true;
  strlcpy(cfgEmailUser,doc["email_user"]|"ate.advantech@gmail.com",sizeof(cfgEmailUser));
  strlcpy(cfgEmailPass,doc["email_pass"]|"wtqrypownonovwdz",sizeof(cfgEmailPass));
  strlcpy(cfgEmailTo,  doc["email_to"]  |"karthikeyan.thirunavukarasu@advantech.com",sizeof(cfgEmailTo));
  cfgNgrokEnable=doc["ngrok_enable"]|false;
  strlcpy(cfgNgrokToken, doc["ngrok_token"] |"",sizeof(cfgNgrokToken));
  strlcpy(cfgNgrokDomain,doc["ngrok_domain"]|"",sizeof(cfgNgrokDomain));

  for(int i=0;i<4;i++) cfgDutEn[i]=false;
  JsonArray arr=doc["duts"];
  if(arr) for(JsonObject d:arr){
    int idx=((int)(d["id"]|1))-1;
    if(idx>=0&&idx<4){
      cfgDutEn[idx]=true;
      cfgDelayMs[idx]=(unsigned long)((long)(d["delay_s"]|0))*1000UL;
      strlcpy(cfgDutName[idx],d["name"]|"DUT",sizeof(cfgDutName[0]));
    }
  }

  // ── Sync epoch FIRST — must happen before DUT init so rtcNow() is correct ──
  if(doc.containsKey("epoch")){
    long ep=(long)doc["epoch"];
    if(ep>0){
      if(rtcOK) rtc.adjust(DateTime((uint32_t)ep));
      epochOffset=ep-(long)(millis()/1000);
      // Timezone offset from browser (minutes, negative for UTC+) -> convert to seconds
      if(doc.containsKey("tz_offset")){
        long tzMin=(long)doc["tz_offset"];  // browser sends -120 for UTC+2
        tzOffsetSec=-tzMin*60L;  // negate: -120 min -> +7200 sec for UTC+2
      }
      Serial.printf("[RTC] Synced: %s (tz=%+lds)\n",rtcNow().c_str(),tzOffsetSec);
    }
  }

  // ── Init DUTs (after epoch sync so dutStartTime has correct wall-clock time) ──
  unsigned long now=millis();
  for(int i=0;i<4;i++){
    duts[i]=DUT();
    if(cfgDutEn[i]){
      duts[i].enabled=true;
      duts[i].startMs=now+cfgDelayMs[i];
      duts[i].waiting=(cfgDelayMs[i]>0);
      duts[i].dutStartMs=now;
      strlcpy(duts[i].dutStartTime, rtcNow().c_str(), sizeof(duts[i].dutStartTime));
      dutTargetCycles[i] = cfgCycles;
      if(!duts[i].waiting) strcpy(duts[i].state,"RELAY_ON");
      else                 strcpy(duts[i].state,"SCHEDULED");
    }
  }

  // Fresh log files
  FFat.remove("/log.csv");
  for(int i=0;i<4;i++){char fn[12];snprintf(fn,sizeof(fn),"/dut%d.csv",i+1);FFat.remove(fn);}
  File f=FFat.open("/log.csv","w");
  if(f){f.println("timestamp,dut,name,cycle,event,result");f.close();}

  // Clear email retry queue
  for(int q=0;q<8;q++) emailQueue[q].active=false;

  testRunning=true; testStartMs=now; announcedDone=false; resumedFromChk=false;
  saveConfig(); saveCheckpoint();

  // ── Send response IMMEDIATELY — do not block the browser ────────────
  // WiFi connect + ngrok happen AFTER response so fetch never times out
  {
    StaticJsonDocument<256> resp;
    resp["ok"]=true;
    resp["router_ip"]=espIP;   // may be 192.168.4.1 if not yet connected — updated by auto-reconnect
    resp["ngrok_url"]="";      // ngrok starts after response
    String msg="Local: http://192.168.4.1/dashboard";
    resp["msg"]=msg;
    String out; serializeJson(resp,out);
    server.send(200,"application/json",out);
    delay(10);  // let TCP stack flush the response
  }

  // ── Buzzer — test started ────────────────────────────────────────────
  testStartBeep();

  // ── WiFi connect (non-blocking style — max 8s, then auto-reconnect takes over) ──
  if(strlen(cfgWifiSSID)>0){
    IPAddress staticIP,gateway,subnet;
    bool useStatic=staticIP.fromString(cfgStaticIP)&&staticIP[0]!=0;
    if(useStatic){
      gateway.fromString(cfgGateway); subnet.fromString(cfgSubnet);
      WiFi.config(staticIP,gateway,subnet);
    }
    staConnect(cfgWifiSSID, cfgWifiPass);
    unsigned long wStart=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-wStart<8000){
      delay(200);
      server.handleClient();  // keep web server alive during wait
    }
    if(WiFi.status()==WL_CONNECTED){
      espIP=WiFi.localIP().toString();
      Serial.println("[WiFi] Connected: "+espIP);
    } else {
      Serial.println("[WiFi] Not connected in 8s — auto-reconnect will retry every 30s");
      Serial.printf("[WiFi] status=%d SSID='%s'\n",(int)WiFi.status(),cfgWifiSSID);
    }
  }

  // ── Ngrok tunnel — uses hardcoded default token if none entered ────────
  {
    // Fill default token if field is empty
    if(strlen(cfgNgrokToken)==0)
      strlcpy(cfgNgrokToken, NGROK_TOKEN_DEFAULT, sizeof(cfgNgrokToken));
    bool wantNgrok = cfgNgrokEnable || strlen(cfgNgrokToken)>0;
    if(wantNgrok && WiFi.status()==WL_CONNECTED){
      bool ok=startNgrokTunnel();
      Serial.println(ok?"[NGROK] Active: "+ngrokURL:"[NGROK] Failed — check token + WiFi");
    } else if(wantNgrok) {
      Serial.println("[NGROK] Skipped — no WiFi connected");
    }
  }

  // ── Start email AFTER ngrok setup (body captured at queue time) ────────
  if(cfgEmailEnable){
    // Small yield to let ngrok URL settle before capturing in email body
    server.handleClient();
    emailTestStart();   // ngrokRunning is now set if ngrok connected above
  }
}

// ================================================================
//  setup()
// ================================================================
void setup(){
  Serial.begin(115200);
  { unsigned long _t=millis(); while(!Serial&&millis()-_t<3000); } // wait for USB CDC
  Serial.println();
  Serial.println("=== ATE Standalone V2 ===");
  Serial.flush();

  // Silence noisy WiFi SDK log messages
  esp_log_level_set("wifi", ESP_LOG_NONE);
  esp_log_level_set("wifi_init", ESP_LOG_NONE);

  // WiFi init — order is critical for AP stability on ESP32-S3
  WiFi.persistent(false);   // never auto-save creds to NVS flash
  WiFi.mode(WIFI_OFF);      // fully reset WiFi stack
  delay(300);               // longer settle — ESP32-S3 needs this
  WiFi.mode(WIFI_AP_STA);   // enable AP+STA simultaneously
  delay(200);
  WiFi.setSleep(false);     // disable power save BEFORE softAP — important
  Serial.println("WiFi: stack ready");

  for(int i=0;i<4;i++){
    pinMode(RELAY[i],OUTPUT); digitalWrite(RELAY[i],LOW);
    pinMode(OPTO[i],INPUT_PULLUP);
  }
  pinMode(BUZZER,OUTPUT); digitalWrite(BUZZER,LOW);
  Serial.println("Relays: all OFF");

  // Watchdog — safely disabled to avoid boot crash on ESP32 core 3.x
  // Re-enable only if you need it and know your core version
  // esp_task_wdt_init(30, true);
  Serial.println("Watchdog: skipped (safe mode)");

  Wire.begin(I2C_SDA,I2C_SCL);    Wire.setClock(100000);
  I2C_1.begin(I2C1_SDA,I2C1_SCL); I2C_1.setClock(100000);
  delay(100);

  Wire.beginTransmission(0x68);
  if(Wire.endTransmission()==0){rtcOK=rtc.begin(&Wire);Serial.printf("RTC: %s\n",rtcOK?"OK":"FAIL");}
  else Serial.println("RTC: not connected");
  delay(50);

  Wire.beginTransmission(0x3C);
  if(Wire.endTransmission()==0){
    oled1OK=oled1.begin(SSD1306_SWITCHCAPVCC,0x3C);
  }
  I2C_1.beginTransmission(0x3C);
  if(I2C_1.endTransmission()==0){
    oled2OK=oled2.begin(SSD1306_SWITCHCAPVCC,0x3C);
  }
  Serial.printf("OLED 0.96:%s  OLED 0.91:%s\n",oled1OK?"OK":"FAIL",oled2OK?"OK":"FAIL");

  // FFat works with 16M Flash (3MB APP/9.9MB FATFS) partition scheme
  fsOK = FFat.begin(true);   // sets global flag — true = format on first use
  if(!fsOK){
    Serial.println("FFat: partition not found — check partition scheme");
    Serial.println("FFat: Set Tools→Partition Scheme→16M Flash (2MB APP/9.9MB FATFS)");
    // Still continue — WiFi AP will work, just no log saving
  } else {
    Serial.printf("FFat: OK %u/%u bytes\n",FFat.usedBytes(),FFat.totalBytes());
  }

  loadSavedConfig();
  loadCheckpoint();

  // Start AP — channel 6, explicit IP, max 4 clients
  // Fixed IP config BEFORE softAP for stable addressing
  IPAddress apIP(192,168,4,1), apGW(192,168,4,1), apSN(255,255,255,0);
  WiFi.softAPConfig(apIP, apGW, apSN);
  delay(100);
  // Force 20MHz bandwidth — Intel/Realtek laptop adapters reject ESP32 40MHz HT
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  bool apOK = WiFi.softAP(SETUP_SSID, SETUP_PASS, 1, 0, 4);  // ch1, max 4 clients
  // Force WPA2-PSK — most compatible auth mode for Windows/Linux/Android
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  delay(200);  // wait for AP to fully initialize before anything else
  Serial.printf("AP: %s / %s @ 192.168.4.1 — %s\n",
                SETUP_SSID, SETUP_PASS, apOK?"OK":"FAILED");
  Serial.printf("AP IP: %s  CH: %d\n",
                WiFi.softAPIP().toString().c_str(), WiFi.channel());
  // STA stays disconnected at boot — only connects when user clicks Start

  server.on("/",           HTTP_GET,  handleSetup);
  server.on("/dashboard",  HTTP_GET,  handleDashboard);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/status", HTTP_GET,  handleApiStatus);
  server.on("/api/cmd",    HTTP_POST, handleApiCmd);
  // Handle CORS preflight (OPTIONS) requests — needed for ngrok remote access
  server.onNotFound([](){
    if(server.method()==HTTP_OPTIONS){
      server.sendHeader("Access-Control-Allow-Origin","*");
      server.sendHeader("Access-Control-Allow-Methods","GET,POST,OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers","Content-Type,ngrok-skip-browser-warning");
      server.send(204);
    } else {
      server.send(404,"text/plain","Not found");
    }
  });
  server.on("/api/log",    HTTP_GET,  handleApiLog);
  server.on("/log.csv",    HTTP_GET,  handleLog);
  server.on("/api/testemail", HTTP_GET, [](){
    Serial.println("[TESTEMAIL] Triggered");
    if(WiFi.status()!=WL_CONNECTED){
      server.send(200,"text/plain","ERROR: WiFi not connected");return;
    }
    char subj[80],body[300];
    snprintf(subj,sizeof(subj),"ATE Test Email from %s",cfgProject);
    snprintf(body,sizeof(body),
      "This is a test email from ATE Standalone V2.\r\n"
      "If you see this, email is working!\r\n\r\n"
      "Device IP: %s\r\nProject: %s\r\nEngineer: %s\r\n",
      espIP.c_str(),cfgProject,cfgEngineer);
    bool ok=sendEmail(subj,body);
    server.send(200,"text/plain",ok?"OK: Email sent successfully!":"FAIL: Check Serial Monitor for SMTP debug output");
  });
  server.begin();

  Serial.println("Ready: http://192.168.4.1");
  Serial.printf("[EMAIL] %s | to: %s\n",
    cfgEmailEnable?"ENABLED":"DISABLED", cfgEmailTo);
  Serial.printf("[WIFI]  SSID: %s\n",
    strlen(cfgWifiSSID)>0?cfgWifiSSID:"(AP only)");
  updateOLEDs(); startupBeep();
}

// ================================================================
//  loop()
// ================================================================
void loop(){
  // ── Web server — HIGHEST priority ───────────────────────────────────
  // Called first AND after every potentially slow operation
  server.handleClient();

  // ── Relay state machine — time-critical ──────────────────────────────
  if(testRunning){
    for(int i=0;i<4;i++) runDUT(i);
    server.handleClient();  // yield after relay work

    bool allDone=true;
    for(int i=0;i<4;i++) if(duts[i].enabled&&!duts[i].done){allDone=false;break;}
    if(allDone&&!announcedDone){
      announcedDone=true; testRunning=false; resumedFromChk=false;
      testEndMs=millis();
      successBeep();
      server.handleClient();  // yield before flash ops
      FFat.remove("/checkpoint.json");
      saveCheckpoint();
      server.handleClient();  // yield after flash ops
      if(cfgEmailEnable) emailAllDone();
      Serial.println("=== ALL DUTs COMPLETE ===");
    }
    if(millis()-lastChkpointMs>=30000){
      server.handleClient();
      saveCheckpoint();
      lastChkpointMs=millis();
    }
  }

  server.handleClient();

  // ── Deferred CSV save — one file per loop, after handleClient ────────
  // Process deferred config save (from reset_dut)
  if(pendingConfigSave){
    pendingConfigSave=false;
    server.handleClient();
    saveConfig();
  }
  for(int _ci=0;_ci<4;_ci++){
    if(pendingCsvSave[_ci]){
      pendingCsvSave[_ci]=false;
      server.handleClient();
      saveDutCsv(_ci);
      server.handleClient();
      saveCheckpoint();  // save checkpoint after CSV written
      break;
    }
  }

  server.handleClient();

  // ── Email queue — only when WiFi idle, non-blocking check ────────────
  processEmailQueue();

  server.handleClient();

  // ── OLED update — every 2s (was 1s) — I2C takes ~5ms ────────────────
  if(millis()-lastOledMs>=2000){updateOLEDs();lastOledMs=millis();}

  // ── AP watchdog ────────────────────────────────────────────────────
  // If AP disappears for any reason, restart it automatically
  {
    static unsigned long lastApCheckMs = 0;
    if(millis()-lastApCheckMs >= 10000){  // check every 10s
      lastApCheckMs = millis();
      IPAddress currentAP = WiFi.softAPIP();
      if(currentAP[0]==0){  // AP IP is 0.0.0.0 — AP has died
        Serial.println("[AP] AP dropped — restarting...");
        IPAddress apIP(192,168,4,1), apGW(192,168,4,1), apSN(255,255,255,0);
        WiFi.softAPConfig(apIP, apGW, apSN);
        WiFi.softAP(SETUP_SSID, SETUP_PASS, 1, 0, 4);
        delay(100);
        Serial.printf("[AP] Restarted — IP: %s\n", WiFi.softAPIP().toString().c_str());
      }
    }
  }

  // STA auto-reconnect removed — WiFi.begin() in loop() disrupts the AP
  // WiFi connects once at test start; pill shows current status
}

