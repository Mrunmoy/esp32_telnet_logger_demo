/* HTTPS GET Example using plain mbedTLS sockets
 *
 * Contacts the howsmyssl.com API via TLS v1.2 and reads a JSON
 * response.
 *
 * Adapted from the ssl_client1 example in mbedtls.
 *
 * Original Copyright (C) 2006-2016, ARM Limited, All Rights Reserved, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD, Apache 2.0 License.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include <sys/param.h>
#include "esp_log.h"
#include "msgdef.h"


/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

/* Constants that aren't configurable in menuconfig */
#define TELNET_PORT 65056

static const char *TAG = "Telnet";

static QueueHandle_t xTelnetQueue;
static volatile bool Telnet_Initialized = false;
static volatile bool Telnet_Connnected = false;
static SemaphoreHandle_t xTelnetSemaphore;


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        Telnet_Connnected = false;
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void err_handler(void)
{
    while (1)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}


static esp_err_t post_telnet_msg(telnet_msg_t *pMsg)
{
    if ( !pMsg )
    {
        ESP_LOGE(TAG, "Null pointer.");
        return ESP_ERR_INVALID_ARG;
    }

    if (!Telnet_Initialized)
    {
        if ( pMsg->isMalloced )
            free(pMsg->pData);

        ESP_LOGE(TAG, "Telnet queue not initialized.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if( xSemaphoreTake( xTelnetSemaphore, ( TickType_t ) 10 ) == pdTRUE )
    {
        /* We were able to obtain the semaphore and can now access the
        shared resource. */

        /* ... */
        if( xQueueSend( xTelnetQueue, ( void * ) pMsg, ( TickType_t ) 10 ) != pdPASS )
        {
            /* Failed to post the message. */
            ESP_LOGE(TAG, "Failed to send telnet queue msg.");
            if ( pMsg->isMalloced )
                free(pMsg->pData);

            xSemaphoreGive( xTelnetSemaphore );
            return ESP_ERR_INVALID_RESPONSE;
        }
        else
        {
            ESP_LOGI(TAG, "Sent telnet queue msg.");
        }

        /* We have finished accessing the shared resource.  Release the
        semaphore. */
        xSemaphoreGive( xTelnetSemaphore );
    }
    else
    {
        if ( pMsg->isMalloced )
            free(pMsg->pData);
    }
    return ESP_OK;
}

static esp_err_t get_telnet_msg(telnet_msg_t *pMsg)
{
    if ( !pMsg )
        return ESP_ERR_INVALID_ARG;

    if( xSemaphoreTake( xTelnetSemaphore, ( TickType_t ) 10 ) == pdTRUE )
    {
        /* We were able to obtain the semaphore and can now access the
        shared resource. */
        if( xQueueReceive( xTelnetQueue, pMsg, ( TickType_t ) 10 ) != pdPASS )
        {
            /* We have finished accessing the shared resource.  Release the
            semaphore. */
            xSemaphoreGive( xTelnetSemaphore );
            return ESP_ERR_INVALID_RESPONSE;
        }
        /* We have finished accessing the shared resource.  Release the
        semaphore. */
        xSemaphoreGive( xTelnetSemaphore );
    }

    return ESP_OK;
}


static void sample1_task(void *pvParameters)
{
    (void)pvParameters;
    int max_len = 100;
    int count = 1;

    while(1)
    {
        /* Wait for the callback to set the CONNECTED_BIT in the
           event group.
        */
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                            false, true, portMAX_DELAY);

        while ( !Telnet_Initialized || !Telnet_Connnected )
        {
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        telnet_msg_t msg = {0};

        msg.pData = calloc(max_len, sizeof(char));
        msg.isMalloced = true;
        snprintf(msg.pData, max_len - 1, "Sensor Debug msg: %d\n", count++);
        msg.pSenderTag = "Sensor task\n";
        msg.length = strlen((char *)msg.pData)+1;

        if ( post_telnet_msg(&msg) != ESP_OK )
        {
            ESP_LOGE(TAG, "Failed to post msg to telent task.");
        }

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

static void sample2_task(void *pvParameters)
{
    (void)pvParameters;
    int max_len = 100;
    int count = 1;

    while(1)
    {
        /* Wait for the callback to set the CONNECTED_BIT in the
           event group.
        */
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                            false, true, portMAX_DELAY);

        while ( !Telnet_Initialized || !Telnet_Connnected )
        {
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        telnet_msg_t msg = {0};

        msg.pData = calloc(max_len, sizeof(char));
        msg.isMalloced = true;
        snprintf(msg.pData, max_len - 1, "Display Debug msg: %d\n", count++);
        msg.pSenderTag = "Display task\n";
        msg.length = strlen((char *)msg.pData)+1;

        if ( post_telnet_msg(&msg) != ESP_OK )
        {
            ESP_LOGE(TAG, "Failed to post msg to telent task.");
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void telnet_task(void *pvParameters)
{
    char buffer[5*1024] = {0};
    esp_err_t err = 0;

    (void)pvParameters;

    // Init
    xTelnetQueue = xQueueCreate( TELNET_MAX_Q_ELEMENTS, TELNET_MSG_SIZE );
    if( xTelnetQueue == NULL )
    {
        ESP_LOGE(TAG, "Failed to create telnet msg queue. Error 0x%x", err);
        err_handler();
    }

    xTelnetSemaphore = xSemaphoreCreateMutex();

    if( xTelnetSemaphore == NULL )
    {
        ESP_LOGE(TAG, "Failed to create telnet queue mutex.");
        err_handler();
    }

    Telnet_Initialized = true;

    // loop
    while(1)
    {
        /* Wait for the callback to set the CONNECTED_BIT in the
           event group.
        */
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                            false, true, portMAX_DELAY);

        struct in_addr iaddr = { 0 };
        tcpip_adapter_ip_info_t ip_info = { 0 };

        err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to get IP address info. Error 0x%x", err);
            err_handler();
        }

        inet_addr_from_ipaddr(&iaddr, &ip_info.ip);
        ESP_LOGI(TAG, "IP address %s", inet_ntoa(iaddr.s_addr));

        // init
        int sock = -1;

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Failed to create socket. Error %d", errno);
            err_handler();
        }

        int value = 1;
        err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
        if (err < 0)
        {
            ESP_LOGE(TAG, "Failed to set SO_REUSEADDR. Error %d", errno);
            close(sock);
            continue;
        }

        struct sockaddr_in server;
        // Prepare the sockaddr_in structure
        server.sin_family      = AF_INET;
        server.sin_addr.s_addr = INADDR_ANY;
        server.sin_port        = htons( TELNET_PORT );

        // Bind
        err = bind(sock, (struct sockaddr *)&server , sizeof(server));
        if( err < 0)
        {
            ESP_LOGE(TAG, "Failed to bind. Error %d", err);
            close(sock);
            continue;
        }

        // Listen for 1 connection
        err = listen(sock , 1);
        if( err < 0)
        {
            ESP_LOGE(TAG, "Failed to listen. Error %d", err);
            close(sock);
            continue;
        }

        ESP_LOGI(TAG, "Waiting for client on port %d", TELNET_PORT);
        //accept connection from an incoming client
        struct sockaddr_in client;
        int c_len = 0;
        int client_sock = accept(sock, (struct sockaddr *)&client, (socklen_t *)&c_len);
        if (client_sock < 0)
        {
            ESP_LOGE(TAG, "Failed to accept. Error %d", err);
            close(sock);
            continue;
        }
        ESP_LOGI(TAG, "Connection accepted.");
        Telnet_Connnected = true;

        err = lwip_fcntl_r(client_sock, F_SETFL, O_NONBLOCK);
        if (err < 0)
        {
            ESP_LOGE(TAG, "Failed to set socket to non blocking. Error %d", err);
        }

        // Receive message from client
        int read_size = 0;
        while( 1 )
        {
            read_size = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

            if(read_size > 0)
            {
                ESP_LOGI(TAG, "%d bytes received", read_size);
                // Process message

                // Send response to client
                write(client_sock , buffer , strlen(buffer));
            }
            else if(read_size != 0)
            {
                telnet_msg_t msg = {0};
                if ( get_telnet_msg(&msg) == ESP_OK )
                {
                    write(client_sock, msg.pSenderTag, strlen(msg.pSenderTag));
                    write(client_sock, msg.pData, msg.length);
                    if ( msg.isMalloced )
                        free(msg.pData);
                }
            }

            if(read_size == 0)
            {
                ESP_LOGI(TAG, "Client disconnected. Goodbye!");
                Telnet_Connnected = false;
                close(client_sock);
                close(sock);
                break;
            }
            else if(read_size == -1)
            {
                //ESP_LOGE(TAG, "Failed to receive. Error %d", err);
            }
        }


        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}


void app_main()
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    xTaskCreate(&telnet_task,  "telnet_task", 8192, NULL, 5, NULL);
    xTaskCreate(&sample1_task, "Sensor task", 8192, NULL, 5, NULL);
    xTaskCreate(&sample2_task, "Display task", 8192, NULL, 5, NULL);
}
