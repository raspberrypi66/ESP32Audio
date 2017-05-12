// [ SigmaDelta ]
// channel 0-7 freq 1220-312500 duty 0-255
// prototype
// uint32_t    sigmaDeltaSetup(uint8_t channel, uint32_t freq);
// void        sigmaDeltaWrite(uint8_t channel, uint8_t duty);
// uint8_t     sigmaDeltaRead(uint8_t channel);
// void        sigmaDeltaAttachPin(uint8_t pin, uint8_t channel);
// void        sigmaDeltaDetachPin(uint8_t pin);



uint8_t ch_L = 0;
uint8_t ch_R = 1;

uint8_t pin_L = 25;
uint8_t pin_R = 26;


void setup()
{
  //setup channel L, R with frequency 312500 Hz
  sigmaDeltaSetup(ch_L, 312500);
  sigmaDeltaSetup(ch_R, 312500);

  //attach pin L, R
  sigmaDeltaAttachPin(pin_L, ch_L);
  sigmaDeltaAttachPin(pin_R, ch_R);

  //initialize channel L, R to off
  sigmaDeltaWrite(ch_L, 0);
  sigmaDeltaWrite(ch_R, 0);
}

void loop()
{
  static uint8_t i = 0;
  static uint8_t j = 0;
  static bool swapable = true;

  sigmaDeltaWrite(ch_L, i++);
  sigmaDeltaWrite(ch_R, j--);

  // swap channel
  if (swapable && ((i % 8) == 0))
  {
    ch_L ^= ch_R;
    ch_R ^= ch_L;
    ch_L ^= ch_R;
  }

  // toggle swapable
  static bool buttonState0 = HIGH;
  if (buttonState0 != digitalRead(0))
  {
    buttonState0 = !buttonState0;
    if (buttonState0 == LOW)
    {
      swapable = !swapable;
    }
  }

  delay(8);
}
