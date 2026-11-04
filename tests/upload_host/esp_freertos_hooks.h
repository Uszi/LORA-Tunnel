#pragma once
using esp_freertos_idle_cb_t=bool(*)();
int esp_register_freertos_idle_hook_for_cpu(esp_freertos_idle_cb_t,unsigned);
void esp_deregister_freertos_idle_hook_for_cpu(esp_freertos_idle_cb_t,unsigned);
