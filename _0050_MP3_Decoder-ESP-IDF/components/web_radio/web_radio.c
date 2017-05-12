/*
 * web_radio.c
 *
 *  Created on: 13.03.2017
 *      Author: michaelboeckling
 */


#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "web_radio.h"
//#include "audio_player.h"
#include "spiram_fifo.h"
#include "http.h"
#include "url_parser.h"

#define TAG "web_radio"


static web_radio_t *current_config = NULL;

static TaskHandle_t *reader_task;

static void http_get_task(void *pvParameters)
{
    web_radio_t *radio_conf = pvParameters;

    /* parse URL */
    url_t *url = url_create(radio_conf->url);

    // blocks until end of stream
    int result = http_client_get(
            url->host, url->port, url->path,
            audio_stream_consumer,
            radio_conf->player_config);

    if(result != 0) 
	{
        ESP_LOGE(TAG, "http_client_get error");
    } 
	else 
	{
        ESP_LOGI(TAG, "http_client_get completed");
    }
    // ESP_LOGI(TAG, "http_client_get stack: %d\n", uxTaskGetStackHighWaterMark(NULL));
	//audio_player_stoped(radio_conf->player_config);
	
    url_free(url);
	
    vTaskDelete(NULL);
	

}


void web_radio_start(web_radio_t *config)
{
	int bytes_in_buf = spiRamFifoFill();
	ESP_LOGI("web_radio_start", "bytes_in_buf: %d", bytes_in_buf);

	// audio_player_init(config->player_config);
	
	config->url = config->urls[config->url_listindex];
	ESP_LOGI(TAG, "PLAYLIST: %d/%d: %s", config->url_listindex + 1, config->url_listlen, config->url);
    audio_player_start(config->player_config);
	
	gpio_set_level(GPIO_NUM_16, 1);

	//vTaskDelay(500 / portTICK_PERIOD_MS);

    // start reader task
    xTaskCreatePinnedToCore(&http_get_task, "http_get_task", 2048, config, 20, reader_task, 0);
	
}

void web_radio_stop(web_radio_t *config)
{
    ESP_LOGI(TAG, "RAM left %d", esp_get_free_heap_size());

    audio_player_stop(config->player_config);

	gpio_set_level(GPIO_NUM_16, 0);

    // reader task terminates itself
	//vTaskDelete(http_get_task);
}


static xQueueHandle command_evt_queue = NULL;
static TaskHandle_t *command_task;
#define ESP_INTR_FLAG_DEFAULT 0



/* gpio event handler */
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
	static uint32_t next = 0;
	uint32_t now = millis();
	if(now > next)
	{
		next = millis() + 400;
		uint32_t gpio_num = (uint32_t) arg;
		xQueueSendFromISR(command_evt_queue, &gpio_num, NULL);
	}
}


