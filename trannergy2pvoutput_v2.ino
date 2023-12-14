/*********************************************************
 * 
 * Arjen Vellekoop
 * December 2023
 * Trannergy Solar logger to PVoutput.org
 * 
 * V2.0
 * 
 * Connects to Trannergy (or Omnik) Inverter and reads its data
 * Every 5 minutes it uploads this data to PVoutput
 * 
 * Data can be read via telnet.
 *
 * 
 *  Respons format Trannergy or Omnik
 *  
 *  Seems to be ready for 3 solar strings
 *  
 *  
 *  BYTE                                              nr of bytes
 *   0:   ? (start message?)                          3
 *   4:   Twice SerialNumber of the datalogger        8
 *  12:   ?
 *  15:   SerialNumber Inverter                       16
 *  31:   temperature * 10                            2
 *  33:   Vpv1 * 10                                   2
 *  35:   Vpv2 * 10                                   2
 *  37:   ? Vpv3 *10                                  2
 *  39:   Probably Ipv1                               2
 *  41:   Probably Ipv2                               2
 *  43:   Probably Ipv3                               2
 *  45:   Iac1 * 10                                   2
 *  47:   Probably Iac2 * 10                          2
 *  49:   Probably Iac3 * 10                          2
 *  51:   Vac1 * 10                                   2
 *  53:   Probably Vac2 * 10                          2
 *  55:   Probably Vac3 * 10                          2
 *  57:   Fac * 100 (gridfrequency)                   2
 *  59:   Pac1                                        2
 *  61:   Probably Pac2                               2
 *  63:   Probably Pac3                               2
 *  65:   ??                                          4
 *  69:   Daily yield kWh * 100                       2
 *  71:   ??                                          2
 *  73:   Total yield in kWh * 10 since reset         2
 *  75:   Might be 2 extra bytes for the total yield  2
 *  77:   On-time                                     2
 *  79 -96 ???
 * 101   Might be checksum                           1
 * 102:   ? Seems to be always the same (Omnik)
 * 107:   2x serial Number datalogger (Omnik)         8
 * 115:   "DATA SEND IS OK" (Omnik)         
 * 130:   ?? End message?                             2
 *
 ***********************************************************/
 
 
 
 
 /****LIBRARIES**************************************************/
#include "secrets.h"
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <time.h>
//  https://github.com/jandrassy/TelnetStream
#include <TelnetStream.h>       // Version 0.0.1
#include <coredecls.h> // optional settimeofday_cb() callback to check on server

/****TEMPEARTURE INIT********************************************/
// Temperature init. DS18B20 at pin 14 (optional)
#define ONE_WIRE_BUS 14 
OneWire oneWire(ONE_WIRE_BUS); 
DallasTemperature sensors(&oneWire);

/**GLOBAL VARIABLES************************************************/
#define Version "Trannergy2PVOutput V2.0_Dec_8_2023"
byte magicmessage[] = {0x68, 0x02, 0x40, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x16};
char  inputc;
long serial_number = TrannergySN;
long SN;
unsigned char  InverterID[20];
unsigned char  server_reply[256];
int i, response_length;

float Trannergy_temperature;
float PVVoltageDC;
float IVCurrentDC;
float PVVoltageAC;
float IVCurrentAC;
float frequency;
float PVPower;
float PowerToday;
float TotalPower;
float TotalHours;

int timezone = 1 * 3600;
int dst = 1 * 3600;

char current_date[10], current_time[10];
unsigned long lasttime, time4data;

boolean SoftDebug=false;
rst_info *xyz;
time_t now;
tm timeinfo;

/*
String weekday;
char daysOfTheWeek[7][10] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char Thismonth[12][10] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
*/



void time_is_set(bool from_sntp /* <= this optional parameter can be used with ESP8266 Core 3.0.0*/) {
  Serial.printf("time was sent! from_sntp=%i", from_sntp);
  TelnetStream.printf("time was sent! from_sntp=%i", from_sntp);
}
uint32_t sntp_update_delay_MS_rfc_not_less_than_15000 () {
  return 60 * 60 * 1000UL; // In milli Seconds every 60 minutes
}

/******SETUP******************/


