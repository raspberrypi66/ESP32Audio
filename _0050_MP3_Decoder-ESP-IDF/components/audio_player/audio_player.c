/*
 * audio_player.c
 *
 *  Created on: 12.03.2017
 *      Author: michaelboeckling
 */


#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"

#include "audio_player.h"
#include "spiram_fifo.h"
#include "freertos/task.h"
#include "mp3_decoder.h"

#define PRIO_MAD configMAX_PRIORITIES - 2

int delayIn = 0; 
int fadeIn = 0; 

double dev_gain = GAIN_DEFAULT;
double app_gain = GAIN_DEFAULT;

void player_vol_up()
{
	double tmp = app_gain + GAIN_STEP;
	app_gain = (tmp > GAIN_MAX)? GAIN_MAX : tmp;
}

void player_vol_down()
{
	double tmp = app_gain - GAIN_STEP;
	app_gain = (tmp < GAIN_MIN)? GAIN_MIN : tmp;
}

void player_vol_init_fadeIn()
{
	delayIn = 0;
	fadeIn = 0;	
}

void player_toggle_mute()
{
	static double tmp = 0;
	if(tmp == 0)
	{
		tmp = app_gain;
		app_gain = 0;
	}
	else
	{
		app_gain = tmp;
		tmp = 0;
	}
}

uint32_t getCycleCount()
{
    uint32_t ccount;
    __asm__ __volatile__("esync; rsr %0,ccount":"=a" (ccount));
    return ccount;
}

uint32_t IRAM_ATTR millis()
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}



static int t;
static bool mad_started = false;
/* pushes bytes into the FIFO queue, starts decoder task if necessary */
int audio_stream_consumer(char *recv_buf, ssize_t bytes_read, void *user_data)
{
    player_t *player = user_data;

    // don't bother consuming bytes if stopped
    if(player->state == STOPPED) 
	{
        // TODO: add proper synchronization, this is just an assumption
        mad_started = false;
        return -1;
    }

    if (bytes_read > 0) 
	{
        spiRamFifoWrite(recv_buf, bytes_read);
    }

    // seems 4 x 2106 (=READBUFSZ) is enough to prevent initial buffer underflow
    if (!mad_started && player->state == PLAYING && (spiRamFifoFill() > (2106 * 4)) )
    {
        mad_started = true;
        //Buffer is filled. Start up the MAD task.
        // TODO: 6300 not enough?
        if (xTaskCreatePinnedToCore(&mp3_decoder_task, "tskmad", 8000, player, PRIO_MAD, NULL, 1) != pdPASS)
        {
            printf("ERROR creating MAD task! Out of memory?\n");
        } 
		else 
		{
            printf("created MAD task\n");
        }
    }

    t = (t+1) & 255;
    if (t == 0) 
	{
        int bytes_in_buf = spiRamFifoFill();
        uint8_t percentage = (bytes_in_buf * 100) / spiRamFifoLen();
        // printf("Buffer fill %d, buff underrun ct %d\n", spiRamFifoFill(), (int)bufUnderrunCt);
		if(player->state == PLAYING && percentage > 20) 
		{
			audio_renderer_start(player->renderer_config);
		}
		else
		{
			printf("Buffer fill %u%%, %d bytes\n", percentage, bytes_in_buf);
		}
    }

    return 0;
}

void audio_player_init(player_t *player)
{
    // initialize I2S
    audio_renderer_init(player->renderer_config);
}

void audio_player_destroy(player_t *player)
{
    // halt I2S
    audio_renderer_destroy(player->renderer_config);
}
#include "esp_log.h"
void audio_player_start(player_t *player)
{
	// int bytes_in_buf = spiRamFifoFill();
	// ESP_LOGI("audio_player", "bytes_in_buf: %d", bytes_in_buf);

    player->state = PLAYING;

    //audio_renderer_start(player->renderer_config);
}

void audio_player_stop(player_t *player)
{
    player->state = STOPPED;
	vTaskDelay(100 / portTICK_PERIOD_MS);
    audio_renderer_stop(player->renderer_config);
}




