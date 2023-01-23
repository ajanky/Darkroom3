////////////////////////
// pi Darkroom Timer
// V1.0 © 2023 3BOTS 3D Engineering GmbH
// written by ajanky
//
// license GNU GPLv3
////////////////////////


#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <RotaryEncoder.h>
#include "RPi_Pico_TimerInterrupt.h"
#include <Adafruit_Sensor.h>
#include "Adafruit_TSL2591.h"
#include "EEPROM.h"

#define LOAD_GFXFF
#define GFXFF 1
#define miniBold &FreeSansBold7pt7b
#define midiBold &FreeSansBold8pt7b
#define maxiBold &FreeSansBold11pt7b
#define maxi &FreeSans11pt7b
#define GLCD 0
#define FONT2 2
#define FONT4 4
#define FONT6 6
#define FONT7 7
#define TIMER_INTERVAL_US  50000L  // 50ms
#define ROTARYMIN -6
#define ROTARYMAX 6
#define TIMERMIN 0
#define TIMERMAX 99
#define PICO_I2C_SDA 0
#define PICO_I2C_SCL 1

#define DUKA 2
#define ENL 3
#define SW1 4  // Timer Start
#define SW2 5  // Enlarger
#define SW3 6  // Mode
#define SW4 7  // Duka Light
#define SW5 8  // Dev Timer
#define MEAS 27
#define KNOB 28
#define ROTA 10
#define ROTB 9
#define LED2 11
#define LED1 12
#define DS18 13
#define BEEP 22


RPI_PICO_Timer ITimer(0);
TFT_eSPI tft = TFT_eSPI();
RotaryEncoder *encoder = nullptr;
Adafruit_TSL2591 TSL2591 = Adafruit_TSL2591(2591);

uint16_t ir, full;
uint32_t lum;
String grade;
float lux, lo, hi, oldLo, oldHi, ratio, Time, paperSens, Sens, factor, expoTime;
bool Trigger, EnlSW, ModeSW, DukaSW, DevSW, MeasureSW, Knob, MS500, MS100, redLight, timerMode, ENL_ON, Dial;
int msCnt, newPos, Timer, newTime, pos = 0;
long oldPosition  = -999;


void setup()
{
  pinMode(DUKA,OUTPUT);
  pinMode(ENL,OUTPUT);
  pinMode(LED1,OUTPUT);
  pinMode(LED2,OUTPUT);
  pinMode(BEEP,OUTPUT);
  pinMode(SW1,INPUT);
  pinMode(SW2,INPUT);
  pinMode(SW3,INPUT);
  pinMode(SW4,INPUT);
  pinMode(SW5,INPUT);
  pinMode(MEAS,INPUT_PULLUP);
  pinMode(KNOB,INPUT);

  gpio_set_drive_strength(22, GPIO_DRIVE_STRENGTH_12MA);  // BEEPER needs more drive power

  Wire.setSDA(PICO_I2C_SDA);
  Wire.setSCL(PICO_I2C_SCL);

  EEPROM.begin(256);

  beep();

  encoder = new RotaryEncoder(ROTA, ROTB, RotaryEncoder::LatchMode::TWO03);
  // register interrupt routine
  attachInterrupt(digitalPinToInterrupt(ROTA), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTB), checkPosition, CHANGE);

  ITimer.attachInterruptInterval(TIMER_INTERVAL_US, TimerHandler);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  Serial.begin(115200); // For debug
  delay(3000);
  Serial.println("PICO DARKROOM 1.1");

  TSL2591.begin();  // start light sensor
  configureTSL();

  tft.drawLine(0,120,319,120, TFT_RED);
  drawGradient();

  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextFont(4);
  tft.setTextDatum(TL_DATUM);
  F_correction();
  Mode(timerMode); // show Mode

  // paperSens = 9.6;  //  Ilford Multigrade
  paperSens = 21;  // Foma Variospeed + FomaBrom Vario
  
  dukaLight(1); // keep Duka on ( or assign Duka button )
  redLight = 1; 

}

