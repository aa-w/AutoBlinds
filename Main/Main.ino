/*
     _   _    _____  __ __      _____   ___  ___  _    _____   __
    /_\ | |  | __\ \/ / \ \    / / _ \ / _ \|   \| |  | __\ \ / /
   / _ \| |__| _| >  <   \ \/\/ / (_) | (_) | |) | |__| _| \ V /
  /_/ \_\____|___/_/\_\   \_/\_/ \___/ \___/|___/|____|___| |_|

*/

//This project is designed to run on an esp
//Libraries
#include <Arduino.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <AccelStepper.h>
#include "EEPROM.h"
#include "time.h"
#include "sup.h"

//links
//https://github.com/olikraus/u8g2/wiki/fntlistall
//https://github.com/olikraus/u8g2/wiki/fntgrpiconic
//https://github.com/olikraus/u8g2/wiki/fntgrpiconic
//https://github.com/olikraus/u8g2/wiki/u8g2reference

//IO
//On Board Button
#define PRGBUTTONPIN 0

//Motor Pins
#define DirPin 36
#define StepPin 37
#define MotorInterfaceType 1
#define MotorEnable 39

//Select Pin
#define SELECTPIN 26

//** SCREEN VALUES ** //

//Display Set Up
#define SCL 15
#define SDA 4
#define RESET 16

//Progamming Values
#define SCREENWIDTH 128
#define SCREENHEIGHT 64

//Timeout for setup in ms
#define CONNECTIONTIMEOUT 20000
#define INNERCIRCLE 17
#define CIRCLEMIN 18
#define CIRCLEMAX 60

#define MOVESCREENCIRCLE 40

//#define CONFIGCURSOROPTIONS 8
#define SETTINGSCURSOROPTIONS 8

#define WIFICONNECTIONREFRESH 500
#define MQTTCONNECTIONREFRESH 5000

//#define ESP32UNIQUEID "" // << This is unqiue MQTT ID FOR this topic

//SUB Values
#define MQTTSUB_BLIND "blind/alexbedroom"
#define MQTTSUB_TIME "spaff/time"
#define MQTTSUB_SUN "spaff/sun"

//PUB Values
#define MQTTPUBERRORVAL "ERROR-AlexBedroom"
#define MQTTPUB_ERROR "ERROR"
#define MQTTPUB_TIME "get/time"
#define MQTTPUB_SUN "get/sun"
#define MQTTPUB_BLIND "blind/alexbedroom/status"

//#define MQTT_TOPIC "timeSpaff"

#define SCREENSAVERTIMEOUT 30000 //30s
#define SETTINGSSAVERAMOUNT 300000
#define BUTTONDEBOUNCETIME 300
#define TICKINTERVAL 60000 //60s
#define TIMEREFRESHTIME 10

#define ANIMATIONTIMERVALUE 500 //time between animations

//Task Multicore running
//TaskHandle_t Task2; //Task to be run on Core 0

//EPROM Setup
#define EEPROM_SIZE 128

//Pref Library Setup
Preferences preferences;

//Display Setup
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, SCL, SDA, RESET);

//Motor Setup
AccelStepper Motor(MotorInterfaceType, StepPin, DirPin);

//Wifi Settings
//PRIVATE INFORMATION
///const char* ssid     = "";     // your network SSID (name of wifi network)
//const char* password = ""; // your network password
WiFiClient espClient;

//MQTT Client
#define WIFITRYLIMIT 30
#define MQTTTRYLIMIT 10

IPAddress server(192, 168, 16, 102);
PubSubClient client(espClient);

bool DaylightSavings = true;

//Blind Control Global Values
bool BlindOpen = false;
bool OnStartup = true;
//Display Global Values
unsigned long ScreenOffTimer;
unsigned long ClockTimer = TICKINTERVAL;
unsigned long EnterButtonDebounceTimer = 0;
unsigned long PRGButtonDebounceTimer = 0;
unsigned long MQTTReconnectTimer = 0;
unsigned long AnimationTimer = 0;

byte SelectedDay = 0;

bool BlankFrame = false;
bool UpdateFrame = true;

//Menu Control Values
byte DisplayIndex = 0;
byte CursorIndex = 0; //Position of the cursor

bool MQTTServer = true; //Config if to connect to an MQTT Server for remote control / weather
bool TimeServer = true;

int CurrentTime = 0; //starts at 0000 or 12 for clarity
byte CurrentDay = 0;
byte CurrentSunrise = 0;
byte CurrentSunset = 2400;
double SetCurrentTime = 0.00; //Only used for setting local time

const double MenuPositions[3] = {(SCREENHEIGHT * 0.33), (SCREENHEIGHT * 0.65), (SCREENHEIGHT * 0.65)};
const double ConfigPositions[4] = {(SCREENHEIGHT * 0.40), (SCREENHEIGHT * 0.60), (SCREENHEIGHT * 0.80), (SCREENHEIGHT * 1)};
const double ConfigSettingsPositions[4] = {(SCREENHEIGHT * 0.40), (SCREENHEIGHT * 0.60), (SCREENHEIGHT * 0.80), (SCREENHEIGHT * 1.0)};
const double MoveSettingsPositions[4] = {(SCREENHEIGHT * 0.40), (SCREENHEIGHT * 0.60), (SCREENHEIGHT * 0.80), (SCREENHEIGHT * 1.0)};
const double SchedulePositions[4] = {(SCREENHEIGHT * 0.58), (SCREENHEIGHT * 0.95), (SCREENHEIGHT * 1.2), (SCREENHEIGHT * 1.0)};

//SCHEDULE STORAGE INFORMATION

struct Days
{
  char DayName[9];
  int OpenTime;
  double ViewOpenTime;
  int CloseTime;
  double ViewCloseTime;
  bool DayIsEnabled = false;
  bool Sunrise = false;
  bool Sunset = false;
};

struct Days Day[7];

void setup()
{
  //display setup
  u8g2.begin();
  u8g2.setFontPosCenter();

  //Button / IO Setup
  pinMode(PRGBUTTONPIN, INPUT);
  pinMode(SELECTPIN, INPUT);

  //Serial Setup
  Serial.begin(115200);

  //Call Wifi Connect
  if (WifiConnection(OnStartup) == true)
  {
    //Call MQTT Connect //Only try if wifi connects ok else skip and keep local?
    //Set values for MQTT
    client.setServer(server, 1883);
    client.setCallback(callback);
    MQTTConnection(OnStartup); //onsart up take over display
  }
  //ScreenSaver
  ScreenOffTimer = millis() + SCREENSAVERTIMEOUT;

  //Motor Setup
  Motor.setMaxSpeed(20);
  Motor.setAcceleration(20);
  Motor.setSpeed(20);
  pinMode(MotorEnable, OUTPUT);
  digitalWrite(MotorEnable, HIGH);

  //////////////// EEPROM /////////////////////
  /*
    EEPROM.begin(512);
    if (!EEPROM.begin(EEPROM_SIZE))
    {
    Serial.println("PREFS - failed to initialise EEPROM");
    }
  */
  strncpy(Day[0].DayName, "Monday", strlen((char*)"Monday"));
  strncpy(Day[1].DayName, "Tuesday", strlen((char*)"Tuesday"));
  strncpy(Day[2].DayName, "Wednesday", strlen((char*)"Wednesday"));
  strncpy(Day[3].DayName, "Thursday", strlen((char*)"Thursday"));
  strncpy(Day[4].DayName, "Friday", strlen((char*)"Friday"));
  strncpy(Day[5].DayName, "Saturday", strlen((char*)"Saturday"));
  strncpy(Day[6].DayName, "Sunday", strlen((char*)"Sunday"));

  //Get values from memory otherwise set defaults
  //if (EEWriteRead(false) == false) PrefsWriteRead
  if (PrefsWriteRead(false) == false)
  {

    Day[0].OpenTime = 1208;
    Day[0].ViewOpenTime = 12.08;
    Day[0].CloseTime = 2005;
    Day[0].ViewCloseTime = 20.05;
    Day[0].DayIsEnabled = true;
    Day[0].Sunrise = false;
    Day[0].Sunset = false;


    Day[1].OpenTime = 1208;
    Day[1].ViewOpenTime = 12.08;
    Day[1].CloseTime = 2005;
    Day[1].ViewCloseTime = 20.05;
    Day[1].DayIsEnabled = true;
    Day[1].Sunrise = false;
    Day[1].Sunset = false;


    Day[2].OpenTime = 1208;
    Day[2].ViewOpenTime = 12.08;
    Day[2].CloseTime = 2005;
    Day[2].ViewCloseTime = 20.05;
    Day[2].DayIsEnabled = true;
    Day[2].Sunrise = false;
    Day[2].Sunset = false;


    Day[3].OpenTime = 1208;
    Day[3].ViewOpenTime = 12.08;
    Day[3].CloseTime = 2005;
    Day[3].ViewCloseTime = 20.05;
    Day[3].DayIsEnabled = true;
    Day[3].Sunrise = false;
    Day[3].Sunset = false;


    Day[4].OpenTime = 1208;
    Day[4].ViewOpenTime = 12.08;
    Day[4].CloseTime = 2005;
    Day[4].ViewCloseTime = 20.05;
    Day[4].DayIsEnabled = true;
    Day[4].Sunrise = false;
    Day[4].Sunset = false;


    Day[5].OpenTime = 1208;
    Day[5].ViewOpenTime = 12.08;
    Day[5].CloseTime = 2005;
    Day[5].ViewCloseTime = 20.05;
    Day[5].DayIsEnabled = true;
    Day[5].Sunrise = false;
    Day[5].Sunset = false;


    //Day[6].DayName[0] = "Sunday";
    Day[6].OpenTime = 1208;
    Day[6].ViewOpenTime = 12.08;
    Day[6].CloseTime = 2005;
    Day[6].ViewCloseTime = 20.05;
    Day[6].DayIsEnabled = true;
    Day[6].Sunrise = false;
    Day[6].Sunset = false;

    if (PrefsWriteRead(true) == true) //Now right the constant values to memory
    {
      Serial.println("PREFS - Values written to memory");
    }
  }


  TimerTick(); //Gets fresh time on startup

  //SPIFFs


  OnStartup = false;
}

