#define KEYS_TX_PIN 6
#define KEYS_RX_PIN 7

#define DISP_TX_PIN 4
#define DISP_RX_PIN 5

SerialPIO keysSerial(KEYS_TX_PIN,KEYS_RX_PIN);
SerialPIO dispSerial(DISP_TX_PIN,DISP_RX_PIN);

uint8_t keyString[] = {0, 0, 0, 0, 0, 0, 0};
int k = 0;

bool lastValWasSep = true;
uint8_t lastVal = 0;

bool updateTiltAngle = true;
float tiltAngle = 0;
float tiltMult[] = {0.01, 0.1, 1.0};
int tiltIdx = 0;
bool tiltLock = false;
bool spinServoIdx = false;
bool cheatMode = false;

bool updateTipAngle = true;
float tipAngle = 44.5;

bool updateZValue = true;
float zValue = 115.442;
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


	Serial.flush();

}


void loop(){


	if (keysSerial.available()){
		
		handleKeyboardUART();

		}


}

void sendToDisplayUART(char* x){

	//dispSerial.println(x);
	Serial.println(x);
	Serial.flush();

}

void handleDisplayUART(){

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

void sendCharAndFloat(char* s, float f, int decimals){
	char buff[12];
	char stringOut[12];

	dtostrf(f, 1, decimals, buff);
	sprintf(stringOut, "%s %s", s, buff);
	sendToDisplayUART(stringOut);

}

void sendCharAndInt(char* s, int i){
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

	//Serial.println(zValue, 3);


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
	updateTiltLockOnScreen();
}

void toggleCheatMode(){
	cheatMode = not(cheatMode);
	updateCheatModeOnOffOnScreen();
}

