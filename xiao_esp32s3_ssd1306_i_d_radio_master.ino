//**********************************************************************************************************
//*    ESP32S3_inet_and_dsp_radio_srv -- Internet and DSP radio with control function by web server
//*
//**********************************************************************************************************
//**********************************************************************************************************
//*    audioI2S -- I2S audiodecoder for ESP32,  refer to https://github.com/schreibfaul1/ESP32-audioI2S                                                            *
//**********************************************************************************************************
//
// Internet radio, first release on 11/2018 for ESP32
//   Version 2  , Aug.05/2019
//   Version 3  , Aug.26/2024 for XIAO ESP32S3
//
// Revise for Internet and DSP : 1/10/2025 - 2/14/2025
//
#include "Arduino.h"
#include "WiFiMulti.h"
#include "WebServer.h"
#include "Audio.h"              // audioI2S see abobe comment
#include <Adafruit_GFX.h>       // install using lib tool
#include <Adafruit_SSD1306.h>   // install using lib tool
#include "Wire.h"
#include <esp_sntp.h>           // esp lib
#include <TimeLib.h>            // https://github.com/PaulStoffregen/Time
#include <Preferences.h>        // For permanent data
#include <RDA5807.h>            // install using lib tool
#define MAXVOL    50          // inet max volume  <= 50 , step is 5
#define MAXVOL_R  15          // radio max volume  <= 15
#define LED_BULTIN       21   // XIAO esp32s3, low level on     
#define I2S_DOUT      3       //## esp32:25
#define I2S_BCLK      7       //## esp32:26
#define I2S_LRC       9       //## esp32:22
#define SLEEP         1       // sleep timer control button pin
#define PIN_SDA 5             // i2c
#define PIN_SCL 6             // i2c
#define VOL_PIN1     2        // volume up
#define VOL_PIN2     4        // volume down
#define INT_PIN      8        // station change
#define AOUT_SW      44       // audio output switch
#define OLED_I2C_ADDRESS 0x3C // Check the I2C bus of your OLED device
#define SCREEN_WIDTH 128      // OLED display width, in pixels
#define SCREEN_HEIGHT 64      // OLED display height, in pixels
#define OLED_RESET -1         // Reset pin # (or -1 if sharing Arduino reset pin)
#define MAXSTNIDX    7          // station index 0-7          
#define MAXSCEDIDX   8          // schedule table index 0-8          

