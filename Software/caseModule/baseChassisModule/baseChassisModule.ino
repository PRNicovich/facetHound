#include <TMC2209.h>
#include "FreqCountRP2.h"
#include "buttonFunctions.h"
#include <AccelStepper.h>
//#include "FastAccelStepper.h"

#define KEYS_TX_PIN 7
#define KEYS_RX_PIN 6

#define DISP_TX_PIN 5
#define DISP_RX_PIN 4

#define MAST_TX_PIN 3
#define MAST_RX_PIN 2



#define motorALMpin 8
#define motorPGpin 9
#define motorSVpin 10
#define motorBKpin 11
#define motorENpin 12
#define motorFRpin 13

#define FAN_PIN 27
#define LAMP_PIN 26


#define TWIST_EN_PIN 14
#define TWIST_TX_PIN 28
#define TWIST_DIR_PIN 0
#define TWIST_STEP_PIN 1

#define ZED_EN_PIN 18
#define ZED_TX_PIN 15
#define ZED_DIR_PIN 17
#define ZED_STEP_PIN 16

#define PUMP_EN_PIN 22
#define PUMP_TX_PIN 19
#define PUMP_DIR_PIN 21
#define PUMP_STEP_PIN 20


#define RPM_LIMIT_HIGH 500
#define RPM_LIMIT_LOW 0
#define motorMaxRPM 3500
#define motorNPoles 3

#define DISP_IN 0
#define MAST_IN 1


SerialPIO keysSerial(KEYS_TX_PIN, KEYS_RX_PIN);
SerialPIO dispSerial(DISP_TX_PIN, DISP_RX_PIN);
SerialPIO mastSerial(MAST_TX_PIN, MAST_RX_PIN);

#define TMC2209_SERIAL_BAUD_RATE 19200

SerialPIO twistSerial(TWIST_TX_PIN, SerialPIO::NOPIN);
SerialPIO pumpSerial(PUMP_TX_PIN, SerialPIO::NOPIN);
SerialPIO zedSerial(ZED_TX_PIN, SerialPIO::NOPIN);

// Instantiate TMC2209 class objects
TMC2209 twistDriver;
TMC2209 zedDriver;
TMC2209 pumpDriver;

// current values may need to be reduced to prevent overheating depending on
// specific motor and power supply voltage
uint32_t TWIST_RMS_CURRENT = 800;
int32_t TWIST_RUN_VELOCITY = 2000;
bool TWIST_DIR = false;
uint16_t TWIST_MICROSTEPS = 256;
float TWIST_MAX_SPEED = 200000.0;
float TWIST_ACCELERATION = 100000.0;
float TWIST_STEP_SPEED = 2500;

uint32_t ZED_RMS_CURRENT = 700;
int32_t ZED_RUN_VELOCITY = 100000;
bool ZED_DIR = false;
uint16_t ZED_MICROSTEPS = 128;
float ZED_MAX_SPEED = 40000.0;
float ZED_ACCELLERATION = 100000.0;
float ZED_STEP_SPEED = 1500;

uint32_t PUMP_RMS_CURRENT = 1100;
int32_t PUMP_RUN_VELOCITY = 1000;
bool PUMP_DIR = true;
uint16_t PUMP_MICROSTEPS = 4;


//FastAccelStepperEngine engine = FastAccelStepperEngine();
//FastAccelStepper* stepper = NULL;

AccelStepper twistDirStep(AccelStepper::DRIVER, TWIST_STEP_PIN, TWIST_DIR_PIN);
AccelStepper zedDirStep(AccelStepper::DRIVER, ZED_STEP_PIN, ZED_DIR_PIN);

uint8_t keyString[] = { 0, 0, 0, 0, 0, 0, 0 };
int k = 0;

bool lastValWasSep = true;
uint8_t lastVal = 0;

uint32_t lastQueryTime = 0;
uint8_t frameCycleTime = 20;  // milliseconds

uint32_t TILT_MOTOR_STEPS_PER_ROT = 200;
uint32_t TILT_STEPS_PER_ROT = TILT_MOTOR_STEPS_PER_ROT*TWIST_MICROSTEPS;


