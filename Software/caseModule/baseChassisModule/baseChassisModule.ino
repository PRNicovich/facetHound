#include <TMC2209.h>
#include "FreqCountRP2.h"

#define KEYS_TX_PIN 7
#define KEYS_RX_PIN 6

#define DISP_TX_PIN 5
#define DISP_RX_PIN 4

#define MAST_TX_PIN 3
#define MAST_RX_PIN 2

#define DISP_IN 0
#define MAST_IN 1

SerialPIO keysSerial(KEYS_TX_PIN,KEYS_RX_PIN);
SerialPIO dispSerial(DISP_TX_PIN,DISP_RX_PIN);
SerialPIO mastSerial(MAST_TX_PIN, MAST_RX_PIN);




/*
#define motorALMpin 8
#define motorPGpin 27
#define motorSVpin 6
#define motorBKpin 14
#define motorENpin 15
#define motorFRpin 26

#define motorMaxRPM 3500
#define motorNPoles 3




#define FAN_PIN 28
#define LAMP_PIN 29

uint8_t FAN_SPEED = 50;


*/

#define TMC2209_SERIAL_BAUD_RATE 19200
#define TWIST_EN_PIN 18
#define TWIST_TX_PIN 15
#define TWIST_DIR_PIN 17
#define TWIST_STEP_PIN 16

#define ZED_EN_PIN 14
#define ZED_TX_PIN 28
#define ZED_DIR_PIN 0
#define ZED_STEP_PIN 1

#define PUMP_EN_PIN 22
#define PUMP_TX_PIN 19
#define PUMP_DIR_PIN 21
#define PUMP_STEP_PIN 20

SerialPIO twistSerial(TWIST_TX_PIN, SerialPIO::NOPIN);
SerialPIO pumpSerial(PUMP_TX_PIN, SerialPIO::NOPIN);
SerialPIO zedSerial(ZED_TX_PIN, SerialPIO::NOPIN);

// Instantiate TMC2209 class objects
TMC2209 twistDriver;
TMC2209 zedDriver;
TMC2209 pumpDriver;



// current values may need to be reduced to prevent overheating depending on
// specific motor and power supply voltage
const uint8_t TWIST_RMS_CURRENT = 1500;
const int32_t TWIST_RUN_VELOCITY = 200000;

const uint8_t ZED_RMS_CURRENT = 1500;
const int32_t ZED_RUN_VELOCITY = 100000;

const uint8_t PUMP_RMS_CURRENT = 1500;
const int32_t PUMP_RUN_VELOCITY = 1000;


uint8_t keyString[] = {0, 0, 0, 0, 0, 0, 0};
int k = 0;

bool lastValWasSep = true;
uint8_t lastVal = 0;

bool updateTiltAngle = true;
float tiltAngle = 0;
uint32_t tiltEncRaw = 0;
float tiltConversion = 360.0/16384.0;
float tiltMult[] = {0.01, 0.1, 1.0};
int tiltIdx = 1;
bool tiltLock = true;
bool tiltDir = false;
bool spinServoIdx = false;
bool cheatMode = false;

bool updateTipAngle = true;
float tipAngle = 44.5;
uint32_t tipEncRaw = 0;
float tipConversion = 360.0/16384.0;

bool updateZValue = true;
float zValue = 0;
int32_t zEncSteps = 0;
float zConversionFactor = 5e-3/16;
float zMult[] = {0.001, 0.010, 0.100};
int zIdx = 0;
bool zLock = false;

bool updateRPMValue = true;
float RPMValue = 0;
int8_t RPM_dir = 0;

bool updateFlowRate = true;
float flowRate = 0.0;
int8_t flow_dir = 0;
float flowRateMin = 0.0;
float flowRateMax = 64.00;

bool updateForceBar = true;
float forceBar = 0;
uint32_t forceRaw = 0;
float forceConversion = 1e-3;

int markPointIdx = 0;
int nMarkPoints = 0;

void setup(){

	Serial.begin(115200);

	keysSerial.begin(115200);
	while (!keysSerial){
		delay(10);
	}
	keysSerial.flush();


	dispSerial.begin(115200);
	while (!dispSerial){
		delay(10);
	}
	dispSerial.flush();

	mastSerial.begin(115200);
	
	while (!mastSerial){
		delay(10);
	}
	
	mastSerial.flush();


	Serial.flush();


	initSteppers();


}



