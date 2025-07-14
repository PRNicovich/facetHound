/**
   @file Unvarnished_Transmission.ino
   @author rakwireless.com
   @brief unvarnished transmission via USB
   @version 0.1
   @date 2021-6-28
   @copyright Copyright (c) 2020
**/

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

bool updateTipAngle = true;
float tipAngle = 44.5;

bool updateZValue = true;
float zValue = 115.442;

bool updateRPMValue = true;
uint32_t RPMValue = 4568;

bool updateFlowRate = true;
float flowRate = 25.5;

bool updateForceBar = true;
uint32_t forceBar = 0;

void setup()
{
	time_t serial_timeout = millis();
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

}


void loop(){


	if (keysSerial.available()){
		
		handleKeyboardUART();

		}
}

void sendToDisplayUART(char* x){

	dispSerial.println(x);
	Serial.println(x);

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

	for (int i = 0; i < (sizeof(keyString) / sizeof(keyString[0])); i++){
		
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
				sendToDisplayUART("a");
				break;

			case 5:
				//Serial.print(" 5 ");
				sendToDisplayUART("b");
				break;

			case 6:
				//Serial.print(" 6 ");
				sendToDisplayUART("c");
				break;

			case 7:
				//Serial.print(" 7 ");
				sendToDisplayUART("d");
				break;

			case 8:
				//Serial.print(" 8 ");
				sendToDisplayUART("e");
				break;

			case 9:
				//Serial.print(" 9 ");
				sendToDisplayUART("f");
				break;

			case 10:
				//Serial.print(" 10 ");
				sendToDisplayUART("g");
				break;

			case 11:
				//Serial.print(" 11 ");
				sendToDisplayUART("h");
				break;

			case 12:
				//Serial.print(" 12 ");
				sendToDisplayUART("i");
				break;

			case 13:
				//Serial.print(" 13 ");
				sendToDisplayUART("j");
				break;

			case 14:
				//Serial.print(" 14 ");
				sendToDisplayUART("k");
				break;

			case 15:
			  //Serial.print(" 15 ");
				sendToDisplayUART("l");
				break;



			case 16:
		 		tiltAngle = tiltAngle + 1;
				
				dtostrf(tiltAngle, 1, 2, buff);
				sprintf(stringOut, "T %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 17:
		 		tiltAngle = 0;
				
				dtostrf(tiltAngle, 1, 2, buff);
				sprintf(stringOut, "T %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 18:
		 		tiltAngle = tiltAngle - 1;
				
				dtostrf(tiltAngle, 1, 2, buff);
				sprintf(stringOut, "T %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 30:
		 		flowRate = flowRate + 1;
				
				dtostrf(flowRate, 1, 2, buff);
				sprintf(stringOut, "F %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 31:
		 		flowRate = 0;
				
				dtostrf(flowRate, 1, 2, buff);
				sprintf(stringOut, "F %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 32:
		 		flowRate = flowRate - 1;
				
				dtostrf(flowRate, 1, 2, buff);
				sprintf(stringOut, "F %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 33:
		 		RPMValue = RPMValue + 1;
				
				dtostrf(RPMValue, 1, 2, buff);
				sprintf(stringOut, "R %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 34:
		 		RPMValue = 0;
				
				dtostrf(RPMValue, 1, 2, buff);
				sprintf(stringOut, "R %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 35:
		 		RPMValue = RPMValue - 1;
				
				dtostrf(RPMValue, 1, 2, buff);
				sprintf(stringOut, "R %s", buff);
				sendToDisplayUART(stringOut);
				break;

			case 36:

				zValue = zValue + 1;
				
				dtostrf(zValue, 1, 3, buff);
				sprintf(stringOut, "Z %s", buff);
				sendToDisplayUART(stringOut);

				break;

			case 37:

				zValue = 0;
				dtostrf(zValue, 1, 3, buff);
				sprintf(stringOut, "Z %s", buff);
				sendToDisplayUART(stringOut);

				break;

			case 38:

				zValue = zValue - 1;
				dtostrf(zValue, 1, 3, buff);
				sprintf(stringOut, "Z %s", buff);
				sendToDisplayUART(stringOut);

				break;


			default:
				Serial.print(keyString[i]);
				break;
		}

		// Serial.print('.');

		keyString[i] = 0;

	}

	//Serial.println();

	//Serial.println(keyString);


}