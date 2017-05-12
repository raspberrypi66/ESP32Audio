/*
 * audio_renderer.c
 *
 *  Created on: 12.03.2017
 *      Author: michaelboeckling
 */

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "soc/i2s_struct.h"
#include "driver/i2s.h"

#include "audio_renderer.h"
#include "audio_player.h"

#define TAG "renderer"

typedef enum 
{
     RENDER_ACTIVE, RENDER_STOPPED
} renderer_state_t;


/* TODO: refactor */
static renderer_config_t *curr_config;
static renderer_state_t state;

// Called by the NXP modifications of libmad. Sets the needed output sample rate.
static int prevRate;
void set_dac_sample_rate(int rate)
{
    if(rate == prevRate)
        return;
    prevRate = rate;

    // modifier will usually be 1.0
    rate = rate / curr_config->sample_rate_modifier;

    //ESP_LOGI(TAG, "setting sample rate to %d\n", rate);
    i2s_set_sample_rates(curr_config->i2s_num, rate);
}



//static short convert_16bit_stereo_to_8bit_stereo(short left, short right)
//{
//    unsigned short sample = (unsigned short) left;
//    sample = (sample << 8 & 0xff00) | (((unsigned short) right >> 8) & 0x00ff);
//    return sample;
//}

//static int convert_16bit_stereo_to_16bit_stereo(short left, short right)
//{
//    unsigned int sample = (unsigned short) left;
//    sample = (sample << 16 & 0xffff0000) | ((unsigned short) right);
//    return sample;
//}

static void init_i2s(renderer_config_t *config)
{

    i2s_config_t i2s_config = {
            .mode = I2S_MODE_MASTER | I2S_MODE_TX,          // Only TX
            .sample_rate = config->sample_rate,
            .bits_per_sample = config->bit_depth,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,   // 2-channels
            .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
            .dma_buf_count = 14,                            // number of buffers, 128 max.
            .dma_buf_len = 32 * 2,                          // size of each buffer
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1        // Interrupt level 1
    };

    i2s_pin_config_t pin_config = {
            .bck_io_num = 26,
            .ws_io_num = 25,
            .data_out_num = 22,
            .data_in_num = I2S_PIN_NO_CHANGE    // Not used
    };

    i2s_driver_install(config->i2s_num, &i2s_config, 0, NULL);
    i2s_set_pin(config->i2s_num, &pin_config);
}

/*
 * Output audio without I2S codec via built-in 8-Bit DAC.
 */
static void init_i2s_dac(renderer_config_t *config)
{
	// clock relative: i2s_CLK = i2s_BCK * 16
	// other i2s mode use "i2s_set_sample_rates()" for result target that i2s_BCK clock
	// that internal ADC mode not use i2s_BCK clock but directly connect [dac_CLK  <--> i2s_CLK] 
	// for result target that i2s_CLK clock must divide rate by 16 (rate = rate/16)
	config->sample_rate_modifier = 16;
	prevRate = config->sample_rate / config->sample_rate_modifier;

    i2s_config_t i2s_config = {
            .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN, // Only TX
            .sample_rate = prevRate,
            //.bits_per_sample = I2S_BITS_PER_SAMPLE_8BIT,    	// support 8-bit and 16-bit mode
            .bits_per_sample = config->bit_depth,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,   	// 2-channels
            .communication_format = I2S_COMM_FORMAT_PCM, 
            .dma_buf_count = 32,	        						// number of buffers, 128 max.
            .dma_buf_len = 64,                          	// size of each buffer
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1        	// Interrupt level 1
    };

	i2s_driver_install(config->i2s_num, &i2s_config, 0, NULL);
	
	// fixed bug (only mono FIFO) in original driver tx_right_first and tx_fifo_mod >>>
	audio_renderer_stop(config);
	I2S0.fifo_conf.dscr_en = 0;
    // I2S0.conf.rx_fifo_reset = 1;
    // I2S0.conf.rx_fifo_reset = 0;
    // I2S0.conf.tx_fifo_reset = 1;
    // I2S0.conf.tx_fifo_reset = 0;	
    // I2S0.conf.tx_right_first = 0;
    I2S0.fifo_conf.tx_fifo_mod = 0;	
	I2S0.fifo_conf.dscr_en = 1;
	// fixed bug (only mono FIFO) in original driver tx_right_first and tx_fifo_mod <<<
    
	i2s_set_pin(config->i2s_num, NULL);
	//i2s_set_sample_rates(config->i2s_num, config->sample_rate / 16); //set sample rates
	//set_dac_sample_rate(config->sample_rate);
}




