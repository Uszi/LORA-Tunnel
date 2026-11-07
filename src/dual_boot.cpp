#include "dual_boot.h"
#include "version.h"
#include <cstdio>
#include <esp_app_desc.h>
#include <esp_idf_version.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <sdkconfig.h>

#if defined(LORA_TUNNEL_CARDPUTER_ADV)
#define DUAL_BOOT_TEXT(x) #x
#define DUAL_BOOT_NUMBER(x) DUAL_BOOT_TEXT(x)
// Replaces the SDK's weak descriptor ("arduino-lib-builder" plus its commit),
// so the other firmware reads this app's name and version from the slot.
// The image then holds only this descriptor, first in DROM (offset 0x20).
extern "C" const esp_app_desc_t esp_app_desc __attribute__((section(".rodata_desc"), used)) = {
  .magic_word = ESP_APP_DESC_MAGIC_WORD,
  .secure_version = 0,
  .reserv1 = {0, 0},
  .version = TUNNEL_VERSION,
  .project_name = "lora-tunnel",
  .time = __TIME__,
  .date = __DATE__,
  .idf_ver = "v" DUAL_BOOT_NUMBER(ESP_IDF_VERSION_MAJOR) "." DUAL_BOOT_NUMBER(ESP_IDF_VERSION_MINOR) "."
             DUAL_BOOT_NUMBER(ESP_IDF_VERSION_PATCH),
  .app_elf_sha256 = {},
  .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
  .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
  .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
};
#endif

// initArduino() confirms a pending OTA image before setup() unless this
// returns true; confirmIfStable() does it after a working start instead.
extern "C" bool verifyRollbackLater() { return true; }

namespace dual_boot {
namespace {
constexpr uint32_t CONFIRM_AFTER_MS = 30000;
bool confirmed = false;

const esp_partition_t* otherApp(esp_app_desc_t& desc) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if(!running || !other || other == running) return nullptr;
  return esp_ota_get_partition_description(other, &desc) == ESP_OK ? other : nullptr;
}
}

bool describeOther(char* out, size_t size) {
  esp_app_desc_t desc{};
  if(!otherApp(desc)) { if(size) out[0] = 0; return false; }
  snprintf(out, size, "%.*s %.*s", int(sizeof(desc.project_name)), desc.project_name,
           int(sizeof(desc.version)), desc.version);
  return true;
}

bool confirmIfStable(uint32_t uptimeMs, bool healthy) {
  if(confirmed || uptimeMs < CONFIRM_AFTER_MS || !healthy) return false;
  confirmed = true;
  esp_ota_img_states_t state;
  if(esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
     state != ESP_OTA_IMG_PENDING_VERIFY) return false;
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

bool startOther() {
  esp_app_desc_t desc{};
  const esp_partition_t* other = otherApp(desc);
  if(!other || esp_ota_set_boot_partition(other) != ESP_OK) return false;
  esp_restart();
}
}