Audio audio;
WiFiMulti wifiMulti;
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RDA5807 radio;
// WiFi
String ssid =     "SSID";      // WiFi 1
String password = "PASSWORD";
String ssid2 =     "SSID2";      // WiFi 2, uncomment if you want to use second wifi access point
String password2 = "PASSWORD2";      // specify if you want 
// time
struct tm *tm;
int d_mon ;
int d_mday ;
int d_hour ;
int d_min ;
int d_sec ;
int d_wday ;
int last_d_sec = 99;
int last_d_min = 99;
static const char *weekStr[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"}; // three letters
int currIdx = 99;
int pofftm_h = 0;
int pofftm_m = 0;
const char* ntpServer = "ntp.nict.jp";
const long  gmtOffset_sec = 32400;
const int   daylightOffset_sec = 0;
// mode
bool mode_chg_ok = true;
bool mode_chg_req = false;
//
int volume = 35; // inet volume
int station = 0; // inet station url
int ct2,pt2,event;  // interval time 
bool sleepmode = false;
int s_remin = 99;
// mode
int inet_radio = 0;  //0: DSP radio, 1: Internet radio on
bool led_onoff = true;
int loop_cnt = 0;

// Preset internet station url and name
const char* station_url[]={
  "http://cast1.torontocast.com:2170/stream",  "http://cast1.torontocast.com:2120/stream",
  "http://jenny.torontocast.com:8062/stream",  "http://51.195.203.179:8002/stream",
  "http://216.235.89.171:80/hitlist",  "string 6"
};
const char* station_name[]={
  "JPopSakura",  "JPop hits",
  "J1GOLD",  "HotHits UK",
  "POWERHIT40",  "string 6"
}; // max 10 char
char stnurl[128];  // current internet station url
char stnname[24];  // current internet station name
int max_station = 5; // valid entry
// web server
WebServer server(80);  // port 80(default)
// Operation by server
int stoken = 0;  // server token, count up 
int s_srv = 1;
int s = 1;
int a_srv = 1;
int b_srv = 1;
char titlebuf[166];
char rstr[128];
// DSP Radio RDA5807
int  vol_r;
int  lastvol;
int  stnIdx;
int  laststnIdx;
int  stnFreq[] = {8040, 8250, 8520, 9040, 9150, 7620, 7810, 7860}; // frequency of radio station
String  stnName[] = {"AirG", "NW", "NHK", "STV", "HBC", "sanka", "karos", "nosut"}; // name of radio station max 5 char

String msg = "none";
bool bassOnOff = false;
bool vol_ok = true;
bool stn_ok = true;
bool p_onoff_req = false;
bool p_on = false;
uint32_t currentFrequency;
float lastfreq;
struct elm {  // program
   int stime; // strat time(min)
   int fidx;  // frequency table index
   int duration; // length min
   int volstep; // volume
   int poweroff; // if 1, power off after duration
   int scheduled; // if 1, schedule done for logic
};
struct elm entity[7][MAXSCEDIDX + 1] = {
{{390,1,59,2,1,0},{540,6,59,1,0,0},{600,0,59,1,0,0},{660,3,119,1,0,0},{780,1,59,1,0,0},{840,0,59,1,0,0},{900,6,59,1,0,0},{1140,3,119,1,0,0},{1410,0,29,1,1,0}}, // sun
{{390,4,59,2,1,0},{480,3,119,1,0,0},{600,6,59,1,0,0},{720,2,119,1,0,0},{840,1,119,1,0,0},{0,0,0,0,0,0},{1020,1,119,1,0,0},{1200,6,89,1,0,0},{1410,0,29,1,1,0}}, // mon
{{390,4,59,2,1,0},{480,3,119,1,0,0},{720,2,89,1,0,0},{840,1,119,1,0,0},{0,0,0,0,0,0},{0,0,0,0,0,0},{1020,1,119,1,0,0},{1200,0,89,1,0,0},{1410,0,29,1,1,0}}, // tue
{{390,4,59,2,1,0},{480,3,119,1,0,0},{720,2,89,1,0,0},{840,1,119,1,0,0},{0,0,0,0,0,0},{0,0,0,0,0,0},{1020,1,119,1,0,0},{1200,0,89,1,0,0},{1410,0,29,1,1,0}}, // wed
{{390,4,59,2,1,0},{480,3,119,1,0,0},{600,6,59,1,0,0},{720,2,119,4,0,0},{840,1,119,1,0,0},{960,1,59,1,0,0},{1080,1,119,1,0,0},{1200,0,89,1,0,0},{1290,3,59,1,1,0}}, // thu
{{390,4,59,2,1,0},{480,3,119,1,0,0},{660,0,59,1,0,0},{720,2,119,1,0,0},{840,6,119,1,0,0},{0,0,0,0,0,0},{1080,1,119,1,0,0},{1200,1,89,1,0,0},{1290,3,59,1,1,1}}, // fri
{{390,0,29,2,0,0},{420,2,119,1,0,0},{540,2,110,1,0,0},{720,2,119,1,0,0},{840,2,119,1,0,0},{960,2,119,1,0,0},{1080,4,59,1,0,0},{1140,3,119,1,0,0},{1260,0,89,1,1,0}}  // sat
};
//struct elm rom_entity[7][MAXSCEDIDX + 1];
int split(String data, char delimiter, String *dst){
  int index = 0;
  int arraySize = (sizeof(data))/sizeof((data[0]));
  int datalength = data.length();
  
  for(int i = 0; i < datalength; i++){
    char tmp = data.charAt(i);
    if( tmp == delimiter ){
      index++;
      if( index > (arraySize - 1)) return -1;
    }
    else dst[index] += tmp;
  }
  return (index + 1);
}
int dayofWeek(String dow) {
  dow.trim();
  //Serial.println(dow);
  if (dow.equals("Sun")) return 0; 
  else if (dow.equals("Mon")) return 1;
  else if (dow.equals("Tue")) return 2;
  else if (dow.equals("Wed")) return 3;
  else if (dow.equals("Thu")) return 4;
  else if (dow.equals("Fri")) return 5;
  else if (dow.equals("Sat")) return 6;
  else return 9;
}

Preferences preferences; // Permanent data

int setWeeksced(String val1){
  String instr[12] = {"\n"};
  String instr2[8] = {"\n"};
  String instr3[4] = {"\n"};
  int ix = split(val1,';',instr);
  if (ix != 11) {
    msg = "different number of arguments.";
    return 4;
  } else {
    //msg = "arguments. ok.";
    int down = dayofWeek(instr[0]);
    if (down > 6) { msg = "invalid day of week."; return 4;}
    else {
      // normal process
      msg = "normal process.";
      instr[0].trim();
      Serial.println(instr[0]);
      for(int j = 0; j <= MAXSCEDIDX; j++) {
        instr[j+1].trim();
        msg = "normal process 2.";
        //Serial.println(instr[j+1]);
        String val2 = instr[j+1];
        ix = split(val2,',',instr2);
        if (ix != 5) { 
            msg = "different number of  2nd level arguments.";
            return 4;
        } else {
            //for(int i = 0; i < 5; i++) {
              msg = "OK! Processing.";
              //Serial.println(instr2[i]);
              val2 = instr2[0];
              ix = split(val2,':',instr3);
              if (ix != 2) {
                msg = "different number of  3rd level arguments.";
                return 4;
              }
              instr3[0].trim();
              instr3[1].trim();
              entity[down][j].stime = instr3[0].toInt() * 60 + instr3[1].toInt();
              instr3[0] = "";
              instr3[1] = "";
              instr2[0] = "";

              entity[down][j].fidx = instr2[1].toInt();
              // todo: check fidx
              instr2[1] = "";
              entity[down][j].duration = instr2[2].toInt();
              instr2[2] = "";
              entity[down][j].volstep = instr2[3].toInt();
              instr2[3] = "";
              entity[down][j].poweroff = instr2[4].toInt();
              instr2[4] = "";
              entity[down][j].scheduled = 0; // reset
              preferences.putString(weekStr[down], val1);  // save permanently
              
            //}
        }
        
      }
      msg = "OK! Done.";
      return 0;
    }
  } 
}
void handleRoot(void)
{
  String html;
  String val1;
  String val2;
  String val3;
  String val4;
  String val5;
  String val6;
  String val7;
  String val8;
  String val9;
  String val10;
  String val11;
  String html_btn0 = "<p><div class=\"lay_i\"><input type=\"submit\" name=\"dsp\"  value=\"dsp_radio\" class=\"btn_g\"><input type=\"submit\" name=\"inet\" value=\"inet_radio\" class=\"btn\"></div></p>";
  String html_btn1 = "<p><div class=\"lay_i\"><input type=\"submit\" name=\"dsp\"  value=\"dsp_radio\" class=\"btn\"><input type=\"submit\" name=\"inet\" value=\"inet_radio\" class=\"btn_g\"></div></p>";
  String html_p1; 
  char htstr[180];
  char stnno[4];    
  char tno[4];
  char stnmsg[12];
  bool responsed = false;
  stnmsg[0]=0;

  Serial.println("web received");
  if (inet_radio == 0) html_p1 = html_btn0; else html_p1 = html_btn1;
  if (server.method() == HTTP_POST) { // submitted with string
    val1 = server.arg("daysced");
    val2 = server.arg("vup");
    val3 = server.arg("vdown");
    val4 = server.arg("stnup");
    val5 = server.arg("stndown");
    val6 = server.arg("sleep");
    val7 = server.arg("stoken");
    val8 = server.arg("stnset");
    val9 = server.arg("pwonoff");
    val10 = server.arg("dsp");
    val11 = server.arg("inet");
    if (val7.length() != 0) { // server token
      Serial.print("stoken:");
      String s_stoken = server.arg("stoken");
      int t_stoken = s_stoken.toInt();
      Serial.println(s_stoken);
      msg = "stoken:" + s_stoken;
      if (stoken > t_stoken) {
        Serial.println("redirect");
        msg = "Post converted to Get";
        responsed = true;
        server.send(307, "text/html", "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"0;url=/\"></head></html>");
      }
    }
    if (!responsed){
      ////
      if (val10.length() != 0) {
        Serial.println("dsp");
        if (mode_chg_ok && (inet_radio == 1)) {
          // inet --> dsp
          html_p1 = html_btn0;
          mode_chg_ok = false;
          mode_chg_req = true;
        }
        msg = "dsp";
      }
      else if (val11.length() != 0) {
        Serial.println("inet");
        if (mode_chg_ok && (inet_radio == 0)) {
          // dsp --> inet
          html_p1 = html_btn1;
          mode_chg_ok = false;
          mode_chg_req = true;
        }
        /*WiFiClient client = server.client(); 
        client.println("HTTP/1.1 307 Temporary Redirect");
        client.println("Location: /ir");
        client.println("Connection: Close");
        client.print("<HEAD>");
        client.print("<meta http-equiv=\"refresh\" content=\"0;url=/ir\">");
        client.print("</head>");
        client.println();
        client.stop();*/
        msg = "inet";
      } else if (val8.length() != 0) {
        Serial.println("station set");
        if (val8 == "set") {
          Serial.print("Set_staion: ");
          // Get input station setting
          String s_stnurl = server.arg("stnurl");
          String s_stnname = server.arg("stnname");
          String s_stnno = server.arg("stnno");
          Serial.print(s_stnno); Serial.print(" "); Serial.print(s_stnname); Serial.print(" ");
          Serial.println(s_stnurl);
          // Set current
          strcpy(stnurl,s_stnurl.c_str());
          strcpy(stnname,s_stnname.c_str());
          station = s_stnno.toInt() - 1;
          if (station < max_station && station >= 0) {
            bool conn_ok;
            conn_ok=audio.connecttohost(s_stnurl.c_str()); // change station
            if (conn_ok) {
                Serial.println("Set new staion OK");
                msg = "Set new staion OK";
            } else {
                Serial.println("Set new staion failed");
                strcpy(stnmsg,"Err!");
                msg = "Set new staion error";
            }
          } else msg = "illegal station number";
        } else if (val8 == "save") {
          String s_stnurl = server.arg("stnurl");
          String s_stnname = server.arg("stnname");
          String s_stnno = server.arg("stnno");
          int stnno = s_stnno.toInt();
          Serial.print(s_stnno); Serial.print(" "); Serial.print(s_stnname); Serial.print(" ");
          Serial.println(s_stnurl);
          if (stnno - 1 <= max_station && stnno -1 >= 0) {
            char tstr[166];
            sprintf(tstr,"%s%d","st", stnno);
            preferences.putString(tstr,s_stnurl);
            sprintf(tstr,"%s%d","nm", stnno);
            preferences.putString(tstr,s_stnname);
            msg = "new station saved";
          } else msg = "illegal station number";
        }
        else msg = "ignore it";
      }
      else if (val2.length() != 0) {
        Serial.println("vup");
        b_srv=0; 
        msg = "control vup";
      }
      else if (val3.length() != 0) {
        Serial.println("vdown");
        a_srv=0; 
        msg = "control vdown";
      }
      else if (val4.length() != 0) {
        Serial.println("stnup");
        station_setting();
        msg = "control stnup";
      }
      else if (val5.length() != 0) {
        Serial.println("stndown");
        station_setting();
        msg = "control stndown";
      }
      else if (val6.length() != 0) {
        Serial.println("sleep");
        sleep_setting();
        msg = "control sleep";
      }
      else if (val9.length() != 0) {
        Serial.println("pwonoff");
        if (inet_radio==0) { // dsp radio
          power_onoff_setting(); 
          msg = "control pwonoff";
        } else msg = "ignore it";
      }
      else if (val1.length() != 0) {
        Serial.println("daysched");
        if (inet_radio>=0) { // dsp radio or inet radio
          int rc = setWeeksced(val1); 
          //msg = "control daysched";
        } else msg = "ignore it";
      }
      else {
        // nop        
      } 
    }
  } 
  if (!responsed) {
    html = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Inet Radio</title>";
    html +="</head><body><form action=\"\" method=\"post\">";
    html += "<p><h2>DSP Radio & Internet Radio</h2></p>";
    html += html_p1;
    html += "<style>.lay_i input:first-of-type{margin-right: 20px;}</style>";
    html += "<style>.btn {width: 300px; padding: 10px; box-sizing: border-box; border: 1px solid #68779a; background: #cbe8fa; cursor: pointer;}</style>";
    html += "<style>.btn_y {width: 300px; padding: 10px; box-sizing: border-box; border: 1px solid #68779a; background: #ffff8a; cursor: pointer;}</style>";
    html += "<style>.btn_g {width: 300px; padding: 10px; box-sizing: border-box; border: 1px solid #68779a; background: #99ff99; cursor: pointer;}</style>";
    //html += "<script>function stokenupd() {const hdtoken = document.getElementById('stoken'); hdtoken.value += 1;}</script>";
    html += "<p>Control Functions</p>";
    html += "<p><div class=\"lay_i\"><input type=\"submit\" name=\"vup\"  value=\"volume up\" class=\"btn\"><input type=\"submit\" name=\"vdown\" value=\"volume down\" class=\"btn\"></div></p>";
    html += "<p><div class=\"lay_i\"><input type=\"submit\" name=\"stnup\"  value=\"station change\" class=\"btn\"></div></p>";
    html += "<p><div class=\"lay_i\"><input type=\"submit\" name=\"pwonoff\"  value=\"dsp_radio pwr_on_off\" class=\"btn_y\">";
    html += "<input type=\"submit\" name=\"sleep\"  value=\"inet_radio sleep\" class=\"btn_y\"></div></p>";
    html += "<p>Volume: ";
    if (inet_radio==1) sprintf(tno,"%d",volume); else sprintf(tno,"%d",vol_r);
    html += tno;
    html += "</p>";
    html += "<p>Response: " + msg + "</p>";
    html += "<p><h3>Internet Radio Info</h3></p>";
    html_p1 = String(stnname);
    html += "<p>Now playing: " + html_p1 + " (" + stnurl +  ") "  + "</p>";
    html_p1 = String(titlebuf);
    html += "<p>" + html_p1 + "</p>";
    html += "<p>Station List: </p>";
    html += "<p><ol>";
    for (int j=0; j < max_station; j++) {
      html += "<li>";
      sprintf(htstr, "%.10s%s%s", station_name[j],  " - ",  station_url[j]);
      html +=  htstr;
      html += "</li>";
    }
    html += "</ol></p>";
    html += "<p></p><p>Preference Settings</p>";
    html += "<table style=\”border:none;\”><tr>";
    html += "<td>Station URL:</td>";
    if (strlen(stnmsg)==0) {
      html += "<td></td>";
    } else {
      html += "<td><font color=\"#ff4500\">Err!</font></td>";
    }
    html += "<td><input type=\"text\" name=\"stnno\" size=\"3\" maxlength=\"2\" value=\"";
    html += station + 1;
    html += "\"></td><td>";
    html += "<td><input type=\"text\" name=\"stnname\" size=\"18\" maxlength=\"23\" value=\"";
    html += stnname;
    html += "\"></td><td>";
    html += "<input type=\"text\" name=\"stnurl\" size=\"64\" maxlength=\"127\" value=\"";
    html += stnurl;
    html += "\">";
    html += "<button type=\"submit\" name=\"stnset\" value=\"set\">TEST SET</button>";
    html += "<button type=\"submit\" name=\"stnset\" value=\"save\">SAVE</button>";
    html += "</td></tr></table>";
    html += "<input type=\"hidden\" name=\"stoken\" value=\"";
    stoken += 1;
    html += stoken;
    html += "\">"; 
    //
    html += "<p><h3>Schedule Setting</h3></p>";
    html += "<p>Select a day of the week, change it, then submit.</p>";
    html += "<p>";
    html += "<input type=\"text\" id=\"daysced\" name=\"daysced\" size=\"120\" value=\"\">";
    html += "</p><p><input type=\"submit\" value=\"submit\" class=\"btn\"></p></form>";
    html += "<p>Response: " + msg + "</p>";
    html += "<p>Arguments of enrty: Start time(hour:min),Station(See below),Duration(min),Volume,Pweroff</p>";
    html += "<p>Station List: 0=" + stnName[0] + ",1=" + stnName[1] + ",2=" + stnName[2] + ",3=" + stnName[3] + ",4=" + stnName[4];
    html += ",5=" + stnName[5] + ",6=" + stnName[6] +  ",7=" + stnName[7] + ",abobe 50 = inet_radio</p>";
    html += "<script>";
//    html += "let entity = [[[390,1,59,4,1],[540,6,59,2,0],[600,0,59,2,0],[660,3,119,2,0],[780,1,59,2,0],[840,0,59,2,0],[900,1,59,2,0],[1140,3,119,2,0],[1410,0,29,2,1]],";
//    html += "[[390,1,59,4,1],[540,6,59,2,0],[600,0,59,2,0],[660,3,119,2,0],[780,1,59,2,0],[840,0,59,2,0],[900,1,59,2,0],[1140,3,119,2,0],[1410,0,29,2,1]]]";
    html += "let entity = [";
    for (int i = 0; i < 7; i++){
      html += "[";
      for(int j = 0; j <= MAXSCEDIDX; j++) {
        sprintf(htstr,"['%d:%02d',%d,%d,%d,%d]",entity[i][j].stime / 60,entity[i][j].stime % 60,entity[i][j].fidx,entity[i][j].duration,entity[i][j].volstep,entity[i][j].poweroff);
        html += htstr;
        if (j != MAXSCEDIDX) html += ",";
      }
      html += "]";
      if (i != 6) html += ",";
    }
    html += "];";
    html += "let week = [\"Sun\",\"Mon\",\"Tue\",\"Wed\",\"Thu\",\"Fri\",\"Sat\"];";
    html += "document.write('<table id=\"tbl\" border=\"1\" style=\"border-collapse: collapse\">');";
    html += "for (let i = 0; i < 7; i++){";
    html += "let wstr ='';";
    html += "wstr ='<tr>' + '<td>' + '<input type=\"radio\" name=\"week\" value=\"\" onclick=\"setinput(' + i + ')\">' + '</td>' + '<td>' + week[i] + '</td>';";
    html += "document.write(wstr);";
    html += "for (let j = 0; j < 9; j++){";
    html += "document.write('<td>');";
    html += "document.write(entity[i][j]);";
    html += "document.write('</td>');}";
    html += "document.write('</tr>');";
    html += "}";
    html += "document.write('</table>');";
    html += "function setinput(trnum) {";
    html += "var input = document.getElementById(\"daysced\");";
    html += "var table = document.getElementById(\"tbl\");";
    html += "var cells = table.rows[trnum].cells;";
    html += "let istr = '';";
    html += "for (let j = 1; j <= 10; j++){";
    html += "istr = istr + cells[j].innerText + ';';";
    html += "}";
    html += "input.value = istr;";
    html += "}";
    html += "</script>";
    //
    html += "</form></p></body>";
    html += "</html>";
    server.send(200, "text/html", html);
    Serial.println("web send response");
  }
//}
}

void handleNotFound(void)
{
  server.send(404, "text/plain", "Not Found.");
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(AOUT_SW, OUTPUT);
  digitalWrite(AOUT_SW, HIGH);
  Serial.begin(115200);
  delay(50);
    pinMode(VOL_PIN1, INPUT_PULLUP);
    pinMode(VOL_PIN2, INPUT_PULLUP);
    pinMode(INT_PIN, INPUT_PULLUP);
    pinMode(SLEEP, INPUT_PULLUP); // sleep after about 60 min when button was pressed
    attachInterrupt(INT_PIN,station_setting, CHANGE); // to change station
    attachInterrupt(VOL_PIN1,volup_setting, CHANGE); // to change volue up
    attachInterrupt(VOL_PIN2,voldown_setting, CHANGE); // to change volume down
    attachInterrupt(SLEEP,sleep_setting, CHANGE); // to change sleep on/off
  Wire.setPins(PIN_SDA, PIN_SCL);  
  Wire.begin(); //
    oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS);
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
  // restore saved settings
    preferences.begin("inet_p", false);
    for (int i = 0; i < 7; i++){
       String val1 = preferences.getString(weekStr[i],"");       
       if (val1 != "") {
         //Serial.println(val1);
         int rc = setWeeksced(val1);
       }
    }
    for (int i = 0; i < max_station; i++){
       char tstr[166];
       String *tcp;
       sprintf(tstr,"%s%d","st",i+1);
       String val1 = preferences.getString(tstr,"");       
       if (val1 != "") {
         tcp = new String(val1.c_str());
         Serial.print(tcp->c_str()); Serial.print(",");        
         station_url[i] = tcp->c_str();
         sprintf(tstr,"%s%d","nm",i+1);
         val1 = preferences.getString(tstr,"");
         tcp = new String(val1.c_str());
         Serial.println(tcp->c_str());
         station_name[i] = tcp->c_str();
       }
    }
    volume = preferences.getInt("vol", -1);
    if (volume < 0) volume = 35;
    vol_r = preferences.getInt("volr", -1);
    if (vol_r < 0) vol_r = 3;

    station = preferences.getInt("stn", -1);
    if (station < 0) station = 0;

  event = 0;
  pt2=millis();
    oled.setTextSize(2); // Draw 2X-scale text
    oled.setCursor(0, 0);
    oled.print("Inet Radio");
    oled.display();
    WiFi.mode(WIFI_STA);
    wifiMulti.addAP(ssid.c_str(), password.c_str());  
    //wifiMulti.addAP(ssid2.c_str(), password2.c_str());  // uncomment if you want second wifi access point
    wifiMulti.run();   // It may be connected to strong one
    while (true) {
      if(WiFi.status() == WL_CONNECTED){ break; }  // WiFi connect OK then next step
      Serial.println("WiFi Err");
      oled.setCursor(0, 15);
      oled.print("WiFi Err");
      oled.display();
      WiFi.disconnect(true);
      delay(5000);
      wifiMulti.run();
      delay(1000*300);  // Wait for Wifi ready
    }
      oled.setCursor(0, 15);
      oled.print("WiFi OK");
      oled.display();
  // time 
    wifisyncjst(); // refer time and day
  radio.setup(); // Stats the receiver with default valuses. Normal operation
  delay(100);
  radio.setBand(2); //
  radio.setSpace(0); //
  radio.setVolume(3);
  radio.setFrequency(9040);  // Tune on 90.4 MHz
  delay(100);
  lastfreq = 9040;
  vol_r = 3;
  lastvol = 3;
  laststnIdx = 3;
  stnIdx = 3;
  p_on = true;
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(volume); 
    strcpy(stnurl,station_url[station]); // use preset
    strcpy(stnname,station_name[station]);
    bool conn_ok = false;
    WiFi.setTxPower(WIFI_POWER_17dBm); // TX power strong   #### xiao < 17dbm
    Serial.println("TX power 17dBm");
    /*conn_ok = audio.connecttohost(stnurl); 
    if (!conn_ok) { // conect failure
      Serial.println("Fail to connect");

    } else { // connect ok
      Serial.println("Internet radio connect ok");
      inet_radio = 0;  //1: Internet radio
    }*/
    server.on("/", handleRoot);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.print("IP = ");
    Serial.println(WiFi.localIP());
    IPAddress ipadr = WiFi.localIP();
      oled.setCursor(0, 30);
      oled.printf("IP:%d.%d", ipadr[2],ipadr[3]);
      oled.display();
    titlebuf[0] = 0;
    //WiFi.setTxPower(WIFI_POWER_17dBm); // TX power 
    digitalWrite(LED_BUILTIN, LOW); // led on
    delay(200);

}
void wifisyncjst() {
  // get jst from NTP server
  int lcnt = 0;
  configTzTime("JST-9", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
  delay(500);
  // get sync time
  while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
    delay(500);
    lcnt++;
    if (lcnt > 100) {
      Serial.println("time not sync within 50 sec");
      break;
    }
  }
}
void loop()
{
  char ts[80];
  char slp[8];
  float tf;
  // check web server req
  server.handleClient();
  //
  if (mode_chg_req) { // mode change request
    if (inet_radio==0) { // dsp -> inet
      strcpy(stnurl,station_url[station]); // use preset
      strcpy(stnname,station_name[station]);
      bool conn_ok = false;
      WiFi.setTxPower(WIFI_POWER_17dBm); // TX power strong   #### xiao < 17dbm
      Serial.println("TX power 17dBm");
      conn_ok = audio.connecttohost(stnurl); 
      if (!conn_ok) { // conect failure
        Serial.println("Fail to connect");

      } else { // connect ok
        
        inet_radio = 1;  //1: Internet radio
        digitalWrite(AOUT_SW, LOW);
        radio.powerDown();
        audio.setVolume(volume); 
        p_on = false;
      }
      mode_chg_ok = true;
      mode_chg_req = false;
    } else { // inet -> dsp
      audio.stopSong(); // inet stop, then set dsp_radio on
      server.stop();
      // Reconnect WiFi to quit audio
      WiFi.disconnect(true);
      Serial.println("WiFi Reconnect");
      oled.setTextSize(2); // Draw 2X-scale text
      oled.clearDisplay();
      oled.setCursor(0, 0);
      oled.print("WiFi Recon");
      oled.display();
      delay(1000);
      wifiMulti.run();
      delay(1000);
      while (true) {
        if(WiFi.status() == WL_CONNECTED){ break; }  // WiFi connect OK then next step
        Serial.println("WiFi Err");
        oled.setCursor(0, 15);
        oled.print("WiFi Err");
        oled.display();
        WiFi.disconnect(true);
        delay(1000*300);  // Wait for Wifi ready
      }
      server.on("/", handleRoot);
      server.onNotFound(handleNotFound);
      server.begin();
        
      digitalWrite(LED_BUILTIN, LOW);     // led on  
      p_onoff_req = true;
      inet_radio = 0;  //0: DSP radio
      mode_chg_ok = true;
      mode_chg_req = false;
    }
  }
  
  if (inet_radio==1 || inet_radio==0) { // process server request
    if (a_srv==0 || b_srv==0) { // by server operation
      Serial.println("Server vol change");
      updatevolume(a_srv, b_srv);
      a_srv=1;
      b_srv=1;
    }
    if (s==0 || s_srv==0) { // button or by server operation
      sleepmode = !sleepmode;
      if (sleepmode) {
        Serial.println("Sleep timer ON");
        s_remin = 60;
      } else {
        Serial.println("Sleep timer OFF");
        s_remin = 60;
      }
      s=1;
      s_srv=1;
    }
  }
  if (inet_radio==1) {
    loop_cnt ++;
    if (loop_cnt > 500) { // about 3 sec in normal process
      loop_cnt = 0;
      led_onoff = !led_onoff;
      if (led_onoff) {
        digitalWrite(LED_BUILTIN, LOW); // on
        oled_display(1); // inet info
      }
      else {
        digitalWrite(LED_BUILTIN, HIGH); // off
        oled_display(0); // time
      }
    }
    audio.loop(); // Inter net radio function call
  }
  if ((event==1) && (inet_radio==1)) { // interrupt of change station request ?
      char tstr[166];
      event = 0; // clear 
      strcpy(stnurl,station_url[station]); 
      strcpy(stnname,station_name[station]);
      audio.connecttohost(station_url[station]); // change station.
      // save change
      sprintf(tstr,"%s%d","st", station + 1);
      preferences.putString(tstr,stnurl);
      sprintf(tstr,"%s%d","nm", station + 1);
      preferences.putString(tstr,stnname);
      preferences.putInt("stn",station);
  }
  if (inet_radio==0) {
    if (p_onoff_req) {
      if (p_on) {
        radio.powerDown();
        Serial.println("radio pw off");
        p_on = false;
      } else {
        radio.powerUp();
        delay(50);
        radio.setFrequency(stnFreq[stnIdx]);
        radio.setVolume(vol_r);
        delay(50);
        lastfreq = stnFreq[stnIdx];
        laststnIdx = stnIdx;
        Serial.println("radio pw on");
        digitalWrite(AOUT_SW, HIGH);
        p_on = true;
      }
      p_onoff_req = false;
    }
    if (laststnIdx != stnIdx) {
      Serial.println("stn changed");
      radio.setFrequency(stnFreq[stnIdx]);
      lastfreq = stnFreq[stnIdx];
      laststnIdx = stnIdx;
      stn_ok = true;
    }
    oled.clearDisplay();
    oled.setTextSize(2); // Draw 2X-scale text
    // display current time
    time_t t = time(NULL);
    tm = localtime(&t);
    d_mon  = tm->tm_mon+1;
    d_mday = tm->tm_mday;
    d_hour = tm->tm_hour;
    d_min  = tm->tm_min;
    d_sec  = tm->tm_sec;
    d_wday = tm->tm_wday;
    //Serial.print("time ");
    if (last_d_sec != d_sec) {
      if (sleepmode) sprintf(slp,"%s","SLP:"); else sprintf(slp,"%s","Vol:");
      sprintf(ts, "%02d-%02d %s", d_mon, d_mday, weekStr[d_wday]);
      oled.setTextSize(2); // Draw 2X-scale text
      oled.clearDisplay();
      oled.setCursor(0, 0);
      oled.print(ts);
      //Serial.println(ts);
      sprintf(ts,"%02d:%02d:%02d",d_hour,d_min,d_sec);
      oled.setCursor(0, 15);
      oled.print(ts);
      //Serial.println(ts);
      int pi = p_on ? 1 : 0;
      sprintf(ts, "%.4s%02d %s%01d", slp, vol_r, "P:", pi);
      //u8g2.drawStr(0, 20, ts);
      oled.setCursor(0, 30);
      oled.print(ts);
      tf = lastfreq/100.0;
      sprintf(ts, "%3.1f %s", tf, "MHz");
      oled.setCursor(0, 45);
      oled.print(ts);
      oled.display();
      last_d_sec = d_sec; // 
      delay(300); // if inet radio off , delay ok here
    }
  }
  // Check Schedule 
  if (last_d_min != d_min) {
    last_d_min = d_min;
    if (pofftm_h == d_hour && pofftm_m == d_min && p_on) { // power off time ?
      p_onoff_req = true;
      pofftm_h = 0;
      pofftm_m = 0;
    } else {
      for(int i = 0; i <= MAXSCEDIDX; i++) {
        if (entity[d_wday][i].stime == 0 ) {     
          //nop
          //Serial.println(d_min);
        } else {
          //Serial.println(entity[d_wday][i].stime);
          if ((entity[d_wday][i].stime <= d_hour * 60 + d_min) && 
              ((entity[d_wday][i].stime + entity[d_wday][i].duration) >= (d_hour * 60 + d_min ))
              && (entity[d_wday][i].scheduled != 1)) {
            if (lastfreq == stnFreq[entity[d_wday][i].fidx] && entity[d_wday][i].fidx < 50 && inet_radio==0) {
              //entity[d_wday][i].scheduled = 1; // mark it scheduled
            } else {          
              //radio.setFrequency(stnFreq[entity[d_wday][i].fidx]);
              int  tstnIdx =  entity[d_wday][i].fidx;
              if (tstnIdx >= 50) {
                int  lstation = station;
                station = tstnIdx - 50;
                if (station > max_station) station = 0;
                strcpy(stnurl,station_url[station]); 
                strcpy(stnname,station_name[station]);
                if (inet_radio==0) mode_chg_req = true;
                if (inet_radio==1 && station != lstation) event = 1;       // change station
                volume = entity[d_wday][i].volstep; // inet
              } else {
                stnIdx = tstnIdx;
                if (inet_radio==1) mode_chg_req = true;
                vol_r = entity[d_wday][i].volstep;    // dsp
              }
            }                     
            
            currIdx = i;
            entity[d_wday][i].scheduled = 1; // mark it scheduled
            Serial.println("scheduled");
            if (entity[d_wday][i].poweroff==1) { // power off ?
              pofftm_h = (entity[d_wday][i].stime + entity[d_wday][i].duration) / 60; // set power off time
              pofftm_m = (entity[d_wday][i].stime + entity[d_wday][i].duration) % 60;
              sprintf(ts,"%02d:%02d %s",pofftm_h,pofftm_m,"poff scheduled");
              Serial.println(ts);
            }
            if (p_on==false && (inet_radio == 0 && !mode_chg_req)) {  // dsp_radio
              p_onoff_req = true;  //  if power off currently then power on req
              pofftm_h = 0;        // reset
              pofftm_m = 0;
              Serial.println("pw on req");
            } 
          } 
        }
      }
    }
  }
}

