/*
* DroneHacker (c) 2015 Anibal Gomez
* ---------------------------------
* Software Controlador De Vuelo
* ---------------------------------
*
* Quadcoptero
* -----------
*
*             - Eje Y -
*
*                RX
*            TL      TR
*            |\     /|
*            ( \   / )
*             \ (_) / 
*              ) _ (   - Eje X -
*             / ( ) \ 
*            ( /   \ )
*            |/     \|
*            BL     BR
*       
*
* CONTRIBUCIONES
* ==============
* 
* - Si desea contribuir con el programa, por favor:
* - No comente ninguna linea de codigo
* - Envie su documentacion externa, en la carpeta, /docs/
* - Use estandares de convencion
* - Use variables lo mas descriptivas posibles
* - Use DocBlocks en la parte superior de cada metodo
*
* ---------------------------------
* anibalgomez@icloud.com
* ---------------------------------
*
* LICENSE
*
* This source file is subject to the new BSD license that is bundled
* with this package in the file LICENSE.txt.
*
**/

#include <PinChangeInt.h>
#include <Servo.h>
#include "I2Cdev.h"
#include <PID_v1.h>

double Setpoint, Input, Output;

int kp = 2;
int ki = 5;
int kd = 1;

PID myPID(&Input, &Output, &Setpoint, kp, ki, kd, DIRECT);

#define THROTTLE_IN_PIN 3
#define PITCH_IN_PIN 4

#define MOTORTL_OUT_PIN 8
#define MOTORTR_OUT_PIN 9
#define MOTORBR_OUT_PIN 11
#define MOTORBL_OUT_PIN 13

#include "MPU6050_6Axis_MotionApps20.h"
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

MPU6050 mpu;

bool dmpReady = false;
uint8_t mpuIntStatus;
uint8_t devStatus;
uint16_t packetSize;
uint16_t fifoCount;
uint8_t fifoBuffer[64];

Quaternion q;
VectorInt16 aa;
VectorInt16 aaReal;
VectorInt16 aaWorld;
VectorFloat gravity;
float euler[3];
float ypr[3];

int outputTL, outputTR, outputBR, outputBL, auxTL, auxTR, auxBR, auxBL;
int mpuYaw, mpuPitch, mpuRoll;

Servo servoMotorTL;
Servo servoMotorTR;
Servo servoMotorBR;
Servo servoMotorBL;

#define THROTTLE_FLAG 1
#define PITCH_FLAG 1

volatile uint8_t bUpdateFlagsShared;

volatile uint16_t unThrottleInShared;
volatile uint16_t unPitchInShared;

uint32_t ulThrottleStart;
uint32_t ulPitchStart;

volatile bool mpuInterrupt = false;
void dmpDataReady() {
    mpuInterrupt = true;
}

void setup(){
  
    #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
        Wire.begin();
        TWBR = 24;
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif
    
  Serial.begin(115200);
  Serial.println("multiChannels");

  servoMotorTL.attach(MOTORTL_OUT_PIN);
  servoMotorTR.attach(MOTORTR_OUT_PIN);
  servoMotorBL.attach(MOTORBL_OUT_PIN);
  servoMotorBR.attach(MOTORBR_OUT_PIN);

  PCintPort::attachInterrupt(THROTTLE_IN_PIN, calcThrottle, CHANGE); 
  PCintPort::attachInterrupt(PITCH_IN_PIN, calcPitch, CHANGE);
  
  arm();
  
  mpu.initialize();
  Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));
  devStatus = mpu.dmpInitialize();
  mpu.setXGyroOffset(220);
  mpu.setYGyroOffset(76);
  mpu.setZGyroOffset(-85);
  mpu.setZAccelOffset(1788);
  
  if (devStatus == 0) {
        mpu.setDMPEnabled(true);
        attachInterrupt(0, dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();
        dmpReady = true;
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }

  //Input = 0;
  //Setpoint = 0;
  myPID.SetMode(AUTOMATIC);
  //myPID.SetOutputLimits(0, 100);

}

