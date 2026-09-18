#include <Arduino.h>
#include <RunningAverage.h>
#include <SerialPIO.h>
#include <math.h>

// Mast sensors.
constexpr uint8_t FORCE_PIN = 28;
constexpr uint8_t TIP_CS_PIN = 14;
constexpr uint8_t TIP_DATA_PIN = 12;
constexpr uint8_t TIP_CLOCK_PIN = 13;
constexpr uint8_t TWIST_CS_PIN = 15;
constexpr uint8_t TWIST_DATA_PIN = 7;
constexpr uint8_t TWIST_CLOCK_PIN = 6;

// Dedicated 3.3 V UART to the base module. Cross TX/RX and share ground:
// mast TX GPIO8 -> base RX GPIO2, mast RX GPIO9 <- base TX GPIO3.
constexpr uint8_t BASE_TX_PIN = 8;
constexpr uint8_t BASE_RX_PIN = 9;
constexpr uint32_t BASE_BAUD = 115200;

constexpr uint8_t TWIST_AVERAGE_SAMPLES = 16;
constexpr uint8_t TIP_AVERAGE_SAMPLES = 8;
constexpr uint8_t FORCE_AVERAGE_SAMPLES = 32;
constexpr float TWIST_ENCODER_COUNTS = 4096.0f;
constexpr uint32_t TELEMETRY_PERIOD_MS = 20; // 50 complete frames per second.

SerialPIO baseSerial(BASE_TX_PIN, BASE_RX_PIN);

RunningAverage twistCosAverage(TWIST_AVERAGE_SAMPLES);
RunningAverage twistSinAverage(TWIST_AVERAGE_SAMPLES);
RunningAverage tipAverage(TIP_AVERAGE_SAMPLES);
RunningAverage forceAverage(FORCE_AVERAGE_SAMPLES);

uint32_t lastTelemetryMs = 0;
long tipRawAveraged = 0;
long twistRawAveraged = 0;
long forceRawAveraged = 0;
bool usbDiagnosticMode = false;
bool tipSampleValid = false;
bool twistSampleValid = false;

static bool validEncoderWord(uint16_t word)
{
  // AMT23: odd parity independently over even and odd positions, including
  // check bits K0 (14) and K1 (15). All-high and all-low both fail.
  uint8_t even = 0, odd = 0;
  for (uint8_t bit = 0; bit < 16; bit += 2)
  {
    even ^= (word >> bit) & 1u;
    odd ^= (word >> (bit + 1)) & 1u;
  }
  return even == 1 && odd == 1;
}

static uint16_t readEncoderBitBang(uint8_t csPin, uint8_t clockPin,
                                   uint8_t dataPin, uint8_t dataBits)
{
  digitalWrite(csPin, LOW);
  delayMicroseconds(20);

  uint16_t word = 0;
  for (uint8_t bit = 0; bit < 16; ++bit)
  {
    if (bit == 8) delayMicroseconds(8);
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(2);
    word = uint16_t((word << 1) | (digitalRead(dataPin) ? 1u : 0u));
    digitalWrite(clockPin, LOW);
    delayMicroseconds(2);
  }

  delayMicroseconds(20);
  digitalWrite(csPin, HIGH);

  if (!validEncoderWord(word)) return 0xFFFFu;
  // Both B encoders are 14-bit. Twist stays normalized to the existing
  // 4096-count wire contract; tip retains all 14 position bits.
  if (dataBits == 12) return uint16_t((word & 0x3FFFu) >> 2);
  if (dataBits == 14) return uint16_t(word & 0x3FFFu);
  return 0;
}

static void sendTelemetryLine(const char* key, long value)
{
  baseSerial.print('@');
  baseSerial.print(key);
  baseSerial.print(',');
  baseSerial.println(value);

  if (usbDiagnosticMode && Serial)
  {
    Serial.print('@');
    Serial.print(key);
    Serial.print(',');
    Serial.println(value);
  }
}

static void publishTelemetry()
{
  // These names and units are the current baseChassisModule mast contract.
  sendTelemetryLine("encfault", (tipSampleValid ? 0 : 1) |
                                (twistSampleValid ? 0 : 2));
  if (tipSampleValid) sendTelemetryLine("tip", tipRawAveraged);
  if (twistSampleValid) sendTelemetryLine("twist", twistRawAveraged);
  sendTelemetryLine("force", forceRawAveraged); // 12-bit ADC count * 32
}