float wheelIndex = 96.0;
float tipEncoderSteps = 65535.0;
bool updateTiltAngle = true;
float tiltAngle = 0;
float targetTilt_float = 48.0;
uint32_t targetTilt = 0;
uint32_t tiltEncRaw = 0;
float tiltConversion = wheelIndex / tipEncoderSteps;
float tiltStepsPerIndexUnit = TILT_STEPS_PER_ROT/wheelIndex;
float tiltMult[] = { 1.0, 0.1, 0.005 };
int tiltIdx = 1;
bool tiltLock = true;
bool tiltDir = false;
float tiltZero = 0;
bool spinServoIdx = false;
float servoRelease = 120.0; // degrees
float servoCapture = 105.0; // degrees
bool cheatMode = false;
float tiltRecall = 0;
bool firstCrossServoThreshold = true;
float twistErrorProportionalTerm = 0.5;
float degreesAndDirection = 0;

float tiltAngleMemory[] = {0, 12, 24, 36, 48, 60, 72, 84};
int8_t tiltMemIdx = 4;
uint8_t tiltMemNPts = 8;

bool updateTipAngle = true;
float tipAngle = 0;
uint32_t tipEncRaw = 0;
float tipConversion = 360.0 / 131072.0;
long tipZero = 47800;

bool updateZValue = true;


uint32_t ZED_MOTOR_STEPS_PER_ROT = 100;
uint32_t ZED_STEPS_PER_ROT = ZED_MOTOR_STEPS_PER_ROT*ZED_MICROSTEPS;

bool zedDir = false;
float zValue = 0;
int32_t zEncSteps = 0;
float zedIndexInitsPerWheelTurn = 5e-3;
float zedGearRatio = 4;
float zedEncoderOversample = 4;
float zConversionFactor = zedIndexInitsPerWheelTurn / (zedGearRatio*zedEncoderOversample);
float zedStepsPerIndexUnit = ZED_MOTOR_STEPS_PER_ROT / zedIndexInitsPerWheelTurn;
float zMult[] = {int(zedStepsPerIndexUnit), int(0.05*zedStepsPerIndexUnit), int(0.0005*zedStepsPerIndexUnit)};
int zIdx = 0;
bool zLock = false;

bool updateRPMValue = true;
int16_t RPMValue = 0;
int8_t RPM_dir = 0;
bool motorAlarm = false;
bool motorOn = false;
bool motorDir = true;
unsigned int motorSpeedRaw = 0;
unsigned int motorSpeedSet = 0;
unsigned int motorSpeedCorr = 0;
unsigned int motorSpeedSetRaw = 0;
bool motorTrueForHardBrake = false;

int freqCountDuration = 500;  // ms, motor speed

bool updateFlowRate = true;
int16_t flowRate = 0;
int8_t flow_dir = -1;
int16_t flowRateMin = 0;
int16_t flowRateMax = 750;
float floatTicksTo_mLperMin = 0.052;
int16_t floatTicksToPUMPVELOCITY = 4;
float flowRateIn_mLperMin = 0;


bool updateForceBar = true;
float forceBar = 0;
uint32_t forceRaw = 0;
float forceConversion = 1e-1;

bool updateRPMbool = false;
bool updateZbool = false;
bool updateTiltbool = false;
bool updateTipbool = false;
bool updateForcebool = false;
bool updateFlowbool = true;

int reason = -1;