// display
void oled_display(int dmode){
  char ts[80];
  char buf[64];
  char buf1[64];
  char slp[8];
  oled.clearDisplay();
  oled.setTextSize(2); // Draw 2X-scale text
  if (dmode==0) { // time
    // display current time
    time_t t = time(NULL);
    tm = localtime(&t);
    d_mon  = tm->tm_mon+1;
    d_mday = tm->tm_mday;
    d_hour = tm->tm_hour;
    d_min  = tm->tm_min;
    d_sec  = tm->tm_sec;
    d_wday = tm->tm_wday;
    //Serial.print("time ");
    if (last_d_sec != d_sec) { // inet info
      sprintf(ts, "%02d-%02d %s", d_mon, d_mday, weekStr[d_wday]);
      oled.setTextSize(2); // Draw 2X-scale text
      oled.setCursor(0, 0);
      oled.print(ts);
      //Serial.println(ts);
      sprintf(ts,"%02d:%02d:%02d",d_hour,d_min,d_sec);
      oled.setCursor(0, 15);
      oled.print(ts);
      //Serial.println(ts);
      last_d_sec = d_sec; // 
    }
  } else {
    if (sleepmode) {
      sprintf(buf1,"SLP%02d V:%02d", s_remin, volume);
    } else {
      sprintf(buf1,"NET Vol:%02d", volume);
    }
    sprintf(buf,"%.10s",stnname);
    oled.setCursor(0, 0);
    oled.print(buf1);
    oled.setCursor(0, 15);
    oled.print(buf);
  }
  oled.setCursor(0, 35);
  oled.setTextSize(1); // Draw 1X-scale text
  oled.print(titlebuf); // title info
  oled.display();
  if (sleepmode) {
    if (last_d_min != d_min) {
      s_remin --;
      if (s_remin==0) {  // fall asleep
        oled.clearDisplay();  // put off
        oled.display(); 
        digitalWrite(LED_BUILTIN, HIGH); // put off
        esp_deep_sleep_start();  // sleep forever
      }
      last_d_min = d_min;
    }
  }
}