bool MQTTDead = false; //debug to stop spamming if it fails to connnect

void loop()
{
  //const CurCusVal = 5;

  TouchHandler(); //Checks for input to wake//Checks Timer for increment

  TimerTick(); //Updates Clock Timer

  if (MQTTServer == true)
  {
    if (client.loop() == false)
    {
      if (MQTTDead == false)
      {
        Serial.println("MQTT - Client Disconnected");
        Serial.print("MQTT -                              Time Of Death - ");
        Serial.println(((millis() / 1000) / 60));

        MQTTDead = true; //debug to stop it spamming
      }
      else
      {
        if (millis() > MQTTReconnectTimer)
        {
          MQTTConnection(false);
          MQTTReconnectTimer = millis() + 10000; //trys again in 10 minutes if it didnt work
        }
      }
    }
  }


  //Check if timedout first
  //We only need to send Display updates upon user interaction or when the animation timer is triggered
  if (millis() < ScreenOffTimer && (UpdateFrame == true || millis() > AnimationTimer))
  {
    u8g2.clearBuffer();
    ScreenHandler();
    DrawCursor();
    u8g2.sendBuffer();
    UpdateFrame = false;

    if (millis() > AnimationTimer) //Resets the animation timer if it triggered an update
    {
      AnimationTimer = millis() + AnimationTimer;
    }
  }

  if (millis() > ScreenOffTimer && BlankFrame == false) //Sends blank frame as the display has timed out
  {
    //Send empty frame to clear the display
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    Serial.println("UI - Empty Frame sent");
    BlankFrame = true; //Just so it doesn't spam empty frames all the time
  }

}

void TimerTick()
{
  if (ClockTimer <= millis())
  {
    ClockTimer = millis() + TICKINTERVAL;
    CurrentTime = TimeIncDec(CurrentTime, true, 2);
    Serial.print("Day - ");
    Serial.print(CurrentDay);
    Serial.print(" ");
    Serial.print("Time - ");
    Serial.println(CurrentTime);
    if (CurrentTime == TIMEREFRESHTIME || OnStartup == true)
    {
      DayTick();
      if (TimeServer == true) // Only get fresh network time if enabled
      {
        if (GetMQTT(0) != true) //Updates Current Time with fresh time from source
        {
          Serial.println("TimerTick - Time Update - Failed to update time from source");
        }
        if (GetMQTT(1) != true) //Updates Current Time with fresh time from source
        {
          Serial.println("TimerTick - Sun Update - Failed to update time from source");
        }
      }
    }
    CheckSchedule();
  }
}

void CheckSchedule() //these may need slow open logic?
{
  //Checks the days schedule for triggers
  if (Day[CurrentDay].DayIsEnabled == true)
  {
    if ((CurrentTime == Day[CurrentDay].OpenTime) && (BlindOpen == false))//Open Time
    {
      MoveBlinds(false);
    }
    else if ((CurrentTime == Day[CurrentDay].CloseTime) && (BlindOpen == true)) //Close Time
    {
      MoveBlinds(false);
    }


    //If enabled - Sunset Time
    if ((Day[CurrentDay].Sunrise == true) && (CurrentTime == CurrentSunrise) && (BlindOpen == false))
    {
      MoveBlinds(false);
    }

    //If enabled - Sunset Time
    if ((Day[CurrentDay].Sunset == true) && (CurrentTime == CurrentSunset) && (BlindOpen == true))
    {
      MoveBlinds(false);
    }
  }
}

void DayTick()
{
  //will tick over the day when triggered
  if (CurrentDay < 6)
  {
    CurrentDay++;
  }
  else
  {
    CurrentDay = 0;
  }
  Serial.print("DayTick - Day is now: ");
  Serial.println(Day[CurrentDay].DayName);
}

void DrawCursor()
{
  u8g2.setFont(u8g2_font_helvB10_tf);

  switch (DisplayIndex)
  {
    /*

    */
    case 0:
      {
        //Menu Screen
        if (CursorIndex == 2)
        {
          u8g2.drawStr((SCREENWIDTH * 0.46), MenuPositions[CursorIndex], ">");
        }
        else if (CursorIndex == 1)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), MenuPositions[CursorIndex], ">");
        }
        else
        {
          //Do nothing for this case because its highlighted
        }
      }
      break;
    case 1:
      {
        //Config Screen
        if (CursorIndex < 3)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex], ">");
        }
        else if (CursorIndex < 6)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex - 3], ">");
        }
        else
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex - 6], ">");
        }
      }
      break;
    case 2:
      {
        //Settings Screen
        if (CursorIndex < 3)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex], ">");
        }
        else if (CursorIndex < 5)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex - 3], ">");
        }
        else if (CursorIndex > 5)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigSettingsPositions[3], ">");
        }
        else
        {

        }
      }
      break;
    case 3:
      {
        if (CursorIndex < 3)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigSettingsPositions[CursorIndex], ">");
        }
        else
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigSettingsPositions[0], ">");
        }
      }
      break;
    case 4:
      {
        //Case 5 Handles both cases
        ///// V V V V V V V
      }
    case 5:
      {
        //down arror - m // up arror - p
        u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
        switch (CursorIndex)
        {
          case 0: //increment hour
            u8g2.drawStr((SCREENWIDTH * 0.22), (SCREENHEIGHT * 0.32), "m");
            break;
          case 2: //increment ten minutes
            u8g2.drawStr((SCREENWIDTH * 0.58), (SCREENHEIGHT * 0.32), "m");
            break;
          case 4: //increment one minute
            u8g2.drawStr((SCREENWIDTH * 0.72), (SCREENHEIGHT * 0.32), "m");
            break;
          case 1: //decrement hour
            u8g2.drawStr((SCREENWIDTH * 0.22), (SCREENHEIGHT * 0.75), "p");
            break;
          case 3: //decrement ten minutes
            u8g2.drawStr((SCREENWIDTH * 0.58), (SCREENHEIGHT * 0.75), "p");
            break;
          case 5: //decrement one minute
            u8g2.drawStr((SCREENWIDTH * 0.72), (SCREENHEIGHT * 0.75), "p");
            break;
          case 6: //rise / set bool
            u8g2.setFont(u8g2_font_helvB10_tf);
            u8g2.drawStr((SCREENWIDTH * 0.00), ConfigSettingsPositions[0], ">");
            break;
          case 7: //rise / set bool
            u8g2.setFont(u8g2_font_helvB10_tf);
            u8g2.drawStr((SCREENWIDTH * 0.00), ConfigSettingsPositions[1], ">");
            break;
          default:
            break;
        }
      }
      break;
    case 6:
      {
        u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
        switch (CursorIndex)
        {
          case 0: //increment hour
            u8g2.drawStr((SCREENWIDTH * 0.22), (SCREENHEIGHT * 0.32), "m");
            break;
          case 2: //increment ten minutes
            u8g2.drawStr((SCREENWIDTH * 0.58), (SCREENHEIGHT * 0.32), "m");
            break;
          case 4: //increment one minute
            u8g2.drawStr((SCREENWIDTH * 0.72), (SCREENHEIGHT * 0.32), "m");
            break;
          case 1: //decrement hour
            u8g2.drawStr((SCREENWIDTH * 0.22), (SCREENHEIGHT * 0.75), "p");
            break;
          case 3: //decrement ten minutes
            u8g2.drawStr((SCREENWIDTH * 0.58), (SCREENHEIGHT * 0.75), "p");
            break;
          case 5: //decrement one minute
            u8g2.drawStr((SCREENWIDTH * 0.72), (SCREENHEIGHT * 0.75), "p");
            break;
          case 6: //Back option
            u8g2.setFont(u8g2_font_helvB10_tf);
            u8g2.drawStr((SCREENWIDTH * 0.00), SchedulePositions[1], ">");
            break;
          default:
            break;
        }
      }
      break;
    case 7:
      {
        //Set Day Screen
        if (CursorIndex < 3)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex], ">");
        }
        else if (CursorIndex < 6)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex - 3], ">");
        }
        else
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex - 6], ">");
        }
      }
      break;
    case 8:
      {
        // VVVVV The Case below handles both of these cases
      }
    case 9:
      {
        //Motor Pos Setup
        if (CursorIndex < 3)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex], ">");
        }
        else if (CursorIndex < 6)
        {
          u8g2.drawStr((SCREENWIDTH * 0.00), ConfigPositions[CursorIndex - 3], ">");
        }
        else
        {

        }
      }
      break;
    default:
      break;
  }
}