void loop() {
  
  static uint16_t unThrottleIn;
  static uint16_t unPitchIn;

  static uint8_t bUpdateFlags;

  if (!dmpReady) return;
    while (!mpuInterrupt && fifoCount < packetSize) {
    }
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();
    fifoCount = mpu.getFIFOCount();
    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
        mpu.resetFIFO();
    } else if (mpuIntStatus & 0x02) {
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        fifoCount -= packetSize;
    }

  mpu.dmpGetQuaternion(&q, fifoBuffer);
  mpu.dmpGetGravity(&gravity, &q);
  mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
  
  mpuYaw = ypr[0] * 180/M_PI;
  mpuPitch = ypr[1] * 180/M_PI;
  mpuRoll = ypr[2] * 180/M_PI;
 

  if(bUpdateFlagsShared) {
    
    noInterrupts();
    bUpdateFlags = bUpdateFlagsShared;
    
    if(bUpdateFlags & THROTTLE_FLAG) {
      unThrottleIn = unThrottleInShared;
    }
    if(bUpdateFlags & PITCH_FLAG) {
      unPitchIn = unPitchInShared;
    }
    
    bUpdateFlagsShared = 0;
    interrupts();
    
  }
          
  if(bUpdateFlags & THROTTLE_FLAG) {
    if(servoMotorTL.readMicroseconds() && servoMotorTR.readMicroseconds() 
        && servoMotorBL.readMicroseconds() && servoMotorBR.readMicroseconds()
        != unThrottleIn) {
              // -- Stabilizer --
    Input = 3; // Not less than 3 or not equals to 0
    Setpoint = abs(mpuPitch);
    myPID.Compute();
        //Serial.println(Output);
        //Serial.println(Setpoint);
          //Serial.println(unThrottleIn);
          outputTR = unThrottleIn;
          outputTL = unThrottleIn;
          outputBL = unThrottleIn;
          outputBR = unThrottleIn;
          auxTR = unThrottleIn;
          auxTL = unThrottleIn;
          auxBL = unThrottleIn;
          auxBR = unThrottleIn;
          if(mpuPitch > 3) {
             outputTL = unThrottleIn + Output;
             outputBL = unThrottleIn + Output;
          }
          if(mpuPitch < -3) {
             outputTR = unThrottleIn + Output;
             outputBR = unThrottleIn + Output;
          }
    }
  }
  
  if(bUpdateFlags & PITCH_FLAG) {
    if(servoMotorTL.readMicroseconds() && servoMotorTR.readMicroseconds() 
        && servoMotorBL.readMicroseconds() && servoMotorBR.readMicroseconds()
        != unPitchIn) {
          //Serial.println(unPitchIn);
          if(unPitchIn > 1550) {
            Setpoint = map(unPitchIn, 1550, 2000, 0, 30);
            myPID.Compute();
            outputTL = auxTL + Output;
            outputBL = auxBL + Output;
          }
          if(unPitchIn < 1450) {
            Setpoint = map(unPitchIn, 1000, 1450, 0, 30);
            myPID.Compute();
            outputTR = auxTR + Output;
            outputBR = auxBR + Output;
          }    
          if(unPitchIn > 1450 || unPitchIn < 1550) {
              Setpoint = 0;
              myPID.Compute();
          }   
    }
  }
  
         /**   
  if(mpuPitch > 3 && unThrottleIn > 1060) {
    outputTL = auxTL + (mpuPitch + 50);
    outputBL = auxBL + (mpuPitch + 50);
  }
  
  if(mpuPitch < -3 && unThrottleIn > 1060) {
    outputTR = auxTR + (abs(mpuPitch) + 50);
    outputBR = auxBR + (abs(mpuPitch) + 50);
  }
  **/
    
    /**
  if(mpuPitch < 0) {
    Setpoint = abs(mpuPitch);
    myPID.Compute();
    outputTL = auxTL + Output;
    outputBL = auxBL + Output;
  }
  **/
  
    //outputTR = auxTR + Output;
    //outputBR = auxBR + Output;
  
  //Serial.println(Setpoint);
  //Serial.println(Output);
 
  
  initMotors(
            outputTL,
            outputTR,
            outputBR,
            outputBL
  );
  
  
  bUpdateFlags = 0;
  
}

void calcThrottle() {
  if(digitalRead(THROTTLE_IN_PIN) == HIGH) { 
    ulThrottleStart = micros();
  } else{
    unThrottleInShared = (uint16_t)(micros() - ulThrottleStart);
    bUpdateFlagsShared |= THROTTLE_FLAG;
  }
}

void calcPitch() {
  if(digitalRead(PITCH_IN_PIN) == HIGH) { 
    ulPitchStart = micros();
  } else{
    unPitchInShared = (uint16_t)(micros() - ulPitchStart);
    bUpdateFlagsShared |= PITCH_FLAG;
  }
}

void arm() {
  servoMotorTL.writeMicroseconds(1000);
  servoMotorTR.writeMicroseconds(1000);
  servoMotorBL.writeMicroseconds(1000);
  servoMotorBR.writeMicroseconds(1000);
}

void initMotors(int tl, int tr, int br, int bl) {
  
  Serial.println(tl);
  Serial.println(tr);
  Serial.println(br);
  Serial.println(bl);
  
  servoMotorTL.writeMicroseconds(tl);
  servoMotorTR.writeMicroseconds(tr);
  servoMotorBR.writeMicroseconds(br);
  servoMotorBL.writeMicroseconds(bl);
}
