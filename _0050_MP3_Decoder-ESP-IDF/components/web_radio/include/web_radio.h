/*
 * web_radio.h
 *
 *  Created on: 13.03.2017
 *      Author: michaelboeckling
 */

#ifndef INCLUDE_WEB_RADIO_H_
#define INCLUDE_WEB_RADIO_H_

#include "audio_player.h"

typedef struct {

} radio_controls_t;

typedef struct {
    char** urls;
	uint8_t url_listindex;
	uint8_t url_listlen;	
    char *url;
    player_t *player_config;
} web_radio_t;

void web_radio_init(web_radio_t *config);
void web_radio_start(web_radio_t *config);

void web_radio_list_next();
void web_radio_list_prev();
void web_radio_list_index();

typedef enum {
    C_BEGIN_COMMAND = 0x80, C_POWER, C_PLAY, C_STOP, C_T_PLAYNEXT_STOP, C_T_PLAY_STOP, C_T_PLAY_PAUSE, C_PREV, C_NEXT, C_LIST1, C_LIST2, C_LIST3, C_LIST4, C_LIST5, C_LIST6, C_LIST7, C_LIST8, C_LIST9, C_LIST0, C_VOL_UP, C_VOL_DOWN, C_MUTE, C_OK, C_MENU, C_RETURN, C_MODE, C_FN1, C_FN2, C_FN3, C_FN4, C_FN5, C_FN6, C_FN7, vFN8, C_FN9, C_FN10, C_FN11, C_FN12
} web_radio_command_t;

void web_radio_command(web_radio_command_t command);



#endif /* INCLUDE_WEB_RADIO_H_ */