void loop(){


	if (keysSerial.available()){
		
		handleKeyboardUART();

		}

	if (dispSerial.available()){
		handleIncomingUART(DISP_IN);
	}

	if (mastSerial.available()){
		handleIncomingUART(MAST_IN);
	}


}

void initSteppers(){
	// Motor start
	// Twist
	twistDriver.setup(twistSerial, TMC2209_SERIAL_BAUD_RATE);
  twistDriver.setHardwareEnablePin(TWIST_EN_PIN);
  twistDriver.setMicrostepsPerStep(256);
  twistDriver.setRMSCurrent(TWIST_RMS_CURRENT, 0.11);
  twistDriver.enableAutomaticCurrentScaling();
  twistDriver.enableCoolStep();
  //twistDriver.enable();
  
	pinMode(TWIST_STEP_PIN, OUTPUT);
	pinMode(TWIST_DIR_PIN, OUTPUT);


	// Z
	zedDriver.setup(zedSerial, TMC2209_SERIAL_BAUD_RATE);
  zedDriver.setHardwareEnablePin(ZED_EN_PIN);
  zedDriver.setMicrostepsPerStep(128);
  zedDriver.setRMSCurrent(ZED_RMS_CURRENT, 0.11);
  zedDriver.enableAutomaticCurrentScaling();
  zedDriver.enableCoolStep();
  //zedDriver.enable();
  //zedDriver.moveAtVelocity(ZED_RUN_VELOCITY);

	// Pump
	pumpDriver.setup(pumpSerial, TMC2209_SERIAL_BAUD_RATE);
  pumpDriver.setHardwareEnablePin(PUMP_EN_PIN);
  pumpDriver.setMicrostepsPerStep(32);
  pumpDriver.setRMSCurrent(PUMP_RMS_CURRENT, 0.11);
  pumpDriver.enableAutomaticCurrentScaling();
  pumpDriver.enableCoolStep();
  pumpDriver.enable();
  pumpDriver.moveAtVelocity(PUMP_RUN_VELOCITY);
}


void handleIncomingUART(int inputStream){
	
	// Read incoming stream from wherever
	// Parse, convert if needed
	// Update relevant value
	// Send to display

	String val;

	if (inputStream == DISP_IN){
		// Input coming from display
		// Expect Z encoder values

		val = dispSerial.readStringUntil('\n');		

	}
	else if (inputStream == MAST_IN){
		// Input coming from mast
		// Expect Tip encoder, Tilt encoder, Force

		val = mastSerial.readStringUntil('\n');
		//Serial.println(val);

	}

	char switchChar = val.charAt(0);


	switch (switchChar){
		case ('w') :
			updateZEncoderSteps(val.substring(2));
			break;
		
		case ('l') :
			updateTiltEncoderSteps(val.substring(2));
			break;

		case ('i') :
			updateTipEncoderSteps(val.substring(2));
			break;
		
		case ('e') :
			updateForceRead(val.substring(2));
			break;

		default:
			Serial.println(val);
	}
}

void updateTiltEncoderSteps(String subVal){
	char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
	tiltEncRaw = atof(chars);
	tEncStepsToTiltValue();
  updateTiltAngleOnScreen();
}

void tEncStepsToTiltValue(){
	tiltAngle = float(tiltEncRaw)  * tiltConversion;
}

void updateForceRead(String subVal){
	char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
	forceRaw = atof(chars);
	fRawToForceValue();
  updateForceValueOnScreen();
}

void fRawToForceValue(){
	forceBar = forceRaw * forceConversion;
}

void updateTipEncoderSteps(String subVal){
	char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
	tipEncRaw = atof(chars);
	tipEncStepsToTipValue();
  updateTipAngleOnScreen();
}

void tipEncStepsToTipValue(){
	tipAngle = float(tipEncRaw) * tipConversion;
}

void updateZEncoderSteps(String subVal){
	char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
	zEncSteps = atof(chars);
	zEncStepsToZValue();
  updateZValueOnScreen();
}

void zEncStepsToZValue(){
	zValue = zEncSteps * zConversionFactor;
}

void sendToDisplayUART(char* x){

	dispSerial.println(x);
	dispSerial.flush();

	Serial.println(x);
	Serial.flush();

}

void handleKeyboardUART(){
		uint8_t val = keysSerial.read();
		
		if (val == 0x0A){
			//End of transmission
			
			keyboardRouter(keyString);
			lastValWasSep = true;
			k = 0;

		}
		else if (val == 44){
			//Serial.println('---');
			k++;
			lastValWasSep = true;
		}
		else if (val == 13){
			// do nothing
			// carriage return
		}
		else{

			if (lastValWasSep){

				keyString[k] = (val-48);
				lastValWasSep = false;
			}

			else{

				keyString[k] = keyString[k]*10 + (val - 48);

			}
		}
}



