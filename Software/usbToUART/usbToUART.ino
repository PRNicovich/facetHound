
#include "Adafruit_TinyUSB.h"
#include "pio_usb_configuration.h"
#include "pico/multicore.h"


// https://github.com/adafruit/Adafruit_TinyUSB_Arduino/issues/293#issuecomment-1666959735
// Optimizer settings to '-O'
// 240 MHz clock

#define PIN_USB_HOST_DP  2

#define KEY_HEART_DELAY 1000

Adafruit_USBH_Host USBHost;

uint lastTime = millis();
uint lastB = millis();

uint32_t receivedData = 0;
uint32_t p = 0;

bool resetFlag = false;
bool keySinceLastBeat = false;


queue_t queue;
const int QUEUE_LENGTH = 128;
typedef struct queueItem {
  uint32_t value;
} item;


//------------- Core0 -------------//
void setup() {

  queue_init(&queue, sizeof(item), QUEUE_LENGTH); //initialize the queue

  kickTheDog();

  
}


void loop() {

  queueItem temp;
  while (queue_try_remove(&queue, &temp)) { //retrieving item from the queue and deleting it from the queue
    //Serial.print("ITEM RETRIEVED:\t");
    //Serial.println(temp.value);
    lastTime = millis();
  }

  
  if ((millis() - lastTime) > (3*KEY_HEART_DELAY)){
    //Serial.println(millis() - lastTime);
    lastTime = millis();
    kickTheDog();

    //Serial.println("woof!");
  
    
  }
  


}


void core1Main(){

  delay(5);

  Serial1.begin(115200);
  while (!Serial1){
    delay(10);
  }

  delay(5);

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = PIN_USB_HOST_DP;
  USBHost.configure_pio_usb(1, &pio_cfg);

  USBHost.begin(1);

  while (true){
    USBHost.task();

    checkOnDog();

  }

}

void checkOnDog(){
  // If no keys have been pushed in last KEY_HEART_DELAY ms, 
  // send something into the fifo so other core knows this one is alive
  if ((millis() - lastB) > KEY_HEART_DELAY){
    
    lastB = millis();

      queueItem temp;
      temp.value = 0xDD;

      if (queue_try_add(&queue, &temp)) { //adding items to the queue
        //Serial.print("ADDING:\t");
        //Serial.println(temp.value);
      }

      else{
        //Serial.println("FIFO was full\n");

      
      }

  }
}


static void process_kbd_report(uint8_t dev_addr, hid_keyboard_report_t const *report)
{

  bool flush = false;


  for(uint8_t i=0; i<6; i++)
  {
    uint8_t keycode = report->keycode[i];
    if ( keycode )
    // Push any keystroke info that isn't a 0 byte
    {

      if (true){
        Serial1.print(keycode);
        Serial1.print(",");
        flush = true;

      }

    }

  }

  if (flush) 
  {
     // placeholder for 'end of message'
    Serial1.println();

  }
}


void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {

  if (!tuh_hid_receive_report(dev_addr, instance)) {
    //Serial.printf("Error: cannot request to receive report\r\n");
  }
}


// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {

      if ( tuh_hid_receive_report(dev_addr, instance) )
      {
        process_kbd_report(dev_addr, (hid_keyboard_report_t const*) report );
      }  
      else{

      }
    
}



void kickTheDog(){
  // Execute when other core stalls
  multicore_reset_core1();
  multicore_launch_core1(core1Main);
}

