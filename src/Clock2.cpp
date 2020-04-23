#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>

#include "LedControl.h"

const char *ssid = "your_network";
const char *password = "your_password";

#define MAX_DATAIN 12
#define MAX_CLK 14
#define MAX_LOAD 16
#define MAX_DISPLAYS 3

#define AUTONOMOUS_WORK 60000 // 60 * 10 * 100, in 10 millisec intervals

unsigned long bufferLong[14] = {0};
const long scrollDelay = 75; // adjust scrolling speed

LedControl lc = LedControl(MAX_DATAIN, MAX_CLK, MAX_LOAD, MAX_DISPLAYS);

/* we always wait a bit between updates of the display */
unsigned long delaytime = 100;
unsigned int localPort = 2390;            // local port to listen for UDP packets
IPAddress timeServer1(192, 168, 1, 1);    // you LAN server (if it supports NTP)
IPAddress timeServer2(89, 179, 240, 132); // 1.debian.pool.ntp.org NTP server
IPAddress timeServers[2];
IPAddress timeServerCur;
const int NTP_PACKET_SIZE = 48;     // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;
ESP8266WebServer server(80);

void sendNTPpacket(IPAddress &address);
void displayString(char str[]);
void scrollFont();
void printBufferLong();
void rotateBufferLong();
void loadBufferLong(int ascii);

void handle_root()
{
  server.send(200, "text/plain", "hello from esp8266!");
}

void setup()
{
  WiFi.mode(WIFI_STA);

  // put your setup code here, to run once:
  Serial.begin(38400);

  // Connect to WiFi network
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  server.on("/", handle_root);
  server.begin();

  for (int iter = 0; iter < 5; iter++)
  {
    //we have to init all devices in a loop
    for (int address = 0; address < MAX_DISPLAYS; address++)
    {
      /*The MAX72XX is in power-saving mode on startup*/
      lc.shutdown(address, false);
      /* Set the brightness to a medium values */
      lc.setIntensity(address, 1);
      /* and clear the display */
      lc.clearDisplay(address);
    }
  }

  timeServers[0] = timeServer1;
  timeServers[1] = timeServer2;
  timeServerCur = timeServers[0];
}

char buf[5];
int tmp;

unsigned long globalCounter = 0;
unsigned long ntpCounter = 0;
unsigned long epoch = 0;
bool packetSent = false;
bool alarmOn = true;
int timeServerCounter = 0;