void TouchHandler()
{
  if (EnterButton(true))
  {
    UpdateFrame = true;
    if (BlankFrame == true) //Resets the sleep functions
    {
      DisplayIndex = 0; //Display Position is reset upon wake
      CursorIndex = 0; //Cursor Position is reset upon wake
      BlankFrame = false;
    }
    else
    {
      switch (DisplayIndex)
      {
        case 0:
          //Menu / Main Screen Actions
          switch (CursorIndex)
          {
            case 0:
              //THIS WILL OPEN / CLOSE THE BLINDS
              MoveBlinds(true);
              break;
            case 1:
              //Move to config screen
              DisplayIndex = 1;
              CursorIndex = 0;
              break;
            case 2:
              //Move to settings screen
              DisplayIndex = 2;
              CursorIndex = 0;
              break;
            case 3:
              //Config day screen
              CursorIndex = 0;
              break;
            default:
              break;
          }
          break;
        case 1:
          {
            //Config Screen
            if (CursorIndex == 7) //Case to return to main
            {
              DisplayIndex = 0;
            }
            else
            {
              SelectedDay = CursorIndex;
              DisplayIndex = 3;
            }

            CursorIndex = 0; //set to cursor to the top of the page
          }
          break;
        case 2:
          //Settings Screen
          switch (CursorIndex)
          {
            case 0:
              {
                //Motor Position Setup
                CursorIndex = 0;
                DisplayIndex = 8;

              }
              break;
            case 1:
              {
                //Time Server Toggle On / Off
                if (TimeServer == true)
                {
                  TimeServer = false;
                }
                else
                {
                  TimeServer = true;
                }
              }
              break;
            case 2:
              {
                //Blind Move Speed Configure
                CursorIndex = 0;
                DisplayIndex = 9;
              }
              break;
            case 3:
              {
                //MQTTT Server Toggle On / Off
                if (MQTTServer == true)
                {
                  MQTTServer = false;
                }
                else
                {
                  MQTTServer = true;
                }
              }
              break;
            case 4:
              {
                //Time Set Dependant on bool option
                //Serial.println("Case 5 is being run");
                if (TimeServer == true)
                {
                  if (GetMQTT(0) == true)
                  {
                    Serial.println("Settings Screen - Get Time Ran");
                  }
                }
                else
                {
                  //Time Set up Page
                  DisplayIndex = 6;
                  CursorIndex = 0;
                }
              }
              break;
            case 5://Back
              {
                DisplayIndex = 0;
                CursorIndex = 0;
              }
              break;
            default:
              break;
          }
          break;
        case 3:
          //Config Options (Dependant on Day) Screen
          switch (CursorIndex)
          {
            case 0:
              //on / off
              if (Day[SelectedDay].DayIsEnabled == false)
              {
                Day[SelectedDay].DayIsEnabled = true;
              }
              else
              {
                Day[SelectedDay].DayIsEnabled = false;
              }
              break;
            case 1:
              //Open Time
              DisplayIndex = 4;
              CursorIndex = 0;
              Serial.println("Open Time");
              break;
            case 2:
              //Close Time
              DisplayIndex = 5;
              CursorIndex = 0;
              Serial.println("Close Time");
              break;
            case 3:
              //Back
              DisplayIndex = 1;
              CursorIndex = (SelectedDay + 1);
              PrefsWriteRead(true);
              //EEWriteRead(true);
              break;
            default:
              break;
          }
          break;
        case 4:
          //Open Schedule // Set trigger time (on time or off)
          switch (CursorIndex)
          {
            case 0: //increment hour
              {
                Day[SelectedDay].OpenTime = TimeIncDec(Day[SelectedDay].OpenTime, true, 0);
                double DoubleConvert = Day[SelectedDay].OpenTime;
                Day[SelectedDay].ViewOpenTime = (DoubleConvert / 100);
              }
              break;
            case 1: //decrement hour
              {
                Day[SelectedDay].OpenTime = TimeIncDec(Day[SelectedDay].OpenTime, false, 0);
                double DoubleConvert = Day[SelectedDay].OpenTime;
                Day[SelectedDay].ViewOpenTime = (DoubleConvert / 100);
              }
              break;
            case 2: //increment 10 minutes
              {
                Day[SelectedDay].OpenTime = TimeIncDec(Day[SelectedDay].OpenTime, true, 1);
                double DoubleConvert = Day[SelectedDay].OpenTime;
                Day[SelectedDay].ViewOpenTime = (DoubleConvert / 100);
              }
              break;
            case 3: //decrement 10 minutes
              {
                Day[SelectedDay].OpenTime = TimeIncDec(Day[SelectedDay].OpenTime, false, 1);
                double DoubleConvert = Day[SelectedDay].OpenTime;
                Day[SelectedDay].ViewOpenTime = (DoubleConvert / 100);
              }
              break;
            case 4: //increment 1 minute
              {
                Day[SelectedDay].OpenTime = TimeIncDec(Day[SelectedDay].OpenTime, true, 2);
                double DoubleConvert = Day[SelectedDay].OpenTime;
                Day[SelectedDay].ViewOpenTime = (DoubleConvert / 100);
              }
              break;
            case 5: //decrement 1 minute
              {
                Day[SelectedDay].OpenTime = TimeIncDec(Day[SelectedDay].OpenTime, false, 2);
                double DoubleConvert = Day[SelectedDay].OpenTime;
                Day[SelectedDay].ViewOpenTime = (DoubleConvert / 100);
              }
              break;
            case 6:
              //toggle on / off
              if (Day[SelectedDay].Sunrise == false)
              {
                Day[SelectedDay].Sunrise = true;
              }
              else
              {
                Day[SelectedDay].Sunrise = false;
              }
              break;
            case 7:
              //Back Button
              DisplayIndex = 3;
              CursorIndex = 0;
              break;
            default:
              break;
          }
          break;
        case 5:
          //Close Schedule // Set trigger time (on time or off)
          switch (CursorIndex)
          {
            case 0: //increment hour
              {
                Day[SelectedDay].CloseTime = TimeIncDec(Day[SelectedDay].CloseTime, true, 0);
                double DoubleConvert = Day[SelectedDay].CloseTime;
                Day[SelectedDay].ViewCloseTime = (DoubleConvert / 100);
              }
              break;
            case 1: //decrement hour
              {
                Day[SelectedDay].CloseTime = TimeIncDec(Day[SelectedDay].CloseTime, false, 0);
                double DoubleConvert = Day[SelectedDay].CloseTime;
                Day[SelectedDay].ViewCloseTime = (DoubleConvert / 100);
              }
              break;
            case 2: //increment 10 minutes
              {
                Day[SelectedDay].CloseTime = TimeIncDec(Day[SelectedDay].CloseTime, true, 1);
                double DoubleConvert = Day[SelectedDay].CloseTime;
                Day[SelectedDay].ViewCloseTime = (DoubleConvert / 100);
              }
              break;
            case 3: //decrement 10 minutes
              {
                Day[SelectedDay].CloseTime = TimeIncDec(Day[SelectedDay].CloseTime, false, 1);
                double DoubleConvert = Day[SelectedDay].CloseTime;
                Day[SelectedDay].ViewCloseTime = (DoubleConvert / 100);
              }
              break;
            case 4: //increment 1 minute
              {
                Day[SelectedDay].CloseTime = TimeIncDec(Day[SelectedDay].CloseTime, true, 2);
                double DoubleConvert = Day[SelectedDay].CloseTime;
                Day[SelectedDay].ViewCloseTime = (DoubleConvert / 100);
              }
              break;
            case 5: //decrement 1 minute
              {
                Day[SelectedDay].CloseTime = TimeIncDec(Day[SelectedDay].CloseTime, false, 2);
                double DoubleConvert = Day[SelectedDay].CloseTime;
                Day[SelectedDay].ViewCloseTime = (DoubleConvert / 100);
              }
              break;
            case 6:
              //toggle on / off
              if (Day[SelectedDay].Sunset == false)
              {
                Day[SelectedDay].Sunset = true;
              }
              else
              {
                Day[SelectedDay].Sunset = false;
              }
              break;
            case 7:
              //Back Button
              DisplayIndex = 3;
              CursorIndex = 0;
              break;
            default:
              break;
          }
          break;
        case 6:
          { //Set Current Time -- using SetCurrentTime
            switch (CursorIndex)
            {
              case 0: //increment hour
                {
                  CurrentTime = TimeIncDec(CurrentTime, true, 0);
                  double DoubleConvert = CurrentTime;
                  SetCurrentTime = (DoubleConvert / 100);
                }
                break;
              case 1: //decrement hour
                {
                  CurrentTime = TimeIncDec(CurrentTime, false, 0);
                  double DoubleConvert = CurrentTime;
                  SetCurrentTime = (DoubleConvert / 100);
                }
                break;
              case 2: //increment 10 minutes
                {
                  CurrentTime = TimeIncDec(CurrentTime, true, 1);
                  double DoubleConvert = CurrentTime;
                  SetCurrentTime = (DoubleConvert / 100);
                }
                break;
              case 3: //decrement 10 minutes
                {
                  CurrentTime = TimeIncDec(CurrentTime, false, 1);
                  double DoubleConvert = CurrentTime;
                  SetCurrentTime = (DoubleConvert / 100);
                }
                break;
              case 4: //increment 1 minute
                {
                  CurrentTime = TimeIncDec(CurrentTime, true, 2);
                  double DoubleConvert = CurrentTime;
                  SetCurrentTime = (DoubleConvert / 100);
                }
                break;
              case 5: //decrement 1 minute
                {
                  CurrentTime = TimeIncDec(CurrentTime, false, 2);
                  double DoubleConvert = CurrentTime;
                  SetCurrentTime = (DoubleConvert / 100);
                }
                break;
              case 6:
                {
                  //Cycles to set day screen
                  DisplayIndex = 7;
                  CursorIndex = 0;
                }
                break;
              default:
                break;
            }
          }
          break;
        case 7:
          { //Set day screen
            CurrentDay = CursorIndex;
            DisplayIndex = 2;
            CursorIndex = 6; //set to cursor to the top of the page
          }
          break;
        case 8:
          {
            //Motor Pos Setup

          }
          break;
        case 9:
          {
            //Motor Speed Setup

          }
          break;
        default:
          break;
      } //Main switch case close
    }
  }
  if (PRGButton(true))
  {
    UpdateFrame = true;
    if (BlankFrame == true) //Resets the sleep functions
    {
      DisplayIndex = 0; //Display Position is reset upon wake
      CursorIndex = 0; //Cursor Position is reset upon wake
      BlankFrame = false;
    }
    else
    {
      //Serial.println(CursorIndex);
      switch (DisplayIndex)
      {
        case 0:
          IncLoop((sizeof(MenuPositions) / sizeof(MenuPositions[0])));
          break;
        case 1:
          //Config Screen
          IncLoop(8);
          break;
        case 2:
          //Settings Screen
          if (CursorIndex == (SETTINGSCURSOROPTIONS - 1)) //incrementing past 5 for time space
          {
            CursorIndex = 0;
          }
          else if (CursorIndex == 5)
          {
            CursorIndex = CursorIndex + 2;
          }
          else
          {
            CursorIndex++;
          }
          break;
        case 3:
        case 5: /// VVVV The Routine Below Carries out both!! VVVVVVV
        case 4:
          //OPEN / CLOSE Time Config
          IncLoop((sizeof(ConfigSettingsPositions) / sizeof(ConfigSettingsPositions[0])));
          //Serial.print("CursorIndex: ");
          //Serial.println(CursorIndex);
          break;
        case 6: //Cursor for manual day setup
          {
            IncLoop(7);
          }
          break;
        case 7: //Cursor for day pick
          {
            IncLoop(7);
            //Config Screen
            /*
              if (CursorIndex == (CONFIGCURSOROPTIONS - 2))
              {
              CursorIndex = 0;
              }
              else
              {
              CursorIndex++;
              }
            */
          }
          break;
        case 8: /// VVVV The Routine Below Carries out both!! VVVVVVV
        case 9:
          {
            IncLoop((sizeof(ConfigSettingsPositions) / sizeof(ConfigSettingsPositions[0])));
          }
          break;
        default:
          break;
      }
      //Serial.println((sizeof(MenuPositions) / sizeof(MenuPositions[0])));
    }
  }
}

