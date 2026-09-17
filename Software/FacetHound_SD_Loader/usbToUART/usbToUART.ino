#include <Adafruit_TinyUSB.h>
#include <pio_usb_configuration.h>
#include <pico/multicore.h>
#include <pico/util/queue.h>
#include <string.h>

// Dedicated USB-host keyboard module. The RP2040 PIO USB host uses adjacent
// pins: D+ on GPIO2 and D- on GPIO3.
constexpr uint8_t USB_HOST_DP_PIN = 2;

// Hardware UART to the base module. Connect module TX GPIO0 to base RX GPIO6,
// module RX GPIO1 to base TX GPIO7, and share ground. Logic is 3.3 V TTL.
constexpr uint8_t BASE_UART_TX_PIN = 0;
constexpr uint8_t BASE_UART_RX_PIN = 1;
constexpr uint32_t BASE_UART_BAUD = 115200;

constexpr uint32_t CORE_HEARTBEAT_MS = 1000;
constexpr uint32_t CORE_RESTART_MS = 3000;
constexpr uint8_t QUEUE_LENGTH = 16;

Adafruit_USBH_Host USBHost;

struct Heartbeat
{
  uint32_t value;
};

queue_t heartbeatQueue;
uint32_t lastCoreHeartbeatMs = 0;
uint32_t lastHeartbeatSentMs = 0;
hid_keyboard_report_t previousKeyboardReport = {};

static void usbHostCore();
static void restartUsbHostCore();

static bool reportContains(const hid_keyboard_report_t& report, uint8_t keycode)
{
  for (uint8_t i = 0; i < 6; ++i)
    if (report.keycode[i] == keycode) return true;
  return false;
}

static void sendKeyToBase(uint8_t keycode)
{
  // This is the exact framing consumed by baseChassisModule::keyboardTask().
  Serial1.print("D,");
  Serial1.println(keycode);
}

static void processKeyboardReport(const hid_keyboard_report_t& report)
{
  // Emit only transitions from released to pressed. HID reports include every
  // held key, so forwarding the entire report would repeat old keys whenever
  // another key is pressed or released.
  for (uint8_t i = 0; i < 6; ++i)
  {
    const uint8_t keycode = report.keycode[i];
    if (keycode && !reportContains(previousKeyboardReport, keycode))
      sendKeyToBase(keycode);
  }
  previousKeyboardReport = report;
}

static void sendCoreHeartbeat()
{
  const uint32_t now = millis();
  if (now - lastHeartbeatSentMs < CORE_HEARTBEAT_MS) return;
  lastHeartbeatSentMs = now;
  Heartbeat heartbeat = {0xDDu};
  queue_try_add(&heartbeatQueue, &heartbeat);
}

void setup()
{
  Serial1.setTX(BASE_UART_TX_PIN);
  Serial1.setRX(BASE_UART_RX_PIN);
  Serial1.begin(BASE_UART_BAUD);

  queue_init(&heartbeatQueue, sizeof(Heartbeat), QUEUE_LENGTH);
  lastCoreHeartbeatMs = millis();
  restartUsbHostCore();
}

void loop()
{
  Heartbeat heartbeat = {};
  while (queue_try_remove(&heartbeatQueue, &heartbeat))
    lastCoreHeartbeatMs = millis();

  if (millis() - lastCoreHeartbeatMs > CORE_RESTART_MS)
  {
    lastCoreHeartbeatMs = millis();
    restartUsbHostCore();
  }
}

static void usbHostCore()
{
  delay(5);

  pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
  config.pin_dp = USB_HOST_DP_PIN;
  USBHost.configure_pio_usb(1, &config);
  USBHost.begin(1);

  while (true)
  {
    USBHost.task();
    sendCoreHeartbeat();
  }
}

static void restartUsbHostCore()
{
  multicore_reset_core1();
  memset(&previousKeyboardReport, 0, sizeof(previousKeyboardReport));
  multicore_launch_core1(usbHostCore);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* descriptor, uint16_t descriptorLength)
{
  (void)descriptor;
  (void)descriptorLength;
  memset(&previousKeyboardReport, 0, sizeof(previousKeyboardReport));
  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  (void)dev_addr;
  (void)instance;
  memset(&previousKeyboardReport, 0, sizeof(previousKeyboardReport));
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t length)
{
  if (tuh_hid_interface_protocol(dev_addr, instance) ==
          HID_ITF_PROTOCOL_KEYBOARD &&
      length >= sizeof(hid_keyboard_report_t))
  {
    processKeyboardReport(
        *reinterpret_cast<hid_keyboard_report_t const*>(report));
  }

  // Arm the endpoint for the next interrupt report, including unsupported HID
  // interfaces so one mouse/gamepad report cannot stall the host task.
  tuh_hid_receive_report(dev_addr, instance);
}