void setup() {



	initSteppers();

	delay(100);


	reason = rp2040.getResetReason();

	switch (reason){
		case 0:
			break;
		
		case 1:
			// Power switch restart
			// Need TMC2209s powered on before boot.
			rp2040.restart();

		case 2:
			break;

		case 3: 
			break;

		case 4:
			break;

		case 5:
			break;

		default :
			break;

	}


	Serial.begin(115200);


	keysSerial.begin(115200);
	while (!keysSerial) {
		delay(10);
	}
	keysSerial.flush();


	dispSerial.begin(115200);
	while (!dispSerial) {
		delay(10);
	}
	dispSerial.flush();	

	mastSerial.begin(115200);

	while (!mastSerial) {
		delay(10);
	}

	mastSerial.flush();

	Serial.flush();

	pinMode(FAN_PIN, OUTPUT);
	pinMode(LAMP_PIN, OUTPUT);


	initESCMotor();

  pinMode(TWIST_STEP_PIN, OUTPUT);
  pinMode(TWIST_DIR_PIN, OUTPUT);

	pinMode(ZED_STEP_PIN, OUTPUT);
  pinMode(ZED_DIR_PIN, OUTPUT);


	changeTiltAngle(true);

}



void loop() {


	if ((millis() - lastQueryTime) > frameCycleTime) {

		sendQuery();

		//float tiltError = targetTilt_float - tiltAngle;

		lastQueryTime = millis();
		if (abs((targetTilt_float - tiltAngle)) > 0) {


			degreesAndDirection = shortestArcPath(targetTilt_float, tiltAngle);
			long stepsToMoveHere = twistErrorProportionalTerm*(degreesAndDirection)*tiltStepsPerIndexUnit;
			
	
			if (twistDirStep.distanceToGo() == 0){
				twistDirStep.move(stepsToMoveHere);
			}

		}
	}


	if (keysSerial.available()) {

		handleKeyboardUART();
	}

	if (dispSerial.available()) {
		handleIncomingUART(DISP_IN);
	}

	if (mastSerial.available()) {
		handleIncomingUART(MAST_IN);
	}

	if (FreqCountRP2.available()) {
		unsigned long frequency = FreqCountRP2.read();
		// Do something with the frequency...

		motorSpeedCorr = frequency * motorNPoles;
		updateRPMbool = true;
	}

	if(twistDirStep.distanceToGo() != 0){
		twistDirStep.run();
	}



	if(zedDirStep.distanceToGo() != 0){
		zedDirStep.run();
	}
}

float shortestArcPath(float target, float current){

	float degAndDir = target - current;

	if (degAndDir > 0){
		if (abs(degAndDir) > (wheelIndex/2)){
			degAndDir = (degAndDir - wheelIndex);
		}
	}
	else{
		if (abs(degAndDir) > (wheelIndex/2)){
			degAndDir = (wheelIndex + degAndDir);
		}

		
	}
	return degAndDir;
}


void transmitFrame() {

	if (updateRPMbool) {
		updateRPMValueOnScreen();
		updateRPMbool = false;
	}

	if (updateZbool) {
		updateZValueOnScreen();
		updateZbool = false;
	}

	if (updateTiltbool) {
		updateTiltAngleOnScreen();
		updateTiltbool = false;
	}

	if (updateTipbool) {
		updateTipAngleOnScreen();
		updateTipbool = false;
	}

	if (updateForcebool) {
		updateForceValueOnScreen();
		updateForcebool = false;
	}

	if (updateFlowbool) {
		updateFlowRateOnScreen();
		updateFlowbool = false;
	}

}