/*
   if (CursorIndex == ((sizeof(ConfigSettingsPositions) / sizeof(ConfigSettingsPositions[0])) - 1))
          {
            CursorIndex = 0;
          }
          else
          {
            CursorIndex++;
          }
          break;
*/

void IncLoop(int RoutineLength) //handles routine cursor looping //Cardinal Counting
{
  if (CursorIndex == (RoutineLength - 1)) //arrays start at 0
  {
    CursorIndex = 0;
  }
  else
  {
    CursorIndex++;
  }
}

void ScreenHandler() //Draws Screen Based on the display position
{
  //Decide which screen to display
  switch (DisplayIndex)
  {
    case 0:
      //Menu Screen
      MenuScreen();
      break;
    case 1:
      //Config Screen
      ConfigScreen();
      break;
    case 2:
      //Settings Screen
      SettingsScreen();
      break;
    case 3:
      //Config Options Screen
      ConfigOptionsScreen();
      break;
    case 4:
      ScheduleScreen(true);
      break;
    case 5:
      ScheduleScreen(false);
      break;
    case 6:
      SetTimeScreen();
      break;
    case 7:
      SetDayScreen();
      break;
    case 8:
      MotorPosScreen();
      break;
    case 9:
      MotorSpeedScreen();
      break;
    default:
      break;
  }
}

int TimeIncDec(int Value, bool IsAdd, byte Unit)
{
  int HourValues = (((Value / 1000U) % 10) * 10) + ((Value / 100U) % 10);
  int TenMinValues = (((Value / 10U) % 10) * 10) + ((Value / 1U) % 10);
  int MinValues = ((Value / 1U) % 10);
  switch (Unit)
  {
    case 0: //Hour Values
      {
        if (IsAdd == true)
        {
          if (HourValues < 23)
          {
            HourValues++;
          }
          else
          {
            HourValues = 0;
          }
        }
        else
        {
          if (HourValues > 0)
          {
            HourValues--;
          }
          else
          {
            HourValues = 23;
          }
        }
      }
      break;
    case 1: //Ten Minute Values
      {
        if (IsAdd == true)
        {
          if (TenMinValues < 50)
          {
            TenMinValues = TenMinValues + 10;
          }
          else
          {
            if (HourValues < 23)
            {
              HourValues++;
            }
            else
            {
              HourValues = 0;
            }
            TenMinValues = TenMinValues - 50;
          }
        }
        else
        {
          if (TenMinValues > 10)
          {
            TenMinValues = TenMinValues - 10;
          }
          else
          {
            if (HourValues > 0)
            {
              HourValues--;
            }
            else
            {
              HourValues = 23;
            }
            TenMinValues = TenMinValues + 50;
          }
        }
      }
      break;
    case 2: //Minute Values
      {
        if (IsAdd == true)
        {
          if (TenMinValues < 59)
          {
            TenMinValues++;
          }
          else
          {
            if (HourValues < 23)
            {
              HourValues++;
            }
            else
            {
              HourValues = 0;
            }
            TenMinValues = 0;
          }
        }
        else
        {
          if (TenMinValues > 0)
          {
            TenMinValues--;
          }
          else
          {
            if (HourValues > 0)
            {
              HourValues--;
            }
            else
            {
              HourValues = 23;
            }
            TenMinValues = 59;
          }
        }
      }
      break;
    default:
      break;
  }

  Value = (HourValues * 100) + (TenMinValues);
  //Serial.print("TimeIncDec - Final - ");
  //Serial.println(Value);
  return Value;
}