void keyboardRouter(uint8_t keyString[]){

	for (int i = 0; i < 8; i++){
		
		char buff[8];
		char stringOut[8];

		//Serial.println(*keyString);

		switch (keyString[i])
		{

			case 0:
				// skip
				break;

			case 1:
				break;
			
			case 2:
				break;

			case 3:
				break;

			case 4:
				//Serial.print(" 4 ");
				settingsMode();
				break;

			case 5:
				//Serial.print(" 5 ");
				zeroTilt();
				break;

			case 6:
				//Serial.print(" 6 ");
				undeclaredFunction();
				break;

			case 7:
				//Serial.print(" 7 ");
				toggleZLock();
				break;

			case 8:
				//Serial.print(" 8 ");
				addPositionToList();
				break;

			case 9:
				//Serial.print(" 9 ");
				undeclaredFunction();
				break;

			case 10:
				//Serial.print(" 10 ");
				undeclaredFunction();
				break;

			case 11:
				//Serial.print(" 11 ");
				spinServoModeToggle();
				break;

			case 12:
				//Serial.print(" 12 ");
				changeMarkPointIndex(false);
				break;

			case 13:
				//Serial.print(" 13 ");
				changeMarkPointIndex(true);
				break;

			case 14:
				//Serial.print(" 14 ");
				toggleCheatMode();
				break;

			case 15:
			  //Serial.print(" 15 ");
				toggleTiltLock();
				break;



			case 16:
		 		tiltAngle = tiltAngle + tiltMult[tiltIdx];
				tiltDir = true;
				changeTiltAngle();
				updateTiltAngleOnScreen();
				break;

			case 17:
		 		tiltIdx = tiltIdx + 1;
				if (tiltIdx >= 3){
					tiltIdx = 0;
				}
				updateTiltIdxOnScreen();
				break;

			case 18:
		 		tiltAngle = tiltAngle - tiltMult[tiltIdx];
				tiltDir = false;
				changeTiltAngle();
				updateTiltAngleOnScreen();
				break;

			case 30:
				flowRate = flowRate + 1;
				changeFlowRate();
				updateFlowRateOnScreen();
				break;

			case 31:

				chageFlowDirection();
				
				break;

			case 32:
				flowRate = flowRate - 1;
				changeFlowRate();
				updateFlowRateOnScreen();
				break;

			case 33:
		 		RPMValue = RPMValue + 1;
				changeMotorSpeed();
				updateRPMValueOnScreen();
				break;

			case 34:

				changeMotorDirection();
				
				break;

			case 35:
		 		RPMValue = RPMValue - 1;
				changeMotorSpeed();
				updateRPMValueOnScreen();
				break;

			case 36:

				zValue = zValue + zMult[zIdx];
				//changeZMotorSteps();
				//Serial.println(zValue);
				updateZValueOnScreen();
				break;

			case 37:
				// Change multiplier
				changeZMultiplier();
				
				break;

			case 38:
				zValue = zValue - zMult[zIdx];
				//changeZMotorSteps();
				//Serial.println(zValue);
				updateZValueOnScreen();
				break;


			default:
				//Serial.print(keyString[i]);
				//Serial.println("unidentified character!");
				break;
		}

		// Serial.print('.');

		keyString[i] = 0;

	}

	//Serial.println();

	//Serial.println(keyString);


}


void changeTiltAngle(){
	// Do something to move the tilt angle

	digitalWrite(TWIST_DIR_PIN, tiltDir);

}

void chageFlowDirection(){
	// Change DIR pin on TMC2208 to flow pump
	// Handle pause + lock-out logic 
	// Handle update to screen commands

	flow_dir = flow_dir + 1;
		if (flow_dir >= 3){
			flow_dir = 0;
		}
	updateFlowIdxOnScreen();
}

void changeFlowRate(){

	if (flowRate >= flowRateMax){
			flowRate = flowRateMax;
		}
	else if (flowRate <= flowRateMin){
			flowRate = flowRateMin;					
		}

	// Change speed command to TMC for water pump
}

void changeMotorSpeed(){
	// Command change in ESC motor via PWM
}

