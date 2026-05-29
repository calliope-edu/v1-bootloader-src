/* Copyright (c) 2013 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

#include "bootloader_settings.h"
#include <stdint.h>
#include <dfu_types.h>

/* Calliope fix (2026-05): pre-seed the settings page with bank_0 = BANK_VALID_APP
 * so the bundled app at DFU_BANK_0_REGION_START boots on the very first cold start
 * after a fresh combined-hex flash. Without this initializer the page emits as
 * all-zeroes in the hex, bank_0 != BANK_VALID_APP, and bootloader_app_is_valid()
 * returns false on every boot -> bootloader stays in DFU mode forever. After a
 * successful DFU, bootloader_settings_save() writes the same 0x01 marker, so this
 * initializer only governs the first boot of a freshly-flashed device.
 *
 * Layout matches bootloader_settings_t (Nordic SDK 8, sizeof(int)=4 enums):
 *   bytes 0-3:  bank_0      = 0x00000001 (BANK_VALID_APP)
 *   bytes 4-5:  bank_0_crc  = 0          (CRC validation skipped when 0)
 *   bytes 6-7:  padding to 4-byte boundary
 *   bytes 8-11: bank_1      = 0x000000FE (BANK_ERASED)
 *   bytes 12+:  bank_*_size = 0          (unused on first boot)
 */
#define CALLIOPE_BOOTLOADER_SETTINGS_INIT \
    0x01, 0x00, 0x00, 0x00,   /* bank_0 = BANK_VALID_APP */ \
    0x00, 0x00, 0x00, 0x00,   /* bank_0_crc = 0 + padding */ \
    0xFE, 0x00, 0x00, 0x00    /* bank_1 = BANK_ERASED */

#if defined ( __CC_ARM )
uint8_t  m_boot_settings[CODE_PAGE_SIZE] __attribute__((at(BOOTLOADER_SETTINGS_ADDRESS))) __attribute__((used)) = { CALLIOPE_BOOTLOADER_SETTINGS_INIT };
uint32_t m_uicr_bootloader_start_address __attribute__((at(NRF_UICR_BOOT_START_ADDRESS))) = BOOTLOADER_REGION_START;            /**< This variable ensures that the linker script will write the bootloader start address to the UICR register. This value will be written in the HEX file and thus written to UICR when the bootloader is flashed into the chip. */
#elif defined ( __GNUC__ )
__attribute__ ((section(".bootloaderSettings"))) uint8_t m_boot_settings[CODE_PAGE_SIZE] = { CALLIOPE_BOOTLOADER_SETTINGS_INIT };
__attribute__ ((section(".uicrBootStartAddress"))) volatile uint32_t m_uicr_bootloader_start_address = BOOTLOADER_REGION_START; /**< This variable ensures that the linker script will write the bootloader start address to the UICR register. This value will be written in the HEX file and thus written to UICR when the bootloader is flashed into the chip. */
#elif defined ( __ICCARM__ )
__no_init uint8_t m_boot_settings[CODE_PAGE_SIZE] @ 0x0003FC00 = { CALLIOPE_BOOTLOADER_SETTINGS_INIT };
__root    const uint32_t m_uicr_bootloader_start_address @ 0x10001014 = BOOTLOADER_REGION_START;                                /**< This variable ensures that the linker script will write the bootloader start address to the UICR register. This value will be written in the HEX file and thus written to UICR when the bootloader is flashed into the chip. */
#endif


void bootloader_util_settings_get(const bootloader_settings_t ** pp_bootloader_settings)
{
    // Read only pointer to bootloader settings in flash. 
    bootloader_settings_t const * const p_bootloader_settings =
        (bootloader_settings_t *)&m_boot_settings[0];        

    *pp_bootloader_settings = p_bootloader_settings;
}