bool WifiConnection(bool GetOnStartup) //Checks if we are connected to wifi and reconnected if not
{
  //WiFi.mode(WIFI_STA);
  byte TryLimit = 0;
  Serial.println("Wifi - Test Connection");
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.begin(ssid, password);

    // attempt to connect to Wifi network:
    unsigned long ConnectionTimer = millis() + WIFICONNECTIONREFRESH;
    while (WiFi.status() != WL_CONNECTED)
    {
      if (GetOnStartup == true) //Takes over display as it's starting
      {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_open_iconic_www_1x_t);
        u8g2.drawStr((SCREENWIDTH * 0.85), (SCREENHEIGHT * 0.1), "H");
        IntroScreen();
        u8g2.sendBuffer();
      }
      else
      {
        u8g2.setFont(u8g2_font_open_iconic_www_1x_t);
        u8g2.drawStr((SCREENWIDTH * 0.85), (SCREENHEIGHT * 0.1), "H");
      }

      Serial.print(".");
      delay(50);
      if (millis() > ConnectionTimer)
      {
        WiFi.begin(ssid, password);
        ConnectionTimer = millis() + WIFICONNECTIONREFRESH;
        Serial.println("Wifi - Connection Try");
      }

      //Push Button to skip
      if (PRGButton(true) || EnterButton(true))
      {
        break;
      }

      if (TryLimit < WIFITRYLIMIT) //Connection Try Limit
      {
        TryLimit++;
      }
      else
      {
        return false;
        //break;
      }
    }
  }
  else
  {
    Serial.println("Wifi - Already Connected");

  }

  Serial.println("Wifi - Connected");
  return true;
}

bool MQTTConnection(bool GetOnStartup)
{
  if (MQTTServer == true) //Only runs if MQTT Server is enabled
  {
    byte TryLimit = 0;
    Serial.print("MQTT - ");

    unsigned long RefreshTimer = millis() + MQTTCONNECTIONREFRESH;

    Serial.print("MQTT Buffer Size: ");
    Serial.println(client.getBufferSize());

    while (!client.connected())
    {
      if (GetOnStartup == true) //Only takes charge of the screen if on startup
      {
        u8g2.clearBuffer();
        IntroScreen();
        u8g2.setFont(u8g2_font_open_iconic_www_1x_t);
        u8g2.drawStr((SCREENWIDTH * 0.90), (SCREENHEIGHT * 0.1), "E");
        u8g2.sendBuffer();
      }
      else
      {
        u8g2.setFont(u8g2_font_open_iconic_www_1x_t);
        u8g2.drawStr((SCREENWIDTH * 0.90), (SCREENHEIGHT * 0.1), "E");
      }

      if (millis() > RefreshTimer)
      {
        if (client.connect("espClient", "name", "password"))
        {
          Serial.println("Connected");

          client.publish("outTopic", "hello world");

          client.subscribe(MQTTSUB_BLIND); //Subs to the control topic
          client.subscribe(MQTTSUB_TIME);
          client.subscribe(MQTTSUB_SUN);
          //client.subscribe("timeSpaff");

        }
        else
        {
          Serial.print("failed, rc= ");
          Serial.println(client.state());
          Serial.println("MQTT - trying again in 5 seconds");

          if (TryLimit > MQTTTRYLIMIT) //Try limit on connection to MQTT before skip
          {
            MQTTServer = false; //Disable the MQTT server as it's not connecting
            break;
          }
          else
          {
            TryLimit++;
          }

          RefreshTimer = millis() + MQTTCONNECTIONREFRESH;
        }
      }

      if (PRGButton(true) || EnterButton(true))
      {
        MQTTServer = false;

        break;
      }
    }
  }
  return true;
}

void MenuScreen()
{
  u8g2.setFont(u8g2_font_fub11_tf);

  //Blind Status
  if (CursorIndex == 0)
  {
    u8g2.drawBox((SCREENWIDTH * 0.14), (SCREENHEIGHT * 0.15), (SCREENWIDTH * 0.80), (SCREENHEIGHT * 0.33));
    u8g2.setDrawColor(0);
  }
  else
  {
    u8g2.drawFrame((SCREENWIDTH * 0.14), (SCREENHEIGHT * 0.15), (SCREENWIDTH * 0.80), (SCREENHEIGHT * 0.33));
  }

  if (BlindOpen == true)
  {
    u8g2.drawStr((SCREENWIDTH * 0.17), MenuPositions[0], "Close Blinds");
  }
  else
  {
    u8g2.drawStr((SCREENWIDTH * 0.17), MenuPositions[0], "Open Blinds");
  }



  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_7x14_mf);
  u8g2.drawStr((SCREENWIDTH * 0.08), MenuPositions[2], "Config.");
  u8g2.drawStr((SCREENWIDTH * 0.53), MenuPositions[2], "Settings");

  DisplayNextEvent();
}

void DisplayNextEvent() //Displays the next event scheduled
{
  //Next Event
  u8g2.setFont(u8g2_font_7x14_mf);
  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.75), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.75));

  //dependant on schedule
  if (Day[CurrentDay].DayIsEnabled == true) //check that this day is enabled
  {
    u8g2.drawStr((SCREENWIDTH * 0.0), (SCREENHEIGHT * 0.90), "Will");
    u8g2.drawStr((SCREENWIDTH * 0.55), (SCREENHEIGHT * 0.90), "at");

    //Logic Dependant Open and Close Time
    char OpenBufferArray[10];
    char CloseBufferArray[10];
    int OpenValue = 0;
    int CloseValue = 0;

    //Descriminates if it's user set or sunrise / set before comparing them both
    if (Day[CurrentDay].Sunrise == true)
    {
      OpenValue = CurrentSunrise;
      double ValueConvert = (CurrentSunrise / 100);
      dtostrf(ValueConvert, 2, 2, OpenBufferArray);
    }
    else
    {
      OpenValue = Day[CurrentDay].OpenTime;
      dtostrf(Day[CurrentDay].ViewOpenTime, 2, 2, OpenBufferArray);
    }

    if (Day[CurrentDay].Sunset == true)
    {
      OpenValue = CurrentSunset;
      double ValueConvert = (CurrentSunset / 100);
      dtostrf(ValueConvert, 2, 2, CloseBufferArray);
    }
    else
    {
      OpenValue = Day[CurrentDay].CloseTime;
      dtostrf(Day[CurrentDay].ViewCloseTime, 2, 2, CloseBufferArray);
    }

    if (CurrentTime < OpenValue)
    {
      u8g2.drawStr((SCREENWIDTH * 0.25), (SCREENHEIGHT * 0.90), "Open");
      u8g2.drawStr((SCREENWIDTH * 0.70), (SCREENHEIGHT * 0.90), OpenBufferArray);
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.25), (SCREENHEIGHT * 0.90), "Close");
      u8g2.drawStr((SCREENWIDTH * 0.70), (SCREENHEIGHT * 0.90), CloseBufferArray);
    }

  }
  else //finds next day which will open
  {

    byte NextOpenDay = 15;
    byte DayCheckValue = CurrentDay + 1;

    for (byte DayIndex = 0; DayIndex != 7; DayIndex++)
    {
      if (Day[DayCheckValue].DayIsEnabled == true)
      {
        NextOpenDay = DayIndex + 1;
        DayIndex = 7;

        break;
      }
      //Increment for next value in loop
      if (DayCheckValue == 6)
      {
        DayCheckValue = 0;
      }
      else
      {
        DayCheckValue++;
      }
    }

    if (NextOpenDay != 15)
    {
      u8g2.drawStr((SCREENWIDTH * 0.0), (SCREENHEIGHT * 0.90), "Will");
      u8g2.drawStr((SCREENWIDTH * 0.25), (SCREENHEIGHT * 0.90), "run on");
      u8g2.drawStr((SCREENWIDTH * 0.55), (SCREENHEIGHT * 0.90), Day[NextOpenDay].DayName);
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.0), (SCREENHEIGHT * 0.90), "ALL DAYS DISABLED");
    }
  }
}

void ConfigScreen()
{
  u8g2.setFont(u8g2_font_7x14_mf);
  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.25), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.25));
  u8g2.drawStr((SCREENWIDTH * 0.025), (SCREENHEIGHT * 0.15), "Configure Days");

  u8g2.setFont(u8g2_font_helvB10_tf);

  //Displays the three Options Pages
  if (CursorIndex < 3)
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[0], Day[0].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[1], Day[1].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[2], Day[2].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[3], Day[3].DayName);
  }
  else if (CursorIndex < 6)
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[0], Day[3].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[1], Day[4].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[2], Day[5].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[3], Day[6].DayName);
  }
  else
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[0], Day[6].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[1], "Save");
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[3], Day[0].DayName);
  }
  //Serial.println(CursorIndex);
}