void loop()
{
  checkPosition();  // update knob value

  ////// check buttons  //////
  if(EnlSW)
  {
    ENL_ON = !ENL_ON;
    Enlarger(ENL_ON);
    // dukaLight(redLight);
    while(!digitalRead(SW2)) { };  // wait til ENL_ON released
    delay(100);
  }

  if(ModeSW)
  {
    timerMode = !timerMode;  // toggle timerMode mode
    Mode(timerMode);
    while(!digitalRead(SW3)) { };  // wait til ModeSW released
    delay(100);
  }

  if (timerMode)  // in timerMode knob sets time
  {
    // tft.fillRect(0,35,339,75,TFT_BLACK);  // clear gradient
    newTime = encoder->getPosition();
    if (newTime < TIMERMIN)  // constrain knob values
    {
      encoder->setPosition(TIMERMIN);
      newTime = TIMERMIN;
    } 
    else if (newTime > TIMERMAX) // constrain knob values
    {
      encoder->setPosition(TIMERMAX);
      newTime = TIMERMAX;
    }
    if (Time != newTime) showSeconds();
    
      Time = newTime;
  }


  if(MeasureSW && !timerMode)  // in Meter Mode check for light-sensor button
  {
    lightMeter();
    Enlarger(1); // switch on enlarger light
    delay(250);  // wait a moment

    getLightValues();  // measure and set Hi and Lo values

    calcTime();
    Enlarger(0);  // switch off enlarger light
  }


  if(Knob)
  {
    encoder->setPosition(0);
    while (Knob)
    {  
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextSize(1);
      tft.setTextFont(4);
      tft.setTextDatum(TL_DATUM);

      newPos = encoder->getPosition();
      if (newPos < ROTARYMIN) 
      {
          encoder->setPosition(ROTARYMIN);
          newPos = ROTARYMIN;
        } 
        else if (newPos > ROTARYMAX) 
        {
          encoder->setPosition(ROTARYMAX);
          newPos = ROTARYMAX;
        }
      factor = newPos * 0.333;
      if (pos != newPos)
        {
          F_correction();
          // tft.setCursor(10,5); tft.printf("corr: %+.2f f  ", factor);
          pos = newPos;
        } 
    } 
  }

  expoTime = Time * pow(2,factor);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_RED , TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextPadding(tft.textWidth("800", 7));
  tft.drawFloat(expoTime, 1, 318, 182, 7);
  
  if (Trigger) // start exposure when trigger button is pressed
  {
    Enlarger(1); dukaLight(0);
    int i = expoTime*10;
    while (i>0)
    {
      if(true)
      {
        MS100 = false;
        i--;
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.drawFloat(i/10.0, 1, 318, 182, 7);
        delay(100);
      }
     }

    Enlarger(0); dukaLight(redLight);
  }

}


void drawGradient()
{
  for (int x = 0; x <= 255; ++x )
  {
    unsigned char red = x >> 3;
    unsigned char green = x >> 2;
    unsigned char blue = x >> 3;
    // int result = (red << (5 + 6)) | (green << 5) | blue;  // white
    int result = (red << (5 + 6)) | (0 << 5) | 0;  // just red

    tft.drawFastVLine(255+32 - x, 80, 15, result);
  }
  tft.drawRect(31, 79, 256, 17, TFT_RED);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_RED);
  tft.drawString("SHDWS",32,100,2);
  tft.drawString("LIGHT",255+32,100,2);
}