void initSteppers() {
	// Motor start
	// Twist
	twistDriver.setup(twistSerial, TMC2209_SERIAL_BAUD_RATE);
	twistDriver.setHardwareEnablePin(TWIST_EN_PIN);
	twistDriver.setMicrostepsPerStep(TWIST_MICROSTEPS);
	twistDriver.setRMSCurrent(TWIST_RMS_CURRENT, 0.11);
	twistDriver.enableAutomaticCurrentScaling();
	twistDriver.enableCoolStep();
	twistDriver.setStandstillMode(TMC2209::NORMAL);
	twistDriver.enable();

	twistDriver.moveUsingStepDirInterface();

	// Z
	zedDriver.setup(zedSerial, TMC2209_SERIAL_BAUD_RATE);
	zedDriver.setHardwareEnablePin(ZED_EN_PIN);
	zedDriver.setMicrostepsPerStep(ZED_MICROSTEPS);
	zedDriver.setRMSCurrent(ZED_RMS_CURRENT, 0.11);
	zedDriver.enableAutomaticCurrentScaling();
	zedDriver.enableCoolStep();

	twistDriver.setStandstillMode(TMC2209::STRONG_BRAKING);
	zedDriver.enable();

	zedDriver.moveUsingStepDirInterface();

	//zedDriver.moveAtVelocity(ZED_RUN_VELOCITY);

	// Pump
	pumpDriver.setup(pumpSerial, TMC2209_SERIAL_BAUD_RATE);
	pumpDriver.setHardwareEnablePin(PUMP_EN_PIN);
	pumpDriver.setMicrostepsPerStep(PUMP_MICROSTEPS);
	pumpDriver.setRMSCurrent(PUMP_RMS_CURRENT, 0.11);
	pumpDriver.enableAutomaticCurrentScaling();
	pumpDriver.enableCoolStep();
	//pumpDriver.enable();

	if (PUMP_DIR) {
		pumpDriver.enableInverseMotorDirection();
	} else {
		pumpDriver.disableInverseMotorDirection();
	}
	pumpDriver.moveAtVelocity(0);

  twistDirStep.setMaxSpeed(TWIST_MAX_SPEED); //20000.0);
  twistDirStep.setAcceleration(TWIST_ACCELERATION); //40000.0);
  twistDirStep.setSpeed(TWIST_STEP_SPEED); // 200

	twistDirStep.moveTo(targetTilt);

	zedDirStep.setMaxSpeed(ZED_MAX_SPEED); //40000.0);
  zedDirStep.setAcceleration(ZED_ACCELLERATION); //100000.0);
  zedDirStep.setSpeed(ZED_STEP_SPEED); //400

	zedDirStep.move(1);



}


void initESCMotor(){

	pinMode(motorALMpin, INPUT);
	pinMode(motorPGpin, INPUT);
	pinMode(motorSVpin, OUTPUT);
	pinMode(motorBKpin, OUTPUT);
	pinMode(motorENpin, OUTPUT);
	pinMode(motorFRpin, OUTPUT);

	analogWrite(motorSVpin, 0);

	setMotorPins();

	FreqCountRP2.beginTimer(motorPGpin, freqCountDuration);

	changeMotorSpeed();

}

void handleIncomingUART(int inputStream) {

	// Read incoming stream from wherever
	// Parse, convert if needed
	// Update relevant value
	// Send to display

	String val;

	if (inputStream == DISP_IN) {
		// Input coming from display
		// Expect Z encoder values

		val = dispSerial.readStringUntil('\n');

	} else if (inputStream == MAST_IN) {
		// Input coming from mast
		// Expect Tip encoder, Tilt encoder, Force

		val = mastSerial.readStringUntil('\n');
		//Serial.println(val);
	}

	//Serial.println(val);

	char switchChar = val.charAt(0);


	switch (switchChar) {
		case ('w'):
			updateZEncoderSteps(val.substring(2));
			updateZbool = true;
			break;

		case ('l'):
			updateTiltEncoderSteps(val.substring(2));
			updateTiltbool = true;
			break;

		case ('i'):
			updateTipEncoderSteps(val.substring(2));

			if (spinServoIdx) {
				checkServoStatus();
			}

			updateTipbool = true;
			break;

		case ('e'):
			updateForceRead(val.substring(2));
			updateForcebool = true;
			break;

		case ('?'):
			//Serial.println("transmit");
			transmitFrame();
			break;

		default:
			break;
			//Serial.println(val);
	}
}

void updateTiltEncoderSteps(String subVal) {
	char chars[8];
	subVal.toCharArray(chars, subVal.length() + 1);
	tiltEncRaw = atof(chars);
	tEncStepsToTiltValue();
}

void tEncStepsToTiltValue() {
	tiltAngle = (float(tiltEncRaw) - tiltZero) * tiltConversion;
}

void updateForceRead(String subVal) {
	char chars[8];
	subVal.toCharArray(chars, subVal.length() + 1);
	forceRaw = atof(chars);
	fRawToForceValue();
	//Serial.println(forceRaw);
}