static void sampleSensors()
{
  const uint16_t forceSample = analogRead(FORCE_PIN);
  const uint16_t tipSample = readEncoderBitBang(
      TIP_CS_PIN, TIP_CLOCK_PIN, TIP_DATA_PIN, 14);
  const uint16_t twistSample = readEncoderBitBang(
      TWIST_CS_PIN, TWIST_CLOCK_PIN, TWIST_DATA_PIN, 12);
  const bool tipWasValid = tipSampleValid;
  const bool twistWasValid = twistSampleValid;
  tipSampleValid = tipSample != 0xFFFFu;
  twistSampleValid = twistSample != 0xFFFFu;
  if (!tipWasValid) tipAverage.clear();
  if (!twistWasValid)
  {
    twistCosAverage.clear();
    twistSinAverage.clear();
  }

  // Circular averaging prevents the twist reading from jumping through the
  // middle of the wheel when samples straddle the 4095 -> 0 rollover.
  if (twistSampleValid)
  {
  const float phase = TWO_PI * float(twistSample) / TWIST_ENCODER_COUNTS;
  twistCosAverage.addValue(cosf(phase));
  twistSinAverage.addValue(sinf(phase));
  float averagedPhase = atan2f(twistSinAverage.getFastAverage(),
                               twistCosAverage.getFastAverage());
  if (averagedPhase < 0.0f) averagedPhase += TWO_PI;
  twistRawAveraged = lroundf(
      averagedPhase * TWIST_ENCODER_COUNTS / TWO_PI) & 0x0FFF;
  }

  // Preserve the classic oversampled ranges used by base calibration and the
  // existing default tip zero (approximately 0..131071 for both channels).
  if (tipSampleValid)
  {
    tipAverage.addValue(long(tipSample) * TIP_AVERAGE_SAMPLES);
    tipRawAveraged = lroundf(tipAverage.getFastAverage());
  }
  forceAverage.addValue(long(forceSample) * FORCE_AVERAGE_SAMPLES);
  forceRawAveraged = lroundf(forceAverage.getFastAverage());
}

void setup()
{
  Serial.begin(115200); // Optional diagnostics only; never awaited.

  // Mirror telemetry only for an actively opened mast USB CDC session. USB
  // power alone does not change normal instrument behavior.

  analogReadResolution(12);
  pinMode(FORCE_PIN, INPUT);

  pinMode(TIP_CS_PIN, OUTPUT);
  pinMode(TIP_DATA_PIN, INPUT);
  pinMode(TIP_CLOCK_PIN, OUTPUT);
  digitalWrite(TIP_CS_PIN, HIGH);
  digitalWrite(TIP_CLOCK_PIN, LOW);

  pinMode(TWIST_CS_PIN, OUTPUT);
  pinMode(TWIST_DATA_PIN, INPUT);
  pinMode(TWIST_CLOCK_PIN, OUTPUT);
  digitalWrite(TWIST_CS_PIN, HIGH);
  digitalWrite(TWIST_CLOCK_PIN, LOW);

  // Restore legacy order: establish both SSI idle states before UART startup,
  // then leave them idle for 500 ms before the first encoder transaction.
  baseSerial.begin(BASE_BAUD);
  baseSerial.flush();
  delay(500);
  usbDiagnosticMode = bool(Serial);

  twistCosAverage.clear();
  twistSinAverage.clear();
  tipAverage.clear();
  forceAverage.clear();

  // Seed every average before the first published frame.
  for (uint8_t i = 0; i < FORCE_AVERAGE_SAMPLES; ++i) sampleSensors();
  publishTelemetry();
  if (usbDiagnosticMode)
    Serial.println("@HELLO,MAST,USB_DIAGNOSTIC");
  lastTelemetryMs = millis();
}

void loop()
{
  sampleSensors();

  bool snapshotRequested = false;
  while (baseSerial.available())
    snapshotRequested |= baseSerial.read() == '?';

  const uint32_t now = millis();
  if (snapshotRequested || now - lastTelemetryMs >= TELEMETRY_PERIOD_MS)
  {
    lastTelemetryMs = now;
    publishTelemetry();
  }
}
