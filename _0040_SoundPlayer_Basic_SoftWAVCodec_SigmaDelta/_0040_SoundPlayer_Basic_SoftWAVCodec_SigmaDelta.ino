#include "resource.h"

// Toggle Play/Stop button is attached to PIN 0 (IO0)
#define BTN_PLAYSTOP  0

// Sigma Delta
uint8_t ch_L = 0;
uint8_t ch_R = 1;
uint8_t spk_L = 25;
uint8_t spk_R = 26;
uint8_t led_L = 32;
uint8_t led_R = 33;


// Timer
hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// WAV Audio unsigned 8 bit 16K
volatile const uint8_t *pIndex = resource_data;
volatile const uint8_t *pEnd = resource_data;

volatile uint32_t sampleCount = 0;
volatile uint32_t numSamples = 0;
volatile long gain = 1.5;

typedef struct  WAV_HEADER
{
  /* RIFF Chunk Descriptor */
  uint8_t         RIFF[4];        // RIFF Header Magic header
  uint32_t        ChunkSize;      // RIFF Chunk Size
  uint8_t         WAVE[4];        // WAVE Header
  /* "fmt" sub-chunk */
  uint8_t         fmt[4];         // FMT header
  uint32_t        Subchunk1Size;  // Size of the fmt chunk
  uint16_t        AudioFormat;    // Audio format 1=PCM,6=mulaw,7=alaw,     257=IBM Mu-Law, 258=IBM A-Law, 259=ADPCM
  uint16_t        NumOfChan;      // Number of channels 1=Mono 2=Sterio
  uint32_t        SamplesPerSec;  // Sampling Frequency in Hz
  uint32_t        bytesPerSec;    // bytes per second
  uint16_t        blockAlign;     // 2=16-bit mono, 4=16-bit stereo
  uint16_t        bitsPerSample;  // Number of bits per sample
  /* "data" sub-chunk */
  uint8_t         Subchunk2ID[4]; // "data"  string
  uint32_t        Subchunk2Size;  // Sampled data length
} wav_hdr;

wav_hdr wavHeader;
int headerSize = sizeof(wav_hdr);


// prototypes
bool openAudio();
void IRAM_ATTR timer_isr();
void start_timer();
void stop_timer();
void player_play();
void player_stop();



void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(10);
  Serial.println("");
  Serial.println("begin setup...");

  // Set BTN_PLAYSTOP to input mode
  pinMode(BTN_PLAYSTOP, INPUT_PULLUP);

  //setup channel L, R with frequency 312500 Hz
  sigmaDeltaSetup(ch_L, 312500);
  sigmaDeltaSetup(ch_R, 312500);

  //attach spk L, R
  sigmaDeltaAttachPin(spk_L, ch_L);
  sigmaDeltaAttachPin(spk_R, ch_R);

  //attach led L, R
  sigmaDeltaAttachPin(led_L, ch_L);
  sigmaDeltaAttachPin(led_R, ch_R);

  //initialize channel L, R to off
  sigmaDeltaWrite(ch_L, 0);
  sigmaDeltaWrite(ch_R, 0);

  timer = NULL;

  Serial.println("end setup...");
}


void loop()
{
  //Serial.println("start loop...");

  // If Timer has fired
  if (numSamples && xSemaphoreTake(timerSemaphore, 0) == pdTRUE)
  {
    uint32_t count = 0;
    portENTER_CRITICAL(&timerMux);
    count = sampleCount;
    portEXIT_CRITICAL(&timerMux);
    Serial.println("c: " + String(count * 100 / numSamples) + "%");
    if (count >= numSamples)
    {
      player_stop();
    }
  }

  if (Serial.available() > 0)
  {
    String str = Serial.readString();
    String strValue;
    int nValue = -1;
    float fValue = NAN;

    switch (str[0])
    {
      case 'p':
        // play
        player_play();
        break;

      default:
        break;
    }
  }

  // toggle play/stop
  static bool buttonState0 = HIGH;
  if (buttonState0 != digitalRead(BTN_PLAYSTOP))
  {
    buttonState0 = !buttonState0;
    if (buttonState0 == LOW)
    {
      if (timer)
      {
        player_stop();
      }
      else
      {
        player_play();
      }
    }
  }
}