void fRawToForceValue() {
	forceBar = forceRaw * forceConversion;
}

void updateTipEncoderSteps(String subVal) {
	char chars[8];
	subVal.toCharArray(chars, subVal.length() + 1);
	tipEncRaw = atof(chars);
	tipEncStepsToTipValue();
}

void tipEncStepsToTipValue() {
	tipAngle = float(tipEncRaw - tipZero) * tipConversion;
}

void updateZEncoderSteps(String subVal) {
	char chars[8];
	subVal.toCharArray(chars, subVal.length() + 1);
	zEncSteps = atof(chars);
	zEncStepsToZValue();
}

void zEncStepsToZValue() {
	zValue = zEncSteps * zConversionFactor;
}

void sendToDisplayUART(char* x) {

	dispSerial.println(x);
	dispSerial.flush();

	//Serial.println(x);
	//Serial.flush();
}

void sendQuery() {
	dispSerial.println('?');
	dispSerial.flush();

	mastSerial.println('?');
	mastSerial.flush();
}

void handleKeyboardUART() {
	uint8_t val = keysSerial.read();

	if (val == 0x0A) {
		//End of transmission

		keyboardRouter(keyString);
		lastValWasSep = true;
		k = 0;

	} else if (val == 44) {
		//Serial.println('---');
		k++;
		lastValWasSep = true;
	} else if (val == 13) {
		// do nothing
		// carriage return
	} else {

		if (lastValWasSep) {

			keyString[k] = (val - 48);
			lastValWasSep = false;
		}

		else {

			keyString[k] = keyString[k] * 10 + (val - 48);
		}
	}
}