void ConfigOptionsScreen()
{
  u8g2.setFont(u8g2_font_7x14_mf);
  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.25), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.25));
  u8g2.drawStr((SCREENWIDTH * 0.1), (SCREENHEIGHT * 0.15), Day[SelectedDay].DayName);

  if (CursorIndex < 3)
  {
    char BufferArray[20];

    //Transfer and output time values
    //itoa(Day[SelectedDay].OpenTime, BufferArray, 10);
    dtostrf(Day[SelectedDay].ViewOpenTime, 2, 2, BufferArray);
    u8g2.drawStr((SCREENWIDTH * 0.7), ConfigSettingsPositions[1], BufferArray);

    //itoa(Day[SelectedDay].CloseTime, BufferArray, 10);
    dtostrf(Day[SelectedDay].ViewCloseTime, 2, 2, BufferArray);
    u8g2.drawStr((SCREENWIDTH * 0.7), ConfigSettingsPositions[2], BufferArray);

    u8g2.setFont(u8g2_font_helvB10_tf);

    u8g2.drawStr((SCREENWIDTH * 0.08), ConfigSettingsPositions[0], "On / Off");
    u8g2.drawStr((SCREENWIDTH * 0.08), ConfigSettingsPositions[1], "Open Time ");
    u8g2.drawStr((SCREENWIDTH * 0.08), ConfigSettingsPositions[2], "Close Time ");
    u8g2.drawStr((SCREENWIDTH * 0.08), ConfigSettingsPositions[3], "Back");
    //this will be logic dependant
    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);

    if (Day[SelectedDay].DayIsEnabled == true)
    {
      u8g2.drawStr((SCREENWIDTH * 0.6), ConfigSettingsPositions[0], "x");
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.6), ConfigSettingsPositions[0], "y");
    }
  }
  else
  {
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawStr((SCREENWIDTH * 0.08), ConfigSettingsPositions[0], "Back");
    u8g2.drawStr((SCREENWIDTH * 0.08), ConfigSettingsPositions[3], "On / Off");

    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);

    if (Day[SelectedDay].DayIsEnabled == true)
    {
      u8g2.drawStr((SCREENWIDTH * 0.6), ConfigSettingsPositions[3], "x");
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.6), ConfigSettingsPositions[3], "y");
    }
  }
}

void ScheduleScreen(bool IsOpenTime)
{
  char BufferArray[10];
  bool SunBool;

  u8g2.setFont(u8g2_font_7x14_mf);
  if (IsOpenTime == true)
  {
    if (Day[SelectedDay].ViewOpenTime < 10)
    {
      char SubBufferArray[10];
      dtostrf(Day[SelectedDay].ViewOpenTime, 2, 2, SubBufferArray);
      BufferArray[0] = '0';
      for (byte BufInd = 0; BufInd != 4; BufInd++)
      {
        BufferArray[BufInd + 1] = SubBufferArray[BufInd];
      }
    }
    else
    {
      dtostrf(Day[SelectedDay].ViewOpenTime, 2, 2, BufferArray);
    }

    u8g2.drawStr((SCREENWIDTH * 0.10), (SCREENHEIGHT * 0.15), "Set Open Time");
    SunBool = Day[SelectedDay].Sunrise;
  }
  else
  {
    if (Day[SelectedDay].ViewCloseTime < 10)
    {
      char SubBufferArray[10];
      dtostrf(Day[SelectedDay].ViewCloseTime, 2, 2, SubBufferArray);
      BufferArray[0] = '0';
      for (byte BufInd = 0; BufInd != 4; BufInd++)
      {
        BufferArray[BufInd + 1] = SubBufferArray[BufInd];
      }
    }
    else
    {
      dtostrf(Day[SelectedDay].ViewCloseTime, 2, 2, BufferArray);
    }

    u8g2.drawStr((SCREENWIDTH * 0.10), (SCREENHEIGHT * 0.15), "Set Close Time");
    SunBool = Day[SelectedDay].Sunset;

    //Serial.print("Display Output - ");
    //Serial.println(BufferArray);
  }

  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.25), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.25));

  if (CursorIndex < 6)
  {
    //Time Value
    u8g2.setFont(u8g2_font_inr21_mf);
    u8g2.drawStr((SCREENWIDTH * 0.10), SchedulePositions[0], BufferArray);

    u8g2.setFont(u8g2_font_helvB10_tf);
    if (IsOpenTime == true)
    {
      u8g2.drawStr((SCREENWIDTH * 0.10), SchedulePositions[1], "Sunrise");
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.10), SchedulePositions[1], "Sunset");
    }

    if (SunBool == true)
    {
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawStr((SCREENWIDTH * 0.8), SchedulePositions[1], "x");
    }
    else
    {
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawStr((SCREENWIDTH * 0.8), SchedulePositions[1], "y");
    }

  }
  else
  {
    //Time Value
    u8g2.setFont(u8g2_font_inr21_mf);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[3], BufferArray);

    u8g2.setFont(u8g2_font_helvB10_tf);
    if (IsOpenTime == true)
    {
      u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[0], "Sunrise");
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[0], "Sunset");
    }

    if (SunBool == true)
    {
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawStr((SCREENWIDTH * 0.8), ConfigSettingsPositions[0], "x");
    }
    else
    {
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawStr((SCREENWIDTH * 0.8), ConfigSettingsPositions[0], "y");
    }

    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[1], "Back");
  }
}

void SetTimeScreen()
{
  char BufferArray[10];

  if (SetCurrentTime < 10)
  {
    char SubBufferArray[10];
    dtostrf(SetCurrentTime, 2, 2, SubBufferArray);
    BufferArray[0] = '0';
    for (byte BufInd = 0; BufInd != 4; BufInd++)
    {
      BufferArray[BufInd + 1] = SubBufferArray[BufInd];
    }
  }
  else
  {
    dtostrf(SetCurrentTime, 2, 2, BufferArray);
  }
  u8g2.setFont(u8g2_font_helvB10_tf);
  u8g2.drawStr((SCREENWIDTH * 0.10), (SCREENHEIGHT * 0.15), "Set Clock Time");
  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.25), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.25));

  //outputs time value
  u8g2.setFont(u8g2_font_inr21_mf);
  u8g2.drawStr((SCREENWIDTH * 0.10), SchedulePositions[0], BufferArray);

  u8g2.setFont(u8g2_font_helvB10_tf);
  u8g2.drawStr((SCREENWIDTH * 0.10), SchedulePositions[1], "Save");
}

void SetDayScreen()
{
  u8g2.setFont(u8g2_font_7x14_mf);
  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.25), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.25));
  u8g2.drawStr((SCREENWIDTH * 0.025), (SCREENHEIGHT * 0.15), "Set the Day");

  u8g2.setFont(u8g2_font_helvB10_tf);

  //Displays the three Options Pages
  if (CursorIndex < 3)
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[0], Day[0].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[1], Day[1].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[2], Day[2].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[3], Day[3].DayName);
  }
  else if (CursorIndex < 6)
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[0], Day[3].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[1], Day[4].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[2], Day[5].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[3], Day[6].DayName);
  }
  else
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[0], Day[6].DayName);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigPositions[3], Day[0].DayName);
  }
}

void SettingsScreen()
{
  u8g2.setFont(u8g2_font_7x14_mf);
  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.25), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.25));
  u8g2.drawStr((SCREENWIDTH * 0.025), (SCREENHEIGHT * 0.15), "Change Settings");
  if (CursorIndex < 3)
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[0], "Blind Positions");

    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[1], "Wifi Time?");
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[2], "Blind Speed");
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[3], "MQTTConnect?");

    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    if (TimeServer == true)
    {
      u8g2.drawStr((SCREENWIDTH * 0.8), ConfigSettingsPositions[1], "x");
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.8), ConfigSettingsPositions[1], "y");
    }

    if (MQTTServer == true)
    {
      u8g2.drawStr((SCREENWIDTH * 0.85), ConfigSettingsPositions[3], "x");
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.85), ConfigSettingsPositions[3], "y");
    }

  }
  else if (CursorIndex < 5)
  {
    u8g2.setFont(u8g2_font_7x14_mf);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[0], "MQTT Connect?");
    if (TimeServer == true)
    {
      u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[1], "Get fresh Time");

    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[1], "Set Day/Time");
    }
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[3], "Back");


    //Output Name of the week values
    char NamePrefix [4];
    NamePrefix[0] = Day[CurrentDay].DayName[0];
    NamePrefix[1] = Day[CurrentDay].DayName[1];
    NamePrefix[2] = Day[CurrentDay].DayName[2];
    NamePrefix[3] = '-';
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[2], NamePrefix);
    //Transfer and output time values
    char BufferArray[20];
    itoa(CurrentTime, BufferArray, 10);
    u8g2.drawStr((SCREENWIDTH * 0.40), ConfigSettingsPositions[2], BufferArray);



    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    if (MQTTServer == true)
    {
      u8g2.drawStr((SCREENWIDTH * 0.8), ConfigSettingsPositions[0], "x");
    }
    else
    {
      u8g2.drawStr((SCREENWIDTH * 0.8), ConfigSettingsPositions[0], "y");
    }

  }
  else
  {
    u8g2.setFont(u8g2_font_7x14_mf);
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[0], "Back");
    u8g2.drawStr((SCREENWIDTH * 0.10), ConfigSettingsPositions[3], "Blind Positions");
  }

}