// change volume
void updatevolume(uint8_t a, uint8_t b)
{
  if (b==0) { // push vol up
    if (inet_radio==1) {
      volume = (volume/5)*5 + 5;
      if (volume > MAXVOL) volume = MAXVOL;// inet
    }    
    else if (inet_radio==0) {
      vol_r ++;
      if (vol_r > MAXVOL_R) vol_r = MAXVOL_R; // dsp
    }
  } else if (a==0) { // push vol down
    if (inet_radio==1) {
      volume = (volume/5)*5- 5;      
      if (volume < 0) volume = 0;
    }
    else if (inet_radio==0) {
      vol_r --;
      if (vol_r < 0) vol_r = 0;
    } 
  }
  Serial.print("vl:"); 
  if (inet_radio==1) {
    Serial.println(volume);
    audio.setVolume(volume);
    preferences.putInt("vol",volume);
  } else if (inet_radio==0) {
    Serial.println(vol_r);
    radio.setVolume(vol_r);
    preferences.putInt("vol_r",vol_r);
  }
}
// volume int routine
void volup_setting(){
  ct2=millis();
  if ((ct2-pt2)>250) {
    updatevolume(1, 0);
  }
  pt2=ct2;
}
void voldown_setting(){
  ct2=millis();
  if ((ct2-pt2)>250) {
    updatevolume(0, 1);
  }
  pt2=ct2;
}
// optional
void audio_info(const char *info){
    Serial.print("info        "); Serial.println(info);
}
void audio_id3data(const char *info){  //id3 metadata
    Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_mp3(const char *info){  //end of file
    Serial.print("eof_mp3     ");Serial.println(info);
}
void audio_showstation(const char *info){
    Serial.print("station     ");Serial.println(info);
}
void audio_showstreaminfo(const char *info){
    Serial.print("streaminfo  ");Serial.println(info);
}
void audio_showstreamtitle(const char *info){
    sprintf(titlebuf,"%.128s",info);  // server data
    oled_display(1); // display inet info
}
void audio_bitrate(const char *info){
    Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info){  //duration in sec
    Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
    Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    Serial.print("lasthost    ");Serial.println(info);
}
void audio_eof_speech(const char *info){
    Serial.print("eof_speech  ");Serial.println(info);
}
void power_onoff_setting() {
  if (p_onoff_req==false) {  // wait last req
     p_onoff_req = true;  // req
  }
}
// sleep int routine
void sleep_setting(){
  ct2=millis();
  if ((ct2-pt2)>250) {
    s = 0;  // sleep on
  }
  pt2=ct2;
}
void station_setting1() { //DSP Radio
  if (stn_ok) {  // wait last req
    stnIdx++;
    stn_ok = false;
    if (stnIdx > MAXSTNIDX) stnIdx = 0;  // turn around to support single button
    Serial.println(stnIdx);
  }
}
// station change int routine
void station_setting(){
  ct2=millis();
  //delay(10);  // no effect here
  if ((ct2-pt2)>250) {
    Serial.print("st:");
    if (inet_radio==1) {
      station = station + 1;  // inet
      if (station>=max_station) station=0;
      Serial.println(station);
      event=1; 
    }
    else if (inet_radio==0) station_setting1(); // dsp radio
  }
  pt2=ct2;
 } 