void changeMotorDirection(){
	// Update ESC motor direction
	// Include logic for pause + lock/unlock

		 		RPM_dir = RPM_dir + 1;
				if (RPM_dir >= 3){
					RPM_dir = 0;
				}

	updateRPMIdxOnScreen();
}

void updateESCLockOnScreen(){
	// Toggle icons for lock/unlock of ESC motor direction w/ encoder push

}

void changeZMotorSteps(){
	// Command Z Movement
	//Serial.println("Move Z!");
}


void changeZMultiplier(){

	zIdx = zIdx + 1;

	if (zIdx >= 3){
		zIdx = 0;
	}
	updateZIdxOnScreen();
}

void sendCharAndFloat(const char* s, float f, int decimals){
	char buff[12];
	char stringFloat[12];

	dtostrf(f, 1, decimals, buff);
	sprintf(stringFloat, "%s %s", s, buff);

	sendToDisplayUART(stringFloat);

}

void sendCharAndInt(const char* s, int i){
	char stringOut[8];
	sprintf(stringOut, "%s %d", s, i);
	sendToDisplayUART(stringOut);
}

void updateFlowIdxOnScreen(){
	sendCharAndInt("f", flow_dir);
}

void updateRPMIdxOnScreen(){
	sendCharAndInt("r", RPM_dir);
}

void updateRPMValueOnScreen(){
	sendCharAndFloat("R", RPMValue, 1);
}

void updateFlowRateOnScreen(){
	sendCharAndFloat("F", flowRate, 2);
}

void updateTiltAngleOnScreen(){
	sendCharAndFloat("T", tiltAngle, 2);
}

void updateTiltIdxOnScreen(){
	sendCharAndInt("t", tiltIdx);
}

void updateTipAngleOnScreen(){
	sendCharAndFloat("P", tipAngle, 2);
}

void updateForceValueOnScreen(){
	sendCharAndFloat("N", forceBar, 1);
}

void updateZValueOnScreen(){
	sendCharAndFloat("Z", zValue, 3);
}


void updateZIdxOnScreen(){
	sendCharAndInt("z", zIdx);
}

void updateSpinServoOnScreen(){
	sendCharAndInt("s", spinServoIdx);
}

void updateZLockOnScreen(){
	sendCharAndInt("a", zLock);
}

void updateTiltLockOnScreen(){
	sendCharAndInt("b", tiltLock);
}

void updateCheatModeOnOffOnScreen(){
	sendCharAndInt("g", cheatMode);
}

void updateMarkPointInfoOnScreen(){
	char stringOut[8];
	sprintf(stringOut, "M %d", markPointIdx);
	// Send info to display about which mark point info to show
	Serial.println(stringOut);
	
}

void settingsMode(){
	Serial.println("Settings Mode!");
}

void zeroTilt(){
	// Zero tilt angle
	tiltAngle = 0;
	updateTiltAngleOnScreen();
}

void toggleZLock(){
	zLock = not(zLock);
	// Set ENABLE pin on Z motor driver to lock/unlock free spin

	if (zLock){
		zedDriver.enable();
		zedDriver.moveAtVelocity(ZED_RUN_VELOCITY);
	}
	else{
		zedDriver.disable();
	}


	updateZLockOnScreen();
}

void undeclaredFunction(){
	Serial.println("Key unassigned");
}

void addPositionToList(){
	Serial.println("Add position");
	nMarkPoints = nMarkPoints + 1;
	updateMarkPointInfoOnScreen();
}

void spinServoModeToggle(){
	spinServoIdx = not(spinServoIdx);
	updateSpinServoOnScreen();
}

void changeMarkPointIndex(bool dir){
	if (dir){
		markPointIdx = markPointIdx + 1; 
	}
	else{
		markPointIdx = markPointIdx - 1;
	}
	if (markPointIdx < 0){
		if (nMarkPoints == 0){
			markPointIdx = 0;
		}
		else{
			markPointIdx = (nMarkPoints-1);
		}
	}
	else if (markPointIdx >= nMarkPoints){
		markPointIdx = 0;
	}

	updateMarkPointInfoOnScreen();

}

void toggleTiltLock(){
	tiltLock = not(tiltLock);

	// Set ENABLE pin on Z motor driver to lock/unlock free spin
	if (tiltLock){
		twistDriver.enable();
		twistDriver.moveAtVelocity(TWIST_RUN_VELOCITY);
	}
	else{
		twistDriver.disable();
	}
	updateTiltLockOnScreen();
}

void toggleCheatMode(){
	cheatMode = not(cheatMode);
	updateCheatModeOnOffOnScreen();
}