void MotorPosScreen()
{
  u8g2.setFont(u8g2_font_7x14_mf);
  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.25), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.25));
  u8g2.drawStr((SCREENWIDTH * 0.025), (SCREENHEIGHT * 0.15), "Blind Position Setup");

  if (CursorIndex < 3)
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[0], "Open Pos");
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[1], "Closed Pos");
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[2], "Test"); //This test sequence can be used for both motor setups
  }
  else
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[0], "Filler");
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[1], "Filler");
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[2], "Back");
  }

}

void MotorSpeedScreen()
{
  u8g2.setFont(u8g2_font_7x14_mf);
  u8g2.drawLine((SCREENWIDTH * 0), (SCREENHEIGHT * 0.25), (SCREENWIDTH * 1), (SCREENHEIGHT * 0.25));
  u8g2.drawStr((SCREENWIDTH * 0.025), (SCREENHEIGHT * 0.15), "Blind Motor Speed");

  if (CursorIndex < 3)
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[0], "Increment");
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[1], "Decrement");
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[2], "Test"); //This test sequence can be used for both motor setups
  }
  else
  {
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[0], "Filler");
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[1], "Filler");
    u8g2.drawStr((SCREENWIDTH * 0.10), MoveSettingsPositions[2], "Back");
  }
}
//May add acceleration Curve later if I have time and if motor torque isn't an issue


void MoveBlinds(bool ManualCall) //Ensure that this doesn't effect the time poll rate
{
  if (ManualCall == true)
  {
    u8g2.sendBuffer();
  }
  else
  {
    ScreenOffTimer = millis() + SCREENSAVERTIMEOUT; //Keep display on after
  }
  //This function Moves the blinds returns to normal once complete

  u8g2.setFont(u8g2_font_open_iconic_all_4x_t);
  u8g2.clearBuffer();
  u8g2.drawCircle((SCREENWIDTH * 0.50), (SCREENHEIGHT * 0.50), MOVESCREENCIRCLE, U8G2_DRAW_ALL);

  unsigned long BlindMoveTime = 5000; //Time at start of move time
  unsigned long BlindTimer = 0; //will be set with full time once config has been completed

  if (BlindOpen == true) //Blind Close
  {
    u8g2.drawStr((SCREENWIDTH * 0.40), (SCREENHEIGHT * 0.50), "S");
    u8g2.sendBuffer();

    BlindTimer = millis() + BlindMoveTime;
    Motor.moveTo(200);

    while (Motor.distanceToGo() != 0)
    {
      if (EnterButton(true) == true) //Motor EStop
      {
        Motor.moveTo(0);
        u8g2.setFont(u8g2_font_7x14_mf);
        u8g2.drawStr((SCREENWIDTH * 0.32), (SCREENHEIGHT * 0.80), "STOPPED");
        u8g2.sendBuffer();
        delay(3000);
        break;
      }
    }
    BlindOpen = false;
  }
  else //Blind Open
  {
    u8g2.drawStr((SCREENWIDTH * 0.40), (SCREENHEIGHT * 0.50), "H");//
    u8g2.sendBuffer();
    BlindTimer = millis() + BlindMoveTime;

    Motor.moveTo(200);
    while (Motor.distanceToGo() != 0)
    {
      if (EnterButton(true) == true) //Motor EStop
      {
        Motor.moveTo(0);
        u8g2.setFont(u8g2_font_7x14_mf);
        u8g2.drawStr((SCREENWIDTH * 0.32), (SCREENHEIGHT * 0.80), "STOPPED");
        u8g2.sendBuffer();
        delay(3000);
        break;
      }
    }
    BlindOpen = true;
  }
}

bool EnterButton(bool NormalScreen) //Function to test the Enter Button and handle debounce timer
{
  if (digitalRead(SELECTPIN) == LOW)
  {
    if (NormalScreen == true) //this case handles settings menus not timing out on the user
    {
      ScreenOffTimer = millis() + SCREENSAVERTIMEOUT; //Updates the screen sleep SETTINGSSAVERAMOUNT
    }
    else
    {
      ScreenOffTimer = millis() + SETTINGSSAVERAMOUNT;
    }

    if (EnterButtonDebounceTimer < millis())
    {
      EnterButtonDebounceTimer = millis() + BUTTONDEBOUNCETIME;
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    return false;
  }
}

bool PRGButton(bool NormalScreen) //Function to test the Enter Button and handle debounce timer
{
  if (digitalRead(PRGBUTTONPIN) == LOW)
  {
    if (NormalScreen == true) //this case handles settings menus not timing out on the user
    {
      ScreenOffTimer = millis() + SCREENSAVERTIMEOUT; //Updates the screen sleep SETTINGSSAVERAMOUNT
    }
    else
    {
      ScreenOffTimer = millis() + SETTINGSSAVERAMOUNT;
    }

    if (PRGButtonDebounceTimer < millis())
    {
      PRGButtonDebounceTimer = millis() + BUTTONDEBOUNCETIME;
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    return false;
  }
}

void IntroScreen() //Intro Screen for style points - one animation frame per call
{
  double CurrentSecond = ((millis() / 1000U) % 10); //Gets the seconds in millis
  double CurrentMiliSecond = ((millis() / 100U) % 10);
  double RadVal = ((((CurrentSecond / 10) + (CurrentMiliSecond / 100)) * SCREENWIDTH) / 2);

  u8g2.setFont(u8g2_font_helvB10_tf);
  u8g2.drawCircle((SCREENWIDTH * 0.50), (SCREENHEIGHT * 0.50), INNERCIRCLE, U8G2_DRAW_ALL);
  u8g2.drawStr((SCREENWIDTH * 0.41), (SCREENHEIGHT * 0.52), "AW");

  if (RadVal > CIRCLEMIN)
  {
    u8g2.drawCircle((SCREENWIDTH * 0.50), (SCREENHEIGHT * 0.50), RadVal, U8G2_DRAW_ALL);
    u8g2.drawCircle((SCREENWIDTH * 0.50), (SCREENHEIGHT * 0.50), (RadVal - 1), U8G2_DRAW_ALL);
  }
  else if (RadVal < CIRCLEMIN)
  {
    //default circle sizes
    u8g2.drawCircle((SCREENWIDTH * 0.50), (SCREENHEIGHT * 0.50), (CIRCLEMAX - (((CIRCLEMAX - CIRCLEMIN) / CIRCLEMIN) * RadVal)), U8G2_DRAW_ALL);
    u8g2.drawCircle((SCREENWIDTH * 0.50), (SCREENHEIGHT * 0.50), ((CIRCLEMAX - (((CIRCLEMAX - CIRCLEMIN) / CIRCLEMIN) * RadVal)) - 1), U8G2_DRAW_ALL);
  }
}

bool GetMQTT(byte MQTTCommand)
{

  Serial.println("GetMQTT - Checking Wifi");
  if (WifiConnection(false) == true) //Test Wifi Connection - false - not on startup
  {
    Serial.println("GetMQTT - Wifi Connected");
    if (MQTTConnection(false))
    {
      Serial.println("GetMQTT - MQTT Connected");

      //Sends get time
      switch (MQTTCommand)
      {
        case 0: //send time pub
          {
            client.publish(MQTTPUB_TIME, "get");
          }
          break;
        case 1: //send sun time pub
          {
            client.publish(MQTTPUB_SUN, "get");
          }
          break;
        case 2: //send error pub
          {
            client.publish(MQTTPUB_TIME, MQTTPUBERRORVAL);
          }
          break;
        default:
          break;
      }
      return true;
    }
    else
    {
      Serial.println("GetMQTT - Couldn't Connect MQTT");
      return false;
    }
  }
  else
  {
    Serial.println("GetMQTT - Couldn't Connect Wifi");
    return false;
  }
}

void callback(char* topic, byte * payload, unsigned int length)
{
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.

  Serial.println(topic);
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  String Stringtopic(topic);

  Serial.println("Callback - Running");

  if (Stringtopic.equals("spaff/time"))
  {
    Serial.println("Callback - Updating Time");

    const size_t capacity = JSON_OBJECT_SIZE(2) + 20;
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, payload);

    double TimeParse = doc["time"];
    Serial.print("TimeParse - ");
    Serial.println(TimeParse);

    if (TimeParse < 25) //Checks that its in range
    {
      TimeParse = (TimeParse * 100);
      CurrentTime = TimeParse;

    }

    byte DayParse = doc["day"];
    Serial.print("DayParse - ");
    Serial.println(DayParse);

    if (DayParse < 7)
    {
      CurrentDay = DayParse;
    }

    ClockTimer = millis() + TICKINTERVAL; //Reset clock Timer Values
  }
  else if (Stringtopic.equals("spaff/sun"))
  {
    Serial.println("Callback - Updating Open/Close");

    const size_t capacity = JSON_OBJECT_SIZE(2) + 20;
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, payload);
    float SunriseParse = doc["sunrise"];
    float SunsetParse = doc["sunset"];

    Serial.print("CurrentSunrise - ");
    Serial.println(SunriseParse);
    Serial.print("CurrentSunset - ");
    Serial.println(SunsetParse);

    if (SunriseParse < 25)
    {
      SunriseParse = (SunriseParse * 100);
      CurrentSunrise = SunriseParse;
      Serial.print("CurrentSunrise - ");
      Serial.println(CurrentSunrise);
    }

    if (SunsetParse < 25)
    {
      SunsetParse = (SunsetParse * 100);
      CurrentSunset = SunsetParse;
      Serial.print("CurrentSunset - ");
      Serial.println(CurrentSunset);
    }
  }
  else if (Stringtopic.equals(MQTTSUB_BLIND))
  {
    Serial.println("Callback - Open / Close has been Called");
  }
  else
  {
    Serial.println("Callback - Does not any topic value");
  }

}