void command_handler_task(void *pvParams)
{
    web_radio_t *config = (web_radio_t *) pvParams;
    uint32_t param;
    for(;;) 
	{
        if(xQueueReceive(command_evt_queue, &param, portMAX_DELAY)) 
		{
			//uint8_t level = gpio_get_level(io_num);
            //printf("GPIO[%d] intr, val: %d\n", io_num, level);
			
			// from GPIO
			if(param < 0x80)
			{
				switch(config->player_config->state) 
				{
					case PLAYING:
						printf("stopping player\n");
						app_gain = 0;
						web_radio_stop(config);
						break;

					case STOPPED:
						printf("starting player\n");
						web_radio_list_next();
						web_radio_start(config);
						break;

					default:
						printf("player state: %d\n", config->player_config->state);
				}
			}
			else
			{
				switch(param) 
				{
					case C_PREV:
						app_gain = 0;
						web_radio_stop(config);
						vTaskDelay(800 / portTICK_PERIOD_MS);
						web_radio_list_prev();
						web_radio_start(config);
						app_gain = GAIN_DEFAULT;
						break;
						
					case C_NEXT:
						app_gain = 0;
						web_radio_stop(config);
						vTaskDelay(800 / portTICK_PERIOD_MS);
						web_radio_list_next();
						web_radio_start(config);
						app_gain = GAIN_DEFAULT;
						break;
						
					case C_VOL_UP:
						player_vol_up();
						ESP_LOGI(TAG, "app_gain: %.02f", app_gain);
						break;
						
					case C_VOL_DOWN:
						player_vol_down();
						ESP_LOGI(TAG, "app_gain: %.02f", app_gain);
						break;
						
					case C_T_PLAY_STOP:
						if(config->player_config->state == PLAYING)
						{
							app_gain = 0;
							web_radio_stop(config);
							vTaskDelay(800 / portTICK_PERIOD_MS);
						}
						else
						{
							web_radio_start(config);
							dev_gain = GAIN_DEFAULT;
						}
						break;
						

					case C_LIST1:
					case C_LIST2:
					case C_LIST3:
					case C_LIST4:
					case C_LIST5:
					case C_LIST6:
					case C_LIST7:
					case C_LIST8:
					case C_LIST9:
					case C_LIST0:
						app_gain = 0;
						web_radio_stop(config);
						vTaskDelay(800 / portTICK_PERIOD_MS);
						web_radio_list_index(param - C_LIST1);
						web_radio_start(config);
						app_gain = GAIN_DEFAULT;
						break;

				}
			}
        }
    }
}

static void web_radio_controls_init(web_radio_t *config)
{
	current_config = config;
	
	gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
	
    gpio_config_t io_conf;

    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE; //GPIO_PIN_INTR_POSEDGE;
    //bit mask of the pins, use GPIO0 here ("Boot" button)
    io_conf.pin_bit_mask = (1 << GPIO_NUM_0);
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    //create a queue to handle gpio, rmt, etc. event from isr
    command_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    //start command task
    xTaskCreatePinnedToCore(command_handler_task, "command_handler_task", 2048, config, 5, command_task, 0);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    // remove existing handler that may be present
    gpio_isr_handler_remove(GPIO_NUM_0);

    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_NUM_0, gpio_isr_handler, (void*) GPIO_NUM_0);
}

static void web_radio_controls_destroy(web_radio_t *config)
{
    gpio_isr_handler_remove(GPIO_NUM_0);
    vTaskDelete(command_task);
    vQueueDelete(command_evt_queue);
}

void web_radio_init(web_radio_t *config)
{
    web_radio_controls_init(config);
    audio_player_init(config->player_config);
}

void web_radio_destroy(web_radio_t *config)
{
    web_radio_controls_destroy(config);
    audio_player_destroy(config->player_config);
}




void web_radio_list_next()
{
	int tmp = current_config->url_listindex + 1;
	current_config->url_listindex = (tmp >= current_config->url_listlen)? 0 : tmp;	
}

void web_radio_list_prev()
{
	int tmp = current_config->url_listindex - 1;
	current_config->url_listindex = (tmp < 0)? current_config->url_listlen - 1 : tmp;	
}


void web_radio_list_index(int index)
{
	current_config->url_listindex = index % current_config->url_listlen;
}

void web_radio_command(web_radio_command_t command)
{
	static web_radio_command_t prv_command = C_BEGIN_COMMAND;
	static uint32_t next = 0;
	uint32_t now = millis();
	bool valid = false;

	if(now > next)
	{			
		if((now + 800) > next)
		{
			valid = true;
		}
		else
		{
			// accept repeat key
			if(command == prv_command)
			{
				if(command == C_VOL_UP || command == C_VOL_DOWN)
				{
					valid = true;
				}
			}	
		}
	}

	if(valid)
	{
		uint32_t param = (uint32_t) command;
		xQueueSend(command_evt_queue, &param, ( TickType_t )0);
	}

	next = millis() + 100;
	prv_command = command;
}