void lowArrow(float low, int color)  // show Lux of shadows
{
  int posX = map(low,0,10,1,255);
  if(posX > 255) posX = 255; // constrain max value
  tft.setTextColor(color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.fillTriangle( 32+posX, 77, 25+posX, 65, 39+posX, 65, color);
  tft.drawFloat(low, 2, 32+posX, 55, 2);
}

void highArrow(float high, int color)  // show Lux of lights
{
  int posX = map(high,0,15,1,255);
  if(posX > 255) posX = 255; // constrain max value
  tft.setTextColor(color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.drawTriangle( 32+posX, 77, 25+posX, 65, 39+posX, 65, color);
  tft.drawFloat(high, 2, 32+posX, 55, 2);
}

void calcTime()  // calculate time from lo_value and paper grade from exposure range
{
if (hi < lo){}
else{std::swap(lo, hi);}
ratio = lo / hi;
calcGrade();
Time = Sens / lo;
tft.fillRect(4,121,80,118, TFT_BLACK);
tft.drawLine(0,160,90,160, TFT_RED);
tft.drawLine(0,200,90,200, TFT_RED);
tft.setTextColor(TFT_RED);
tft.setTextSize(1);
tft.setTextFont(2);
tft.setCursor(4, 122); tft.print("EXP-RATIO");
tft.setCursor(4, 161); tft.print("F-STOPS");
tft.setCursor(4, 201); tft.print("PAPER-GRADE");
tft.setTextFont(4);
tft.setCursor(4, 136); tft.print(ratio);
tft.setCursor(4, 176); tft.print(log2(ratio),1); // tft.print(" f");
tft.setCursor(4, 216); tft.print(grade);
}

void showSeconds()  // show seconds for timer
{
tft.fillRect(4,121,90,118, TFT_BLACK);
tft.drawLine(0,160,90,160, TFT_RED);
tft.setTextSize(1);
tft.setTextFont(4);
tft.setCursor(4, 130); tft.print("sec");
DukaTimer();
}

void LuxRead(void)  // reads light sensor and calculates Lux
{
  lum = TSL2591.getFullLuminosity();
  ir = lum >> 16;
  full = lum & 0xFFFF;
  lux = TSL2591.calculateLux(full, ir);
  // showBig(lux);
}

void calcGrade()  // calculates the recommended paper grade from EV ratio
{
  if (ratio < 2.5) { grade = "5 uhard"; Sens = paperSens+2;} // the +x operand reflects different sensitivity of the paper grade
  else if (ratio < 4  ) { grade = "4 hard"; Sens = paperSens+1;}
  else if (ratio < 8) { grade = "3 norm"; Sens = paperSens+0;}
  else if (ratio < 16) { grade = "2 norm"; Sens = paperSens+0;}
  else if (ratio < 25) { grade = "1 soft"; Sens = paperSens-1;}
  else if (ratio < 32) { grade = "0 ssoft"; Sens = paperSens-2;}
}

void dukaLight(bool on)  // set dukaLight output according to value
{
  if (on)
  {
    tft.setFreeFont(miniBold);
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(1);
    tft.fillRect(269, 0, 50, 16, TFT_RED);
    tft.setCursor(274, 12); tft.print("DUKA");
    digitalWrite(DUKA, HIGH);
  }
  else
  {
    tft.fillRect(269, 0, 50, 16, TFT_BLACK);
    digitalWrite(DUKA, LOW);
  }
}

void Enlarger(bool on) // set enlarger output according to value
{
  if (on)
  {
    tft.setFreeFont(miniBold);
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(1);
    tft.fillRect(269, 20, 50, 16, TFT_RED);
    tft.setCursor(274, 32); tft.print("ENLG");
    digitalWrite(ENL, HIGH);
  }
  else
  {
    tft.fillRect(269, 20, 50, 16, TFT_BLACK);
    digitalWrite(ENL, LOW);
  }
}

void Mode(bool timerMode)  // init mode according to mode-value 
{
  if (timerMode)
  {
    DukaTimer();
  }
  else
  {
    lightMeter();
  }
}

bool TimerHandler(struct repeating_timer *t) // IRQ: check all buttons
{
  Trigger = !digitalRead(SW1);
  EnlSW = !digitalRead(SW2);
  ModeSW = !digitalRead(SW3);
  DukaSW = !digitalRead(SW4);
  DevSW = !digitalRead(SW5);
  MeasureSW = !digitalRead(MEAS);
  Knob = !digitalRead(KNOB);
  // msCnt++;
  // if (msCnt == 2 | msCnt == 4 | msCnt == 6 | msCnt == 8)  {MS100 = true;}
  // if (msCnt == 20) {MS500 = true; MS100 = true; msCnt=0;}
  // digitalWrite(LED1, MS100); digitalWrite(LED2, MS500); 

  return true;
}

void configureTSL()  // sets light sensor sensitivity
{
  // You can change the gain on the fly, to adapt to brighter/dimmer light situations
  // TSL2591.setGain(TSL2591_GAIN_LOW);    // 1x gain (bright light)
  TSL2591.setGain(TSL2591_GAIN_MED); // 25x gain
  // TSL2591.setGain(TSL2591_GAIN_HIGH);   // 428x gain

  // Changing the integration time gives you a longer time over which to sense light
  // longer timelines are slower, but are good in very low light situtations!
  // TSL2591.setTiming(TSL2591_INTEGRATIONTIME_100MS);  // shortest integration time (bright light)
  // TSL2591.setTiming(TSL2591_INTEGRATIONTIME_200MS);
  // TSL2591.setTiming(TSL2591_INTEGRATIONTIME_300MS);
  // TSL2591.setTiming(TSL2591_INTEGRATIONTIME_400MS);
  TSL2591.setTiming(TSL2591_INTEGRATIONTIME_500MS);
  // TSL2591.setTiming(TSL2591_INTEGRATIONTIME_600MS);  // longest integration time (dim light)
}

void F_correction()  // shows f-correction factor
{
  tft.setFreeFont(miniBold);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect( 125, 16, 68, 24, TFT_BLACK);
  tft.fillRect( 125, 0, 70, 15, TFT_RED);
  tft.drawRect( 125, 0, 70, 40, TFT_RED);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(125+5, 12); tft.print("F-CORR");
  tft.setTextColor(TFT_RED);
  tft.setTextFont(4);
  tft.setCursor(125+5,17); tft.printf("%+.1f f", factor);
}

void DukaTimer()  // sets DukaTimer mode
{
  tft.setFreeFont(miniBold);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(1);
  // tft.fillRect( 1, 16, 98, 24, TFT_BLACK);
  tft.fillRect( 0, 0, 120, 45, TFT_BLACK);
  tft.fillRect( 0, 0, 120, 15, TFT_RED);
  tft.drawRect( 0, 0, 120, 40, TFT_RED);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(0+5, 12); tft.print("DUKATIMER");
  tft.setTextColor(TFT_RED);
  tft.setTextFont(4);
  tft.setCursor(0+5,17); tft.printf("%.1f s", factor);
}

void lightMeter()  // sets LightMeter mode
{
  tft.setFreeFont(miniBold);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect( 1, 16, 120, 45, TFT_BLACK);
  tft.fillRect( 0, 0, 120, 15, TFT_RED);
  tft.drawRect( 0, 0, 120, 40, TFT_RED);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(0+5, 12); tft.print("LIGHTMETER");
  tft.setTextColor(TFT_RED);
  tft.setTextFont(4);
  tft.setCursor(0+5,17); tft.printf("sns: %.1f", paperSens);
}

void click()  // short audible tick
{
  digitalWrite(BEEP, HIGH);
  delay(5);
  digitalWrite(BEEP, LOW);
}

void beep()  // audible beep
{
  digitalWrite(BEEP, HIGH);
  delay(250);
  digitalWrite(BEEP, LOW);
}

void showBig(float val)  // show val as big number
{
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_RED , TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextPadding(tft.textWidth("10000", 4));
  tft.drawFloat(val, 1, 260, 15, 4);
}

void checkPosition()  // encoder update
{
  encoder->tick(); // just call tick() to check the state.
}

void getLightValues()  // measure Hi and Lo
{
  Measurement(1);
  tft.fillRect(1, 44, 318, 35, TFT_BLACK);  // clear arrows
  LuxRead();
  click();
  lo = lux;
  Serial.println(lo);
  lowArrow(lo, TFT_RED);
  delay(250);

  while(!MeasureSW) { delay(100); }   // wait for 2nd button press

  // if (MeasureSW)  // triggered by switch in light-sensor
  {
    LuxRead();
    click();
    hi = lux;
    Serial.println(hi);
    highArrow(hi, TFT_RED);
    delay(100);
    Measurement(0);
  }
}

void Measurement(bool stat)  // show measure label
{
  tft.setFreeFont(miniBold);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(1);
  if (stat) tft.fillRect(217, 0, 50, 16, TFT_GREEN);
  else tft.fillRect(217, 0, 50, 16, TFT_BLACK);
  tft.setCursor(222, 12); tft.print("MEAS");
}