char* GenName(char Name [4], byte Number)
{
  char NumBuffer [2];
  char NameBuffer [6];

  itoa(Number, NumBuffer, 10);

  strcpy(NameBuffer, Name);
  strcat(NameBuffer, NumBuffer);

  //Serial.println(NameBuffer);
  return NameBuffer;
}

bool PrefsWriteRead(bool IsWrite)
{
  preferences.begin("my-app", false);
  //Reads and writes values to the Eeprom
  if (IsWrite == true) //Write to eeprom address
  {
    Serial.println("PREFS - Memory is being written");
    preferences.putInt("Flag", 5);//Prefs written flag

    for (byte index = 0; index != 7; index++)
    {

      preferences.putInt(GenName("OT", index), Day[index].OpenTime);
      Serial.println(preferences.getInt(GenName("OT", index), 999));

      preferences.putFloat(GenName("VOT", index), Day[index].ViewOpenTime);

      preferences.putInt(GenName("CT", index), Day[index].CloseTime);

      preferences.putFloat(GenName("VCT", index), Day[index].ViewCloseTime);

      preferences.putBool(GenName("DIE", index), Day[index].DayIsEnabled);

      preferences.putBool(GenName("SR", index), Day[index].Sunrise);

      preferences.putBool(GenName("SS", index), Day[index].Sunset);

    }

    return true;
  }
  else //Read from eeprom address
  {
    byte ReadFlag = 0;
    ReadFlag = preferences.getInt("Flag", 9);

    if (ReadFlag == 5)
    {
      Serial.println("PREFS - Memory is written to");
      for (byte index = 0; index != 7; index++)
      {

        Day[index].OpenTime = preferences.getInt(GenName("OT", index), 801);
        Serial.println(preferences.getInt(GenName("OT", index), 801));

        Day[index].ViewOpenTime = preferences.getFloat(GenName("VOT", index), 8.01);
        Serial.println(preferences.getFloat(GenName("VOT", index), 8.01));

        Day[index].CloseTime = preferences.getInt(GenName("CT", index), 2001);
        Serial.println(preferences.getInt(GenName("CT", index), 2001));

        Day[index].ViewCloseTime = preferences.getFloat(GenName("VCT", index), 20.01);
        Serial.println(preferences.getFloat(GenName("VCT", index), 20.01));

        Day[index].DayIsEnabled = preferences.getBool(GenName("DIE", index), true);
        Serial.println(preferences.getBool(GenName("DIE", index), true));

        Day[index].Sunrise = preferences.getBool(GenName("SR", index), false);
        Serial.println(preferences.getBool(GenName("SR", index), false));

        Day[index].Sunset = preferences.getBool(GenName("SS", index), false);
        Serial.println(preferences.getBool(GenName("SS", index), false));
      }
      preferences.end();
      return true;
    }
    else
    {
      Serial.println("PREFS - Memory not written to");
      preferences.end();
      return false;
    }
  }
  preferences.end();
  return false;
}

bool EEWriteRead(bool IsWrite)
{

  int address = 4; //not starting from 0 for corruption

  //Reads and writes values to the Eeprom
  if (IsWrite == true) //Write to eeprom address
  {
    Serial.println("EEPROM - Memory is being written");
    EEPROM.writeByte(address, 5); //Eeprom written flag
    address = address + sizeof(byte);

    for (byte index = 0; index != 7; index++)
    {
      EEPROM.writeString(address, Day[index].DayName);
      address = address + 10;

      EEPROM.writeInt(address, Day[index].OpenTime);
      address = address + sizeof(Day[index].OpenTime);

      EEPROM.writeFloat(address, Day[index].ViewOpenTime);
      address = address + sizeof(Day[index].ViewOpenTime);

      EEPROM.writeInt(address, Day[index].CloseTime);
      address = address + sizeof(Day[index].CloseTime);

      EEPROM.writeFloat(address, Day[index].ViewCloseTime);
      address = address + sizeof(Day[index].ViewCloseTime);

      EEPROM.writeBool(address, Day[index].DayIsEnabled);
      address = address + sizeof(Day[index].DayIsEnabled);

      EEPROM.writeBool(address, Day[index].Sunrise);
      address = address + sizeof(Day[index].Sunrise);

      EEPROM.writeBool(address, Day[index].Sunset);
      address = address + sizeof(Day[index].Sunset) + 10;
      address = address + 20;
    }
    EEPROM.commit();
    return true;
  }
  else //Read from eeprom address
  {
    byte ReadFlag = 0;
    ReadFlag = EEPROM.readByte(address);

    if (ReadFlag == 5)
    {
      Serial.println("EEPROM - Memory is written to");
      address = address + sizeof(byte);

      for (byte index = 0; index != 7; index++)
      {
        Serial.println(EEPROM.readString(address));

        address = address + 10;

        Day[index].OpenTime = EEPROM.readInt(address);
        Serial.println(EEPROM.readInt(address));

        address = address + sizeof(Day[index].OpenTime);

        Day[index].ViewOpenTime = EEPROM.readFloat(address);
        Serial.println(EEPROM.readFloat(address));

        address = address + sizeof(Day[index].ViewOpenTime);

        Day[index].CloseTime = EEPROM.readInt(address);
        Serial.println(EEPROM.readInt(address));

        address = address + sizeof(Day[index].CloseTime);

        Day[index].ViewCloseTime = EEPROM.readFloat(address);
        Serial.println(EEPROM.readFloat(address));

        address = address + sizeof(Day[index].ViewCloseTime);

        Day[index].DayIsEnabled = EEPROM.readByte(address);
        Serial.println(EEPROM.readByte(address));

        address = address + sizeof(Day[index].DayIsEnabled);

        Day[index].Sunrise = EEPROM.readByte(address);
        Serial.println(EEPROM.readByte(address));

        address = address + sizeof(Day[index].Sunrise);

        Day[index].Sunset = EEPROM.read(address);
        Serial.println(EEPROM.read(address));

        address = address + sizeof(Day[index].Sunset) + 10;
        address = address + 20;
      }
      return true;
    }
    else
    {
      Serial.println("EEPROM - Memory not written to");
      return false;
    }
  }
  return false;
}

byte AnimationTick()
{
  double CurrentMiliSecond = ((millis() / 100U) % 10);

  Serial.print("CurrentMiliSecond - ");
  Serial.println(CurrentMiliSecond);
  return CurrentMiliSecond;
}
