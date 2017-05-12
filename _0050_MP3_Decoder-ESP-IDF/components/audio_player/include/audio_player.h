/*
 * audio_player.h
 *
 *  Created on: 12.03.2017
 *      Author: michaelboeckling
 */

#ifndef INCLUDE_AUDIO_PLAYER_H_
#define INCLUDE_AUDIO_PLAYER_H_

#include "audio_renderer.h"

uint32_t getCycleCount();
uint32_t IRAM_ATTR millis();

#define GAIN_MIN		0.0
#define GAIN_DEFAULT	1.0
#define GAIN_MAX		3.0
#define GAIN_STEP		0.1

#define MAX_DELAY	1024
#define MAX_FADE	4096

extern int delayIn; 
extern int fadeIn; 

extern double dev_gain;
extern double app_gain;

void player_vol_up();
void player_vol_down();
void player_vol_init_fadeIn();
void player_toggle_mute();

int audio_stream_consumer(char *recv_buf, ssize_t bytes_read, void *user_data);


typedef enum {
    IDLE, PLAYING, BUFFER_UNDERRUN, FINISHED, STOPPED
} player_state_t;


typedef struct {
    volatile player_state_t state;
    renderer_config_t *renderer_config;
} player_t;

void audio_player_init(player_t *player);
void audio_player_start(player_t *player);
void audio_player_stop(player_t *player);
void audio_player_destroy(player_t *player);

#endif /* INCLUDE_AUDIO_PLAYER_H_ */
