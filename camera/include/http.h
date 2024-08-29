#ifndef _HTTP_H
#define _HTTP_H

#include <string>
#include <esp_http_client.h>

esp_http_client_handle_t initHttpClient();

std::string getHttpContent(esp_http_client_handle_t);

#endif