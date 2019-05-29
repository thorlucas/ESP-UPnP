#ifndef _UPNP_H_
#define _UPNP_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "nvs_flash.h"

#include "lwip/def.h"
#include "lwip/opt.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"

#include <cstdlib>
#include <cstring>
#include <functional>

#define WIFI_EVENT_CONN_BIT BIT0

#define SOCK_ERROR_CHECK(expr) if ((expr) < 0) { ESP_LOGE(TAG, "Socket error: %s", strerror(errno)); }

void vTaskCode( void * pvParameters );

class UPnP {
private:
	static constexpr const char* TAG = "ESP-UPnP";
	
	/**
	 * This is the multicast socket's address.
	 * The port must be 1900.
	 */
	sockaddr_in multicastSockAddr;
	ip4_addr_t multicastGroupAddr;
	int multicastSockFD; /**< Multicast socket file descriptor. */

	EventGroupHandle_t wifiEventGroup;

	TaskHandle_t multicastReceiveTaskHandle;

	void initializeWifi();
	void wifiEventHandler(void* arg, esp_event_base_t eventBase, int32_t eventID, void* eventData);
	void beginMulticast();

	void multicastReceiveTask(void* taskParams);

public:
	/**
	 * This will initialize WiFi as create the required sokets.
	 */
	UPnP();
	~UPnP();

	void beginMulticastReceiveTask();
};

#endif // _UPNP_H_