void loop()
{
  //Serial.println("========== LOOP");
  //scrollMessage(scrollText);
  //scrollFont();

  if (ntpCounter == 0)
  {
    //Serial.print("========== sending packet.....");
    sendNTPpacket(timeServerCur); // send an NTP packet to a time server
    packetSent = true;
  }

  if (packetSent && (ntpCounter > 90))
  {
    //Serial.print("========== parsing packet.....");
    int cb = udp.parsePacket();
    if (!cb)
    {
      //Serial.println("no packet yet");
      //lc.setLed(1, 3, 7, true);
      if (ntpCounter > 120)
      {
        alarmOn = true;
        ntpCounter = -1;
        packetSent = false;
        timeServerCounter++;
        timeServerCounter %= 2;
        timeServerCur = timeServers[timeServerCounter];
        if (!WiFi.isConnected())
        {
          // Connect to WiFi network
          WiFi.reconnect();

          // Wait for connection
          while (WiFi.status() != WL_CONNECTED)
          {
            delay(200);
            Serial.print("r");
          }
        }
      }
    }
    else
    {
      alarmOn = false;
      //Serial.print("packet received, length=");
      //Serial.println(cb);
      // We've received a packet, read the data from it
      udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

      //the timestamp starts at byte 40 of the received packet and is four bytes,
      // or two words, long. First, esxtract the two words:

      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      // combine the four bytes (two words) into a long integer
      // this is NTP time (seconds since Jan 1 1900):
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      //Serial.print("Seconds since Jan 1 1900 = " );
      //Serial.println(secsSince1900);

      // now convert NTP time into everyday time:
      //Serial.print("Unix time = ");
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;
      // subtract seventy years:
      epoch = secsSince1900 - seventyYears;
      // print Unix time:
      //Serial.println(epoch);

      // +3 Moscow Time Zone
      epoch += 3 * 3600;

      packetSent = false;
    }
  }

  if ((globalCounter % 100) == 0)
  {
    int pos = 0;
    // print the hour, minute and second:
    //Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    tmp = (epoch % 86400L) / 3600;
    //Serial.print(tmp); // print the hour (86400 equals secs per day)
    itoa(tmp, &buf[pos], 10);
    pos += 2;
    buf[2] = ' ';
    pos++;

    //Serial.print(':');
    if (((epoch % 3600) / 60) < 10)
    {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      //Serial.print('0');
      buf[pos] = '0';
      pos++;
    }
    tmp = (epoch % 3600) / 60;
    //Serial.print(tmp); // print the minute (3600 equals secs per minute)
    itoa(tmp, &buf[pos], 10);
    pos += 2;

    //Serial.print(':');
    if ((epoch % 60) < 10)
    {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      //Serial.print('0');
    }
    tmp = epoch % 60;
    //Serial.println(tmp); // print the second

    displayString(buf);
    //lc.setRow(2, 7, tmp);
    bool even = (tmp % 2) == 0;
    lc.setLed(1, 3, 0, even);
    lc.setLed(1, 4, 0, even);
    lc.setLed(1, 3, 7, alarmOn);

    epoch += 1;
  }

  delay(10);
  if (globalCounter == AUTONOMOUS_WORK)
  {
    globalCounter = 0;
  }
  else
  {
    globalCounter += 1;
  }

  if (ntpCounter == AUTONOMOUS_WORK)
  {
    ntpCounter = 0;
  }
  else
  {
    ntpCounter += 1;
  }
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;          // Stratum, or type of clock
  packetBuffer[2] = 6;          // Polling Interval
  packetBuffer[3] = 0xEC;       // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char font5x7[] = { //Numeric Font Matrix (Arranged as 7x font data + 1x kerning data)
    B00000000,     //Space (Char 0x20)
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    1,

    B10000000, //!
    B10000000,
    B10000000,
    B10000000,
    B00000000,
    B00000000,
    B10000000,
    2,

    B10100000, //"
    B10100000,
    B10100000,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    4,

    B01010000, //#
    B01010000,
    B11111000,
    B01010000,
    B11111000,
    B01010000,
    B01010000,
    6,

    B00100000, //$
    B01111000,
    B10100000,
    B01110000,
    B00101000,
    B11110000,
    B00100000,
    6,

    B11000000, //%
    B11001000,
    B00010000,
    B00100000,
    B01000000,
    B10011000,
    B00011000,
    6,

    B01100000, //&
    B10010000,
    B10100000,
    B01000000,
    B10101000,
    B10010000,
    B01101000,
    6,

    B11000000, //'
    B01000000,
    B10000000,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    3,

    B00100000, //(
    B01000000,
    B10000000,
    B10000000,
    B10000000,
    B01000000,
    B00100000,
    4,

    B10000000, //)
    B01000000,
    B00100000,
    B00100000,
    B00100000,
    B01000000,
    B10000000,
    4,

    B00000000, //*
    B00100000,
    B10101000,
    B01110000,
    B10101000,
    B00100000,
    B00000000,
    6,

    B00000000, //+
    B00100000,
    B00100000,
    B11111000,
    B00100000,
    B00100000,
    B00000000,
    6,

    B00000000, //,
    B00000000,
    B00000000,
    B00000000,
    B11000000,
    B01000000,
    B10000000,
    3,

    B00000000, //-
    B00000000,
    B11111000,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    6,

    B00000000, //.
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    B11000000,
    B11000000,
    3,

    B00000000, ///
    B00001000,
    B00010000,
    B00100000,
    B01000000,
    B10000000,
    B00000000,
    6,

    //B01110000,  //0
    //B10001000,
    //B10011000,
    //B10101000,
    //B11001000,
    //B10001000,
    //B01110000,
    //6,

    B11111000, //0
    B10001000,
    B10001000,
    B10001000,
    B10001000,
    B10001000,
    B11111000,
    6,

    //B01000000,  //1
    //B11000000,
    //B01000000,
    //B01000000,
    //B01000000,
    //B01000000,
    //B11100000,
    //4,

    //B00010000,  //1
    //B00110000,
    //B00010000,
    //B00010000,
    //B00010000,
    //B00010000,
    //B00111000,
    //6,

    B00010000, //1
    B00110000,
    B01010000,
    B00010000,
    B00010000,
    B00010000,
    B00010000,
    6,

    //B01110000,  //2
    //B10001000,
    //B00001000,
    //B00010000,
    //B00100000,
    //B01000000,
    //B11111000,
    //6,

    B11111000, //2
    B00001000,
    B00001000,
    B00010000,
    B00100000,
    B01000000,
    B11111000,
    6,

    //B11111000,  //3
    //B00010000,
    //B00100000,
    //B00010000,
    //B00001000,
    //B10001000,
    //B01110000,
    //6,

    B11111000, //3
    B00001000,
    B00001000,
    B11111000,
    B00001000,
    B00001000,
    B11111000,
    6,

    //B00010000,  //4
    //B00110000,
    //B01010000,
    //B10010000,
    //B11111000,
    //B00010000,
    //B00010000,
    //6,

    B10001000, //4
    B10001000,
    B10001000,
    B11111000,
    B00001000,
    B00001000,
    B00001000,
    6,

    //B11111000,  //5
    //B10000000,
    //B11110000,
    //B00001000,
    //B00001000,
    //B10001000,
    //B01110000,
    //6,

    B11111000, //5
    B10000000,
    B10000000,
    B11111000,
    B00001000,
    B00001000,
    B11111000,
    6,

    //B00110000,  //6
    //B01000000,
    //B10000000,
    //B11110000,
    //B10001000,
    //B10001000,
    //B01110000,
    //6,

    B11111000, //6
    B10000000,
    B10000000,
    B11111000,
    B10001000,
    B10001000,
    B11111000,
    6,

    //B11111000,  //7
    //B10001000,
    //B00001000,
    //B00010000,
    //B00100000,
    //B00100000,
    //B00100000,
    //6,

    B11111000, //7
    B00001000,
    B00010000,
    B00010000,
    B00100000,
    B00100000,
    B01000000,
    6,

    //B01110000,  //8
    //B10001000,
    //B10001000,
    //B01110000,
    //B10001000,
    //B10001000,
    //B01110000,
    //6,

    B11111000, //8
    B10001000,
    B10001000,
    B11111000,
    B10001000,
    B10001000,
    B11111000,
    6,

    //B01110000,  //9
    //B10001000,
    //B10001000,
    //B01111000,
    //B00001000,
    //B00010000,
    //B01100000,
    //6,

    B11111000, //9
    B10001000,
    B10001000,
    B11111000,
    B00001000,
    B00001000,
    B11111000,
    6,

    B00000000, //:
    B11000000,
    B11000000,
    B00000000,
    B11000000,
    B11000000,
    B00000000,
    3,

    B00000000, //;
    B11000000,
    B11000000,
    B00000000,
    B11000000,
    B01000000,
    B10000000,
    3,

    B00010000, //<
    B00100000,
    B01000000,
    B10000000,
    B01000000,
    B00100000,
    B00010000,
    5,

    B00000000, //=
    B00000000,
    B11111000,
    B00000000,
    B11111000,
    B00000000,
    B00000000,
    6,

    B10000000, //>
    B01000000,
    B00100000,
    B00010000,
    B00100000,
    B01000000,
    B10000000,
    5,

    B01110000, //?
    B10001000,
    B00001000,
    B00010000,
    B00100000,
    B00000000,
    B00100000,
    6,

    B01110000, //@
    B10001000,
    B00001000,
    B01101000,
    B10101000,
    B10101000,
    B01110000,
    6,

    B01110000, //A
    B10001000,
    B10001000,
    B10001000,
    B11111000,
    B10001000,
    B10001000,
    6,

    B11110000, //B
    B10001000,
    B10001000,
    B11110000,
    B10001000,
    B10001000,
    B11110000,
    6,

    B01110000, //C
    B10001000,
    B10000000,
    B10000000,
    B10000000,
    B10001000,
    B01110000,
    6,

    B11100000, //D
    B10010000,
    B10001000,
    B10001000,
    B10001000,
    B10010000,
    B11100000,
    6,

    B11111000, //E
    B10000000,
    B10000000,
    B11110000,
    B10000000,
    B10000000,
    B11111000,
    6,

    B11111000, //F
    B10000000,
    B10000000,
    B11110000,
    B10000000,
    B10000000,
    B10000000,
    6,

    B01110000, //G
    B10001000,
    B10000000,
    B10111000,
    B10001000,
    B10001000,
    B01111000,
    6,

    B10001000, //H
    B10001000,
    B10001000,
    B11111000,
    B10001000,
    B10001000,
    B10001000,
    6,

    B11100000, //I
    B01000000,
    B01000000,
    B01000000,
    B01000000,
    B01000000,
    B11100000,
    4,

    B00111000, //J
    B00010000,
    B00010000,
    B00010000,
    B00010000,
    B10010000,
    B01100000,
    6,

    B10001000, //K
    B10010000,
    B10100000,
    B11000000,
    B10100000,
    B10010000,
    B10001000,
    6,

    B10000000, //L
    B10000000,
    B10000000,
    B10000000,
    B10000000,
    B10000000,
    B11111000,
    6,

    B10001000, //M
    B11011000,
    B10101000,
    B10101000,
    B10001000,
    B10001000,
    B10001000,
    6,

    B10001000, //N
    B10001000,
    B11001000,
    B10101000,
    B10011000,
    B10001000,
    B10001000,
    6,

    B01110000, //O
    B10001000,
    B10001000,
    B10001000,
    B10001000,
    B10001000,
    B01110000,
    6,

    B11110000, //P
    B10001000,
    B10001000,
    B11110000,
    B10000000,
    B10000000,
    B10000000,
    6,

    B01110000, //Q
    B10001000,
    B10001000,
    B10001000,
    B10101000,
    B10010000,
    B01101000,
    6,

    B11110000, //R
    B10001000,
    B10001000,
    B11110000,
    B10100000,
    B10010000,
    B10001000,
    6,

    B01111000, //S
    B10000000,
    B10000000,
    B01110000,
    B00001000,
    B00001000,
    B11110000,
    6,

    B11111000, //T
    B00100000,
    B00100000,
    B00100000,
    B00100000,
    B00100000,
    B00100000,
    6,

    B10001000, //U
    B10001000,
    B10001000,
    B10001000,
    B10001000,
    B10001000,
    B01110000,
    6,

    B10001000, //V
    B10001000,
    B10001000,
    B10001000,
    B10001000,
    B01010000,
    B00100000,
    6,

    B10001000, //W
    B10001000,
    B10001000,
    B10101000,
    B10101000,
    B10101000,
    B01010000,
    6,

    B10001000, //X
    B10001000,
    B01010000,
    B00100000,
    B01010000,
    B10001000,
    B10001000,
    6,

    B10001000, //Y
    B10001000,
    B10001000,
    B01010000,
    B00100000,
    B00100000,
    B00100000,
    6,

    B11111000, //Z
    B00001000,
    B00010000,
    B00100000,
    B01000000,
    B10000000,
    B11111000,
    6,

    B11100000, //[
    B10000000,
    B10000000,
    B10000000,
    B10000000,
    B10000000,
    B11100000,
    4,

    B00000000, //(Backward Slash)
    B10000000,
    B01000000,
    B00100000,
    B00010000,
    B00001000,
    B00000000,
    6,

    B11100000, //]
    B00100000,
    B00100000,
    B00100000,
    B00100000,
    B00100000,
    B11100000,
    4,

    B00100000, //^
    B01010000,
    B10001000,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    6,

    B00000000, //_
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    B11111000,
    6,

    B10000000, //`
    B01000000,
    B00100000,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    4,

    B00000000, //a
    B00000000,
    B01110000,
    B00001000,
    B01111000,
    B10001000,
    B01111000,
    6,

    B10000000, //b
    B10000000,
    B10110000,
    B11001000,
    B10001000,
    B10001000,
    B11110000,
    6,

    B00000000, //c
    B00000000,
    B01110000,
    B10001000,
    B10000000,
    B10001000,
    B01110000,
    6,

    B00001000, //d
    B00001000,
    B01101000,
    B10011000,
    B10001000,
    B10001000,
    B01111000,
    6,

    B00000000, //e
    B00000000,
    B01110000,
    B10001000,
    B11111000,
    B10000000,
    B01110000,
    6,

    B00110000, //f
    B01001000,
    B01000000,
    B11100000,
    B01000000,
    B01000000,
    B01000000,
    6,

    B00000000, //g
    B01111000,
    B10001000,
    B10001000,
    B01111000,
    B00001000,
    B01110000,
    6,

    B10000000, //h
    B10000000,
    B10110000,
    B11001000,
    B10001000,
    B10001000,
    B10001000,
    6,

    B01000000, //i
    B00000000,
    B11000000,
    B01000000,
    B01000000,
    B01000000,
    B11100000,
    4,

    B00010000, //j
    B00000000,
    B00110000,
    B00010000,
    B00010000,
    B10010000,
    B01100000,
    5,

    B10000000, //k
    B10000000,
    B10010000,
    B10100000,
    B11000000,
    B10100000,
    B10010000,
    5,

    B11000000, //l
    B01000000,
    B01000000,
    B01000000,
    B01000000,
    B01000000,
    B11100000,
    4,

    B00000000, //m
    B00000000,
    B11010000,
    B10101000,
    B10101000,
    B10001000,
    B10001000,
    6,

    B00000000, //n
    B00000000,
    B10110000,
    B11001000,
    B10001000,
    B10001000,
    B10001000,
    6,

    B00000000, //o
    B00000000,
    B01110000,
    B10001000,
    B10001000,
    B10001000,
    B01110000,
    6,

    B00000000, //p
    B00000000,
    B11110000,
    B10001000,
    B11110000,
    B10000000,
    B10000000,
    6,

    B00000000, //q
    B00000000,
    B01101000,
    B10011000,
    B01111000,
    B00001000,
    B00001000,
    6,

    B00000000, //r
    B00000000,
    B10110000,
    B11001000,
    B10000000,
    B10000000,
    B10000000,
    6,

    B00000000, //s
    B00000000,
    B01110000,
    B10000000,
    B01110000,
    B00001000,
    B11110000,
    6,

    B01000000, //t
    B01000000,
    B11100000,
    B01000000,
    B01000000,
    B01001000,
    B00110000,
    6,

    B00000000, //u
    B00000000,
    B10001000,
    B10001000,
    B10001000,
    B10011000,
    B01101000,
    6,

    B00000000, //v
    B00000000,
    B10001000,
    B10001000,
    B10001000,
    B01010000,
    B00100000,
    6,

    B00000000, //w
    B00000000,
    B10001000,
    B10101000,
    B10101000,
    B10101000,
    B01010000,
    6,

    B00000000, //x
    B00000000,
    B10001000,
    B01010000,
    B00100000,
    B01010000,
    B10001000,
    6,

    B00000000, //y
    B00000000,
    B10001000,
    B10001000,
    B01111000,
    B00001000,
    B01110000,
    6,

    B00000000, //z
    B00000000,
    B11111000,
    B00010000,
    B00100000,
    B01000000,
    B11111000,
    6,

    B00100000, //{
    B01000000,
    B01000000,
    B10000000,
    B01000000,
    B01000000,
    B00100000,
    4,

    B10000000, //|
    B10000000,
    B10000000,
    B10000000,
    B10000000,
    B10000000,
    B10000000,
    2,

    B10000000, //}
    B01000000,
    B01000000,
    B00100000,
    B01000000,
    B01000000,
    B10000000,
    4,

    B00000000, //~
    B00000000,
    B00000000,
    B01101000,
    B10010000,
    B00000000,
    B00000000,
    6,

    B01100000, // (Char 0x7F)
    B10010000,
    B10010000,
    B01100000,
    B00000000,
    B00000000,
    B00000000,
    5};