void setup(void) 
{ 
  int checksum=0;
  Serial.begin(115200);            // start serial port 
  xyz = ESP.getResetInfoPtr();
  sensors.begin(); // Start temp sensor
  sensors.setResolution(9);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("ESPSolar");
  
  //connect to your local wi-fi network
  WiFi.begin(SECRET_SSID, SECRET_PASS);

  //check wi-fi is connected to wi-fi network
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.print("WiFi connected..! ");
  Serial.print(SECRET_SSID);
  Serial.print(" ");
  Serial.println(WiFi.localIP());
  Serial.printf("Version: %s\n\r", Version); // print the version
  // generate the magic message
  // first 4 bytes are fixed x68 x02 x40 x30
  // next 8 bytes are the reversed serial number twice(hex)
  // next 2 bytes are fixed x01 x00
  // next byte is a checksum (2x each binary number from the serial number + 115)
  // last byte is fixed x16
  
  for (i=0; i<4; i++) {
    magicmessage[4+i] = ((serial_number>>(8*i))&0xff);
    magicmessage[8+i] = ((serial_number>>(8*i))&0xff);
    checksum += magicmessage[4+i];
  }
  magicmessage[14] = ((checksum*2 + 115)&0xff);

  if (DEBUG) {
    for (i=0;i<16;i++) {
      Serial.print(magicmessage[i],HEX);
      Serial.print(" ");
    }
    Serial.println();
  }

  /*********** TIMEZONE ********************/
  configTime(timezone, dst, "pool.ntp.org","time.nist.gov");
  setenv("TZ", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
  tzset();
  Serial.println("\nWaiting for NTP...");

  time(&now);                   // this function calls the NTP server only every hour
  localtime_r(&now, &timeinfo); // converts epoch time to tm structure  now = time(nullptr);
  while(now < 1631544104){  // sept 2021
    delay(1000);            // wait a second
    time(&now);                   // this function calls the NTP server only every hour
    localtime_r(&now, &timeinfo); // converts epoch time to tm structurenow = time(nullptr);
    Serial.printf("%lld ", now);
    strftime(current_date, sizeof current_date, "%Y%m%d", &timeinfo); 
    strftime(current_time, sizeof current_time, "%H:%M", &timeinfo);
    Serial.printf("%s %s\n\r", current_date, current_time);
  }
  TelnetStream.begin();  // start Telnet server
  lasttime = now; // to avoid immediate upload
  time4data = now;
  settimeofday_cb(time_is_set); // optional: callback if time was sent
  TelnetStream.flush();
  
} 

/************** LOOP *******************/

void loop(void) {
  unsigned errflag=0;
  unsigned long timeout;

  switch (toupper(TelnetStream.read())) { // some commands for telnet output
    case 'R':
    TelnetStream.stop();
    delay(100);
    ESP.reset();
      break;
    case 'C':
      TelnetStream.println("bye bye");
      TelnetStream.flush();
      TelnetStream.stop();
      break;
    case 'F':
      TelnetStream.printf("Freeheap: %d\n\r", ESP.getFreeHeap());
      break;
    case 'B':
      TelnetStream.printf("Latest reset reason: %u\n\r", xyz->reason);
      break;
    case 'V':
      TelnetStream.printf("Version: %s\n\r", Version);
      break;
    case 'D': // Toggle Software Debug
      SoftDebug = !SoftDebug;
      TelnetStream.printf("Debug: %i\n\r", SoftDebug);
      break;
    case 'H':
      TelnetStream.println("R: Reset");
      TelnetStream.println("C: Clear and stop");
      TelnetStream.println("F: FreeHeap");
      TelnetStream.println("B: Reboot report");
      TelnetStream.println("V: Version");
      TelnetStream.println("D: Toggle Debug");
      TelnetStream.println("L: Latest output");
      break;
    case 'L':
      Printalloutput();
      break;
    default:
      while(TelnetStream.available())
        TelnetStream.read(); // empty buffer. Required because at startup the buffer is filled with garbage
                            // and after a single letter command there is still a return and Newline.
  } //end switch

  time(&now);                   // this function calls the NTP server only every hour
  localtime_r(&now, &timeinfo); // converts epoch time to tm structure
  TelnetStream.printf("Please Wait for it (60s) %lld\r", now-time4data);
  
  if ((now - time4data) > 60) { // Every minute
    time4data = now;
    sensors.requestTemperatures(); // Send the command to get temperature readings 
    message("Temperature is: "); 
    messageln(String(sensors.getTempCByIndex(0)));

    delay(100); // might be for steady state...
    message("Analog input: ");
    messageln(String(analogRead(A0)));

//    time_t now = time(nullptr);
//    now = time(nullptr);
    time(&now);
    localtime_r(&now, &timeinfo); // converts epoch time to tm structure

    if (SoftDebug) {
      message("year: ");messageln(String(timeinfo.tm_year+1900));
      message("Month: ");messageln(String(timeinfo.tm_mon+1));
      message("day: ");messageln(String(timeinfo.tm_mday));
      message("hour: ");messageln(String(timeinfo.tm_hour));
      message("minutes: ");messageln(String(timeinfo.tm_min));
      message("secs: ");messageln(String(timeinfo.tm_sec));
    }

    // some date/time formatting we'll need for PVOUTPUT
    strftime(current_date, sizeof current_date, "%Y%m%d", &timeinfo); 
    message(current_date);
    message(" ");
    strftime(current_time, sizeof current_time, "%H:%M", &timeinfo);
    messageln(current_time);

  
    WiFiClient client;
    if (client.connect(TrannergyURL, TrannergyPort)) {
      messageln("Connected to Inverter!");
      delay(100);
      client.write((const uint8_t*)magicmessage, (uint8_t) 16);
      messageln("");

      errflag = 0;
      timeout = millis();
      while (!client.available() && !errflag) {
        if (millis() - timeout > 5000) {
          messageln(">>> Client Timeout !");
          client.stop();
          errflag = 1;
        }
      }
      if (!errflag) {
        // Read all the lines of the reply from server and print them to Serial
        response_length = 0;
        while (client.available() && (response_length<255)) {
          server_reply[response_length] = client.read();
          if (SoftDebug) {
            Serial.printf("%i\t", response_length);
            TelnetStream.printf("%i\t", response_length);
            Serial.print(server_reply[response_length],HEX);
            TelnetStream.print(server_reply[response_length],HEX);
            if ((server_reply[response_length] > '/') && (server_reply[response_length] < '{')) {
              Serial.printf("\t%c", server_reply[response_length]);
              TelnetStream.printf("\t%c", server_reply[response_length]);
            }
            messageln("");
          }
          response_length++;
          if (response_length>255){
            message("response longer than expected!");
            errflag = 2;
          }
          delay(10); // seems necessary for all the bytes to arrive
        }
        Serial.printf("Total bytes received: %i\n\n", response_length);
        TelnetStream.printf("Total bytes received: %i\n\n", response_length);
        client.stop();

        if (!errflag) {   // Extract the values from the reponse (if any)
          SN=0;
          message("Serial Number: ");
          for (i=0; i<4; i++) {
            Serial.printf("%x", (unsigned char) server_reply[7-i]);
            TelnetStream.printf("%x", (unsigned char) server_reply[7-i]);
            SN += (long) (unsigned char) server_reply[7-i] * pow(256, (3-i));
          }
          Serial.printf("\t\t%ld\n", SN);
          TelnetStream.printf("\t\t%ld\n", SN);
          strncpy((char *) InverterID, (const char *) &server_reply[15], 16);
          InverterID[16] = 0;
          Serial.printf("ID Inverter: \t\t\t%s\n", InverterID);
          TelnetStream.printf("ID Inverter: \t\t\t%s\n", InverterID);

          Trannergy_temperature = ctonr(&server_reply[31], 2, 10);
          PVVoltageDC =           ctonr(&server_reply[33], 2, 10);
          IVCurrentDC =           ctonr(&server_reply[39], 2, 10);
          PVVoltageAC =           ctonr(&server_reply[51], 2, 10);
          IVCurrentAC =           ctonr(&server_reply[45], 2, 10);
          PVPower =               ctonr(&server_reply[59], 2, 1);
          frequency =             ctonr(&server_reply[57], 2, 100);
          PowerToday =      1000* ctonr(&server_reply[69], 2, 100);
          TotalPower =            ctonr(&server_reply[71], 4, 10);
          TotalHours =            ctonr(&server_reply[75], 4, 1);

          Printalloutput();
        }
      }
    }
    else {
      errflag=5; // there was no connection
      messageln("No connection to Inverter");
    }

    message("Last Upload since (secs) : ");
    messageln(String(now-lasttime));
  
    if (((now-lasttime) > 300) && (!errflag)) {    // every 5 minutes upload data
      lasttime = now;
    
      // the curse for PVOutput:
      char pvstring[200];
      sprintf(pvstring, "GET %s?d=%s&t=%s&v1=%.0f&v2=%.0f&v5=%.1f&v6=%.1f&key=%s&sid=%d HTTP/1.1\r\nHost: %s \r\nConnection: close\r\n\r\n",
                PVoutputURL,
                current_date,
                current_time,
                PowerToday,
                PVPower,
                Trannergy_temperature,
                PVVoltageDC,
                PVoutputapi, 
                SystemID,
                HostURL);

      if (SoftDebug) {
        messageln(pvstring);
      }
      messageln("Trying to connect to PVOutput..");

      WiFiClient client;
      const int httpPort = 80;
      if (!client.connect(HostURL, httpPort)) {
        messageln("connection failed");
      }
      else {
        messageln("PVOutput Connected!");
        // This will send the request to the server
        client.print(pvstring);
        errflag = 0;
        timeout = millis();
        while (!client.available() && !errflag) {
          if (millis() - timeout > 5000) {
            messageln(">>> Client Timeout !");
            client.stop();
            errflag = 1;
          }
        }
        if (!errflag) {
          // Read all the lines of the reply from server and print them to Serial
          while(client.available()) {
            String line = client.readStringUntil('\n');
            messageln(line);
          }
          messageln("");
          messageln("Disconnect PVOutput....");
          message("");
          client.stop();
        }
        else {
          // Parser error, print error
          messageln(String(errflag));
        }
      }
    }
  }
  delay(2000);
}
        

/**************** FUNCTIONS ******************/

float ctonr(unsigned char * src, int nrofbytes, int div) {
  int i, flag=0;
  float sum=0;

//  sanity check
  if (nrofbytes<=0 || nrofbytes>4) 
    return -1;

  for (i=nrofbytes; i>0; i--) {
    sum += (float) (src[i-1] * pow(256, nrofbytes-i));
    if (src[i-1] == 0xff)
      flag++;
  }
  if (flag == nrofbytes) // all oxff
    sum = 0;
  sum /= (float) div;
  return sum;
}
void message(String msg) {
  Serial.print(msg);
  TelnetStream.print(msg);
}
void messageln(String msg) {
  Serial.println(msg);
  TelnetStream.println(msg);
}

void  Printalloutput(void) {
  Serial.printf("Temperature:\t\t\t%.1f\n", Trannergy_temperature);
  Serial.printf("PV1 Voltage (DC):\t\t%.1f\n", PVVoltageDC);
  Serial.printf("IV1 Current (DC):\t\t%.1f\n", IVCurrentDC);
  Serial.printf("PV1 Voltage (AC):\t\t%.1f\n", PVVoltageAC);
  Serial.printf("IV1 Current (AC):\t\t%.1f\n", IVCurrentAC);
  Serial.printf("PV1 Power:\t\t\t%.0f\n", PVPower);
  Serial.printf("Frequency (AC):\t\t\t%.2f\n", frequency);
  Serial.printf("Total Power today (Wh):\t\t%.0f\n", PowerToday);
  Serial.printf("Total Power since reset (kWh):\t%.1f\n", TotalPower);
  Serial.printf("Total Hours since reset:\t%.0f\n\n", TotalHours);
  TelnetStream.printf("Temperature:\t\t\t%.1f\n", Trannergy_temperature);
  TelnetStream.printf("PV1 Voltage (DC):\t\t%.1f\n", PVVoltageDC);
  TelnetStream.printf("IV1 Current (DC):\t\t%.1f\n", IVCurrentDC);
  TelnetStream.printf("PV1 Voltage (AC):\t\t%.1f\n", PVVoltageAC);
  TelnetStream.printf("IV1 Current (AC):\t\t%.1f\n", IVCurrentAC);
  TelnetStream.printf("PV1 Power:\t\t\t%.0f\n", PVPower);
  TelnetStream.printf("Frequency (AC):\t\t\t%.2f\n", frequency);
  TelnetStream.printf("Total Power today (Wh):\t\t%.0f\n", PowerToday);
  TelnetStream.printf("Total Power since reset (kWh):\t%.1f\n", TotalPower);
  TelnetStream.printf("Total Hours since reset:\t%.0f\n\n", TotalHours);
}