/* render callback for the libmad synth */
void render_sample_block(short *sample_buff_ch0, short *sample_buff_ch1, int num_samples, unsigned int num_channels)
{
    // if mono: simply duplicate the left channel
    if(num_channels == 1) 
	{
        sample_buff_ch1 = sample_buff_ch0;
    }

    // max delay: 50ms instead of portMAX_DELAY
    static  TickType_t delay = 50 / portTICK_PERIOD_MS;
	
	//uint8_t left[2];
	//uint8_t right[2];
	static uint8_t samp8[4];
	static int16_t samp16[2];
	static int32_t temp;
	static int bytes_pushed;
	
	if(MAX_DELAY > delayIn)
	{
		dev_gain = 0.0001;
		delayIn++;
	}	
	else if(MAX_FADE > fadeIn)
	{
		dev_gain = app_gain * fadeIn++ / (double)MAX_FADE;
	}
	else
	{
		dev_gain = app_gain;
	}
	
    switch(curr_config->output_mode) 
	{
        case DAC_BUILT_IN:
			if(curr_config->bit_depth == I2S_BITS_PER_SAMPLE_8BIT)
			{
				for (int i=0; i < num_samples; i++) 
				{
					if(state == RENDER_STOPPED)
						break;

					//short samp8 = convert_16bit_stereo_to_8bit_stereo(sample_buff_ch0[i], sample_buff_ch1[i]);
					
					bytes_pushed = 0;
					// send LEFT
					samp8[0] = 0;
					samp8[1] = ((int16_t)(sample_buff_ch0[i] * dev_gain) >> 8) ^ 0x80;
					bytes_pushed += i2s_push_sample(curr_config->i2s_num,  (char *)samp8, delay);

					// send RIGHT
					samp8[0] = 0;
					samp8[1] = ((int16_t)(sample_buff_ch1[i] * dev_gain) >> 8) ^ 0x80;
					bytes_pushed += i2s_push_sample(curr_config->i2s_num,  (char *)samp8, delay);
					
					// DMA buffer full - retry
					if(bytes_pushed == 0) 
					{
						i--;
					}
				}
			}
			else
			{
				for (int i=0; i < num_samples; i++) 
				{
					if(state == RENDER_STOPPED)
						break;

					// samp16[0] = ((int16_t)(sample_buff_ch0[i] * app_gain)) ^ 0x8000;
					// samp16[1] = ((int16_t)(sample_buff_ch1[i] * app_gain)) ^ 0x8000;
					temp = sample_buff_ch0[i];
					temp *= dev_gain;
					if(temp < -32768) 
						temp = 0x0000;
					else if(temp > 32767) 
						temp = 0xFFFF;
					else
						temp ^= 0x8000;
					samp16[0] = temp;

					temp = sample_buff_ch1[i];
					temp *= dev_gain;
					if(temp < -32768) 
						temp = 0x0000;
					else if(temp > 32767) 
						temp = 0xFFFF;
					else
						temp ^= 0x8000;
					samp16[1] = temp;
					
					bytes_pushed = i2s_push_sample(curr_config->i2s_num,  (char *)samp16, delay);

					// DMA buffer full - retry
					if(bytes_pushed == 0) 
					{
						i--;
					}
				}
				
				//bytes_pushed = i2s_write_bytes(curr_config->i2s_num, (char *)buff, 128, ( TickType_t )0);
				//ESP_LOGI(TAG, "bytes_pushed %d\n", bytes_pushed);
			}
            break;

        case I2S:
            for (int i=0; i < num_samples; i++) 
			{
                if(state == RENDER_STOPPED)
                    break;

                //samp16 = convert_16bit_stereo_to_16bit_stereo(sample_buff_ch0[i], sample_buff_ch1[i]);
				samp16[0] = sample_buff_ch0[i] * dev_gain;
				samp16[1] = sample_buff_ch1[i] * dev_gain;
                bytes_pushed = i2s_push_sample(curr_config->i2s_num,  (char *)samp16, delay);

                // DMA buffer full - retry
                if(bytes_pushed == 0) 
				{
                    i--;
                }
            }
            break;

        case PDM:
            // TODO
			ESP_LOGE(TAG, "TODO: PDM");
            break;
    }
}


/* init renderer sink */
void audio_renderer_init(renderer_config_t *config)
{
    // update global
    curr_config = config;
    state = RENDER_STOPPED;

    switch (config->output_mode) 
	{
        case I2S:
            init_i2s(config);
            break;

        case DAC_BUILT_IN:
            init_i2s_dac(config);
            break;

        case PDM:
            // TODO
            break;
    }
}


void audio_renderer_start(renderer_config_t *config)
{
	if(state == RENDER_ACTIVE)
		return;

	player_vol_init_fadeIn();

    i2s_start(config->i2s_num);
    state = RENDER_ACTIVE;
	
}

void audio_renderer_stop(renderer_config_t *config)
{
    state = RENDER_STOPPED;
    i2s_stop(config->i2s_num);
	I2S0.fifo_conf.dscr_en = 0;
    I2S0.conf.rx_fifo_reset = 1;
    I2S0.conf.rx_fifo_reset = 0;
    I2S0.conf.tx_fifo_reset = 1;
    I2S0.conf.tx_fifo_reset = 0;	
	I2S0.fifo_conf.dscr_en = 1;
}

void audio_renderer_destroy(renderer_config_t *config)
{
    state = RENDER_STOPPED;
    i2s_driver_uninstall(config->i2s_num);
    free(config);
}

