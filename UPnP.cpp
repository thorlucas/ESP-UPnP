#include "UPnP.h"

UPnP::UPnP() {
    ESP_LOGD(TAG, "In UPnP Init");
    initializeWifi();

    xEventGroupWaitBits(wifiEventGroup, WIFI_EVENT_CONN_BIT, false, false, portMAX_DELAY);

    beginMulticast();
}

UPnP::~UPnP() {
    ESP_LOGD(TAG, "UPnP deallocated");
}

void UPnP::initializeWifi() {
    ESP_LOGD(TAG, "Setting up NVS...");

    esp_err_t rvsInitRet = nvs_flash_init();
    if (rvsInitRet == ESP_ERR_NVS_NO_FREE_PAGES || rvsInitRet == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      rvsInitRet = nvs_flash_init();
    }
    ESP_ERROR_CHECK(rvsInitRet);

    ESP_LOGD(TAG, "Setting up events...");

    wifiEventGroup = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // this is passed as the arg pointer
    auto wifiEventLambda = [](void* arg, esp_event_base_t eventBase, int32_t eventID, void* eventData) {
        ((UPnP*) arg)->wifiEventHandler(arg, eventBase, eventID, eventData);
    };

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventLambda, (void*) this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEventLambda, (void*) this));

    ESP_LOGD(TAG, "Initializing WiFi...");

    tcpip_adapter_init();
    
    wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifiInitConfig));

    wifi_config_t wifiConfig = {};
    memcpy(wifiConfig.sta.ssid, CONFIG_WIFI_SSID, sizeof(CONFIG_WIFI_SSID));
    memcpy(wifiConfig.sta.password, CONFIG_WIFI_PASSWORD, sizeof(CONFIG_WIFI_PASSWORD));

    ESP_LOGD(TAG, "WiFi configured for SSID '%s'", wifiConfig.sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifiConfig));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGD(TAG, "Successfully initialized WiFi!");
}

void UPnP::wifiEventHandler(void* arg, esp_event_base_t eventBase, int32_t eventID, void* eventData) {
    if (eventBase == WIFI_EVENT) {
        switch(eventID) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi disconnected! Retrying...");
                xEventGroupClearBits(wifiEventGroup, WIFI_EVENT_CONN_BIT);
                esp_wifi_connect();
                break;
            default:
                break;
        }
    } else if (eventBase == IP_EVENT) {
        ip_event_got_ip_t* ipData;

        switch(eventID) {
            case IP_EVENT_STA_GOT_IP:
                ipData = (ip_event_got_ip_t*) eventData;
                ESP_LOGI(TAG, "Successfully connected to WiFi, got IP: %s", ip4addr_ntoa(&ipData->ip_info.ip));
                
                xEventGroupSetBits(wifiEventGroup, WIFI_EVENT_CONN_BIT);
                break;
            default:
                break; 
        }
    }
}

void UPnP::beginMulticast() {
    // Open multicast socket
    // TODO: Should we set this based on the netif info from esp_wifi?
    ESP_LOGD(TAG, "Opening multicast socket...");
    multicastSockAddr.sin_family = AF_INET;
    multicastSockAddr.sin_port = htons(1900);
    multicastSockAddr.sin_addr.s_addr = htonl(IPADDR_ANY);

    SOCK_ERROR_CHECK(multicastSockFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP));
    SOCK_ERROR_CHECK(bind(multicastSockFD, (sockaddr*) &multicastSockAddr, sizeof(multicastSockAddr)));
    ESP_LOGD(TAG, "Socket FD: %d", multicastSockFD);

    // Reuse socket address
    int yes = 1;
    SOCK_ERROR_CHECK(setsockopt(multicastSockFD, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));

    // Subscribe to igmp
    ESP_LOGD(TAG, "Subscribing to IGMP");
    IP4_ADDR(&multicastGroupAddr, 239, 255, 255, 250);
    SOCK_ERROR_CHECK(igmp_joingroup((ip4_addr_t*) &multicastSockAddr.sin_addr.s_addr, &multicastGroupAddr));

    // Nonblocking
    fcntl(multicastSockFD, F_SETFL, O_NONBLOCK);
}

void UPnP::multicastReceiveTask(void* taskParams) {
    ESP_LOGD(TAG, "In multicast receive task");
    
    ssize_t bytesReceived;
    char buffer[1024];
    sockaddr_storage clientAddress;
    socklen_t clientAddressLen;

    while(1) {
        // TODO: No error check because the socket returns -1 on no packets received too.
        // Kind of dumb.
        ESP_LOGV(TAG, "Receiving...");
        bytesReceived = recvfrom(multicastSockFD, buffer, 1024, 0, (sockaddr*) &clientAddress, &clientAddressLen);
        buffer[bytesReceived] = 0; // null terminate

        if (bytesReceived > 0) {
            ESP_LOGI(TAG, "Received data: \n%s\n", buffer);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void UPnP::beginMulticastReceiveTask() {
    xTaskCreate([](void* taskParams) {
        static_cast<UPnP*>(taskParams)->multicastReceiveTask(taskParams);
    }, "ESP-UPnP MC", 8192, (void*) this, 5, &multicastReceiveTaskHandle);
}