bool openAudio()
{
  //Serial.flush();
  size_t len = sizeof(resource_data) / sizeof(*resource_data);
  Serial.println("resource len: " + String(len));

  memcpy_P((uint8_t*)&wavHeader, resource_data, headerSize);
  sampleCount = 0;

  //Read the data
  uint16_t bytesPerSample = wavHeader.bitsPerSample / 8;      //Number     of bytes per sample
  if (wavHeader.Subchunk2ID[0] == 'd' && wavHeader.Subchunk2ID[1] == 'a' && wavHeader.Subchunk2ID[2] == 't' && wavHeader.Subchunk2ID[3] == 'a')
  {
    pIndex = resource_data + headerSize;
    numSamples = (wavHeader.Subchunk2Size / bytesPerSample); //How many samples are in the wav file?
  }
  else
  {
    pIndex = resource_data + headerSize + wavHeader.Subchunk2Size + 8;
    numSamples = (wavHeader.ChunkSize / bytesPerSample) - headerSize - wavHeader.Subchunk2Size; //How many samples are in the wav file?
  }

  pEnd = pIndex + numSamples;
  Serial.println("p:" + String((int)resource_data, HEX) + ", " + String((int)pIndex, HEX) + ", " + String((int)pEnd, HEX));

  Serial.print("byte/s:");
  Serial.println(bytesPerSample);
  Serial.print("numS:");
  Serial.println(numSamples);


  // Display the sampling Rate from the header
  Serial.print("hz:");
  Serial.println(wavHeader.SamplesPerSec);
  Serial.print("bit/s:");
  Serial.println(wavHeader.bitsPerSample);
  Serial.print("ch:");
  Serial.println(wavHeader.NumOfChan);
  Serial.print("bps:");
  Serial.println(wavHeader.bytesPerSec);
  Serial.print("len:");
  Serial.println(wavHeader.Subchunk2Size);
  Serial.print("f:");
  Serial.println(wavHeader.AudioFormat);
  // Audio format 1=PCM,6=mulaw,7=alaw, 257=IBM Mu-Law, 258=IBM A-Law, 259=ADPCM

  Serial.println("b:" + String(wavHeader.blockAlign));
  Serial.print("str:");
  Serial.println("str:" + String((char)wavHeader.Subchunk2ID[0]) + String((char)wavHeader.Subchunk2ID[1]) + String((char)wavHeader.Subchunk2ID[2]) + String((char)wavHeader.Subchunk2ID[3]));
}

void IRAM_ATTR timer_isr()
{
  static int16_t d = 0;
  static uint8_t u8d = 0;
  static uint8_t buf[256] = {0};
  static uint8_t bufIndex = 0;

  if ((sampleCount % 256) == 0)
  {
    if (sampleCount < numSamples)
    {

      int len = 256;
      int remain = numSamples - sampleCount;
      if (remain < len)
        len = remain;
      memcpy_P(&buf, (const void*)pIndex + sampleCount, len);
      bufIndex = 0;
    }
  }

  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  if (sampleCount < numSamples)
  {
    // !!!??? d * gain cpu hang
    // fixed type variabe of gain (change float to long)
    d = buf[bufIndex++];
    d -= 128;
    d *= gain;
    d += 128;
    if (d < 0)
      d = 0;
    if (d > 255)
      d = 255;
    u8d = d;
    sigmaDeltaWrite(ch_L, u8d);
    sigmaDeltaWrite(ch_R, u8d);

    ++sampleCount;
  }
  //  else
  //  {
  //    player_stop();
  //  }

  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output

}

void start_timer()
{
  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Use 1st timer of 4 (counted from zero).
  // Set 10 divider for prescaler (see ESP32 Technical Reference Manual for more info).
  timer = timerBegin(0, 10, true);

  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &timer_isr, true);

  // Set alarm to call onTimer function every second (value in microseconds).
  // Repeat the alarm (third parameter)
  timerAlarmWrite(timer, 8000000 / wavHeader.SamplesPerSec, true);

  // Start an alarm
  timerAlarmEnable(timer);
}


void stop_timer()
{
  // If timer is still running
  if (timer)
  {
    // Stop and free timer
    timerEnd(timer);
    timer = NULL;
    delay(20);
  }
}

void player_play()
{
  player_stop();

  openAudio();
  start_timer();
}

void player_stop()
{
  sampleCount = numSamples = 0;
  stop_timer();
  sigmaDeltaWrite(ch_L, 0);
  sigmaDeltaWrite(ch_R, 0);
  Serial.println("player_stop");
}