void scrollFont()
{
  for (int counter = 0x20; counter < 0x80; counter++)
  {
    loadBufferLong(counter);
    delay(500);
  }
}

// Scroll Message
void scrollMessage(char *messageString)
{
  int counter = 0;
  int myChar = 0;
  do
  {
    // read back a char
    myChar = *(messageString + counter);
    if (myChar != 0)
    {
      loadBufferLong(myChar);
    }
    counter++;
  } while (myChar != 0);
}
// Load character into scroll buffer
void loadBufferLong(int ascii)
{
  if (ascii >= 0x20 && ascii <= 0x7f)
  {
    for (int a = 0; a < 7; a++)
    {                                                          // Loop 7 times for a 5x7 font
      unsigned long c = *(font5x7 + ((ascii - 0x20) * 8) + a); // Index into character table to get row data
      unsigned long x = bufferLong[a * 2];                     // Load current scroll buffer
      x = x | c;                                               // OR the new character onto end of current
      bufferLong[a * 2] = x;                                   // Store in buffer
    }
    byte count = *(font5x7 + ((ascii - 0x20) * 8) + 7); // Index into character table for kerning data
    for (byte x = 0; x < count; x++)
    {
      rotateBufferLong();
      printBufferLong();
      delay(scrollDelay);
    }
  }
}
// Rotate the buffer
void rotateBufferLong()
{
  for (int a = 0; a < 7; a++)
  {                                      // Loop 7 times for a 5x7 font
    unsigned long x = bufferLong[a * 2]; // Get low buffer entry
    byte b = bitRead(x, 31);             // Copy high order bit that gets lost in rotation
    x = x << 1;                          // Rotate left one bit
    bufferLong[a * 2] = x;               // Store new low buffer
    x = bufferLong[a * 2 + 1];           // Get high buffer entry
    x = x << 1;                          // Rotate left one bit
    bitWrite(x, 0, b);                   // Store saved bit
    bufferLong[a * 2 + 1] = x;           // Store new high buffer
  }
}
// Display Buffer on LED matrix
void printBufferLong()
{
  int a = 0;
  for (int b = 0; b < 7; b++)
  { // Loop 7 times for a 5x7 font
    a = 7 - b;
    unsigned long x = bufferLong[b * 2 + 1]; // Get high buffer entry
    byte y = x;                              // Mask off first character
    lc.setColumn(0, a, y);                   // Send row to relevent MAX7219 chip
    x = bufferLong[b * 2];                   // Get low buffer entry
    y = (x >> 24);                           // Mask off second character
    lc.setColumn(1, a, y);                   // Send row to relevent MAX7219 chip
    y = (x >> 16);                           // Mask off third character
    lc.setColumn(2, a, y);                   // Send row to relevent MAX7219 chip
    y = (x >> 8);                            // Mask off forth character
    lc.setColumn(3, a, y);                   // Send row to relevent MAX7219 chip
  }
}

void displayString(char str[])
{
  memset(bufferLong, 0, sizeof(bufferLong));
  for (int i = 0; i < 5; i++)
  {
    int ascii = str[i];
    if (ascii >= 0x20 && ascii <= 0x7f)
    {
      for (int a = 0; a < 7; a++)
      {                                                          // Loop 7 times for a 5x7 font
        unsigned long c = *(font5x7 + ((ascii - 0x20) * 8) + a); // Index into character table to get row data
        unsigned long x = bufferLong[a * 2];                     // Load current scroll buffer
        x = x | c;                                               // OR the new character onto end of current
        bufferLong[a * 2] = x;                                   // Store in buffer
      }
      if (i < 4)
      {
        byte count = *(font5x7 + ((ascii - 0x20) * 8) + 7); // Index into character table for kerning data
        for (byte x = 0; x < count; x++)
        {
          rotateBufferLong();
        }
      }
    }
  }
  for (int a = 0; a < 13; a++)
  {
    rotateBufferLong();
  }
  printBufferLong();
  //delay(scrollDelay);
}
