// [ DAC ]
// prototype
// void dacWrite(uint8_t pin, uint8_t value);

uint8_t ch_L = 25;
uint8_t ch_R = 26;

void setup()
{
  dacWrite(25, 0);
  dacWrite(26, 0);
}

void loop()
{
  static uint8_t i = 0;
  static uint8_t j = 0;
  static bool swapable = true;

  dacWrite(ch_L, i++);
  dacWrite(ch_R, j--);

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
