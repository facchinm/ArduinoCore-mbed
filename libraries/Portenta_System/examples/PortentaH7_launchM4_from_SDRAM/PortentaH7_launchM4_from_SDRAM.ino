#include "SDRAM.h"
#include "PluggableUSBMSD.h"
#include "stm32h7xx_ll_system.h"

//REDIRECT_STDOUT_TO(Serial);

// This address need to be in range 0x10000000-0x3FFF0000 to be usable by the M4 as a trampoline
uint32_t  __attribute__((aligned(0x10000))) trampoline[2];
#define RAM_BASE_FOR_TRAMPOLINE   (uint32_t)&trampoline[0]

// The location in SDRAM where we'll copy the M4 binary
#define SDRAM_FW_LOCATION         0x60000000

void setup() {

  enableMassStorage();

  Serial.begin(115200);
  while (!Serial);

  SDRAM.begin(0);

  pinMode(LED_BUILTIN, OUTPUT);

  // Copy M4 firmware to SDRAM
  FILE* fw = fopen("/fs/fw.bin", "r");
  if (fw == NULL) {
    while (1) {
      Serial.println("Please copy a firmware for M4 core in the PORTENTA mass storage");
      delay(100);
    }
  }
  fread((uint8_t*)SDRAM_FW_LOCATION, getFileSize(fw), 1, fw);
  fclose(fw);

  dump_memory(SDRAM_FW_LOCATION, 100);

  // We need to call this in case we want to use BACKUP_SRAM as trampoline
  HAL_PWR_EnableBkUpAccess();

  // Copy first 2 words of the firmware in trampoline location
  memcpy((void*)RAM_BASE_FOR_TRAMPOLINE, (void*)SDRAM_FW_LOCATION, 8);
  dump_memory(RAM_BASE_FOR_TRAMPOLINE, 8);

  SCB_CleanDCache();

  // Set CM4_BOOT0 address
  // This actually writes a flash register and thus is persistent across reboots
  // RAM_BASE_FOR_TRAMPOLINE must be aligned to 0x10000 barrier
  LL_SYSCFG_SetCM4BootAddress0(RAM_BASE_FOR_TRAMPOLINE >> 16);

  bootM4();
}

void dump_memory(uint32_t address, size_t sz) {
  for (int i = 0; i < sz; i++) {
    if (i % 16 == 0) {
      Serial.println();
    }
    if (((uint8_t*)address)[i] < 16) {
      Serial.print('0');
    }
    Serial.print(((uint8_t*)address)[i], HEX);
    Serial.print(" ");
  }

}

void loop() {
  delay(1000);
}