void keyboardRouter(uint8_t keyString[]) {

	for (int i = 0; i < 8; i++) {

		char buff[8];
		char stringOut[8];

		//Serial.println(*keyString);

		switch (keyString[i]) {

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
				//tiltAngle = tiltAngle + tiltMult[tiltIdx];
				tiltDir = true;
				//twistDirStep.move(-100);
				changeTiltAngle(false);
				//updateTiltAngleOnScreen();
				break;

			case 17:
				tiltIdx = tiltIdx + 1;
				if (tiltIdx >= 3) {
					tiltIdx = 0;
				}
				updateTiltIdxOnScreen();
				break;

			case 18:
				//tiltAngle = tiltAngle - tiltMult[tiltIdx];

				//twistDirStep.move(100);
				tiltDir = false;
				changeTiltAngle(false);
				//updateTiltAngleOnScreen();
				break;

			case 30:
				flowRate = flowRate + 1;
				changeFlowRate();
				//updateFlowRateOnScreen();
				break;

			case 31:

				chageFlowDirection();

				break;

			case 32:
				flowRate = flowRate - 1;
				changeFlowRate();
				//updateFlowRateOnScreen();
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

				//zValue = zValue + zMult[zIdx];
				zedDir = true;
				changeZMotorSteps();
				//zedDir = false;
				//Serial.println(zValue);
				//updateZValueOnScreen();
				break;

			case 37:
				// Change multiplier
				changeZMultiplier();

				break;

			case 38:
				//zValue = zValue - zMult[zIdx];
				zedDir = false;
				changeZMotorSteps();

				//Serial.println(zValue);
				//updateZValueOnScreen();
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




void sendCharAndFloat(const char* s, float f, int decimals) {
	char buff[12];
	char stringFloat[12];

	dtostrf(f, 1, decimals, buff);
	sprintf(stringFloat, "%s %s", s, buff);

	sendToDisplayUART(stringFloat);
}

void sendCharAndInt(const char* s, int i) {
	char stringOut[8];
	sprintf(stringOut, "%s %d", s, i);
	sendToDisplayUART(stringOut);
}

void checkServoStatus(){

	if (tipAngle > servoRelease){

		if (firstCrossServoThreshold){
			tiltLock = false;
			tiltRecall = tiltAngle;
			firstCrossServoThreshold = false;
			twistDriver.disable();
		}
	}
	
	else if (tipAngle < servoCapture){

			if (!firstCrossServoThreshold){
				tiltLock = true;
				twistDriver.enable();

				delay(10);

				degreesAndDirection = shortestArcPath(targetTilt_float, tiltAngle);
				long stepsToMoveHere = degreesAndDirection*tiltStepsPerIndexUnit;
				twistDirStep.move(stepsToMoveHere);

				//twistDirStep.moveTo(int(targetTilt_float*tiltStepsPerIndexUnit));// - (tiltEncRaw*51200)/65535);

				firstCrossServoThreshold = true;

			}
			//Serial.println(tiltEncRaw*51200/65535);

		}
}



void changeTiltAngle(bool directSet) {

	if (tiltLock){

		if (not(directSet)) {

			if (tiltDir){
				targetTilt_float = targetTilt_float + tiltMult[tiltIdx];
			}
			else{
				targetTilt_float = targetTilt_float - tiltMult[tiltIdx];
			}
		}

		degreesAndDirection = shortestArcPath(targetTilt_float, tiltAngle);
		long stepsToMoveHere = degreesAndDirection*tiltStepsPerIndexUnit;

		twistDirStep.move(stepsToMoveHere);
	}
}

void chageFlowDirection() {
	// Change DIR pin on TMC2208 to flow pump
	// Handle pause + lock-out logic
	// Handle update to screen commands

	flow_dir = flow_dir + 1;
	if (flow_dir >= 4) {
		flow_dir = 0;
	}

	if (flow_dir == 0) {
		pumpDriver.enable();
		pumpDriver.enableInverseMotorDirection();
		//Serial.println("pump go up");

	} else if (flow_dir == 1) {
		pumpDriver.disable();
		pumpDriver.enableInverseMotorDirection();
		//Serial.println("pump stop up");

	} else if (flow_dir == 2) {
		pumpDriver.enable();
		pumpDriver.disableInverseMotorDirection();
		//Serial.println("pump go down");

	} else if (flow_dir == 3) {
		pumpDriver.disable();
		pumpDriver.disableInverseMotorDirection();
		//Serial.println("pump stop up");
	}


	updateFlowIdxOnScreen();
}

void changeFlowRate() {

	if (flowRate >= flowRateMax) {
		flowRate = flowRateMax;
	} else if (flowRate <= flowRateMin) {
		flowRate = flowRateMin;
	}

	flowRateIn_mLperMin = float(flowRate) * floatTicksTo_mLperMin;

	pumpDriver.moveAtVelocity(flowRate*floatTicksToPUMPVELOCITY);

	// Change speed command to TMC for water pump
	updateFlowbool = true;
}

void changeMotorSpeed() {

	if (RPMValue > RPM_LIMIT_HIGH){
		RPMValue = RPM_LIMIT_HIGH;
	}

	if (RPMValue < RPM_LIMIT_LOW){
		RPMValue = RPM_LIMIT_LOW;
	}

	// Command change in ESC motor via PWM
	analogWrite(motorSVpin, RPMValue);
	Serial.println(RPMValue);

}

void setMotorPins(){

	digitalWrite(motorENpin, motorOn);
	digitalWrite(motorFRpin, motorDir);
	digitalWrite(motorBKpin, motorTrueForHardBrake);
}

void changeMotorDirection() {
	// Update ESC motor direction
	// Include logic for pause + lock/unlock

	RPM_dir = RPM_dir + 1;

	switch (RPM_dir) {
		case 1 : 
			motorOn = true;
			motorDir = false;
			break;
		
		case 2 :
			motorOn = false;
			motorDir = true;
			break;

		case 3 :
			motorOn = true;
			motorDir = true;
			break;

		case 4 :
			motorOn = false;
			motorDir = false;
			RPM_dir = 0;
			break;
				
		}

	setMotorPins();


	Serial.println(RPM_dir);
	updateRPMIdxOnScreen();
}

void updateESCLockOnScreen() {

	
	// Toggle icons for lock/unlock of ESC motor direction w/ encoder push
}

void changeZMotorSteps() {

	if (zLock) {

		int zedMove;

		if (zedDir){
			//Down
			zedMove = zMult[zIdx];
		}
		else{
			//Up
			zedMove = -zMult[zIdx];
		}
		zedDirStep.move(zedMove);
	}
}


void changeZMultiplier() {

	zIdx = zIdx + 1;

	if (zIdx >= 3) {
		zIdx = 0;
	}
	updateZIdxOnScreen();
}

void updateFlowIdxOnScreen() {
	sendCharAndInt("f", flow_dir);
}

void updateFlowRateOnScreen() {
	sendCharAndFloat("F", flowRateIn_mLperMin, 2);
}

void updateRPMIdxOnScreen() {
	
	sendCharAndInt("r", RPM_dir);
}

void updateRPMValueOnScreen() {
	sendCharAndInt("R", motorSpeedCorr);
}



void updateTiltAngleOnScreen() {
	sendCharAndFloat("T", tiltAngle, 2);
}

void updateTiltIdxOnScreen() {
	sendCharAndInt("t", tiltIdx);
}

void updateTipAngleOnScreen() {
	sendCharAndFloat("P", tipAngle, 2);
}

void updateForceValueOnScreen() {
	sendCharAndFloat("N", forceBar, 1);
}

void updateZValueOnScreen() {
	sendCharAndFloat("Z", zValue, 3);
}


void updateZIdxOnScreen() {
	sendCharAndInt("z", zIdx);
}

void updateSpinServoOnScreen() {
	firstCrossServoThreshold = true;
	sendCharAndInt("s", spinServoIdx);
}

void updateZLockOnScreen() {
	sendCharAndInt("a", zLock);
}

void updateTiltLockOnScreen() {
	sendCharAndInt("b", tiltLock);
}

void updateCheatModeOnOffOnScreen() {
	sendCharAndInt("g", cheatMode);
}

void updateMarkPointInfoOnScreen() {
	char stringOut[8];
	sprintf(stringOut, "M %d", tiltMemIdx);
	// Send info to display about which mark point info to show
	//Serial.println(stringOut);
}

void settingsMode() {
	//Serial.println("Settings Mode!");
}

void zeroTilt() {
	// Zero tilt angle
	tiltAngle = 0;
	updateTiltAngleOnScreen();
}

void toggleZLock() {
	zLock = not(zLock);
	// Set ENABLE pin on Z motor driver to lock/unlock free spin

	if (zLock) {
		zedDriver.enable();
		//zedDriver.moveAtVelocity(ZED_RUN_VELOCITY);
	} else {
		zedDriver.disable();
	}


	updateZLockOnScreen();
}

void undeclaredFunction() {
	//Serial.println("Key unassigned");
}

void addPositionToList() {
	//Serial.println("Add position");
	tiltMemNPts = tiltMemNPts + 1;
	updateMarkPointInfoOnScreen();
}

void spinServoModeToggle() {
	spinServoIdx = not(spinServoIdx);
	updateSpinServoOnScreen();
}





void changeMarkPointIndex(bool dir) {
	if (dir) {
		tiltMemIdx = tiltMemIdx + 1;
	} else {
		tiltMemIdx = tiltMemIdx - 1;
	}

	

	if (tiltMemIdx < 0) {
		if (tiltMemNPts == 0) {
			tiltMemIdx = 0;
		} else {
			tiltMemIdx = (tiltMemNPts - 1);
		}
	} else if (tiltMemIdx == tiltMemNPts) {
		tiltMemIdx = 0;

	}
	
	targetTilt_float = tiltAngleMemory[tiltMemIdx];

	changeTiltAngle(true);

	updateMarkPointInfoOnScreen();
}

void toggleTiltLock() {
	tiltLock = not(tiltLock);

	// Set ENABLE pin on Z motor driver to lock/unlock free spin
	if (tiltLock) {
		targetTilt_float = tiltAngle;
		twistDriver.enable();

	} else {
		twistDriver.disable();
	}
	updateTiltLockOnScreen();
}

void toggleCheatMode() {
	cheatMode = not(cheatMode);
	updateCheatModeOnOffOnScreen();
}
