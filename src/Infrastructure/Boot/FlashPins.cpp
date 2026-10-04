#include <stdint.h>

#include <esp_rom_efuse.h>
#include <rom/spi_flash.h>
#include <soc/io_mux_reg.h>

// Connects the flash IO2 and IO3 pads to the memory-SPI peripheral.
//
// The application reads flash in QIO: esp_flash_spi_init.c derives the driver's read mode from
// CONFIG_ESPTOOLPY_FLASHMODE_QIO, which is set both in the SDK built from source and in the
// prebuilt one, so every esp_flash_read() issues 0xEB and expects address and data on four lines.
// Two of those lines are IO2 and IO3, and in this configuration nothing else needs them: the
// cache reads through SPI0 in the mode recorded in the image header, which is DIO, and erases and
// writes are single-line commands. So nothing else connects them either, and a data line that is
// not connected to the peripheral reads as a constant - the read returns ESP_OK and wrong bytes.
// SPIFFS, NVS and the OTA data partition all sit on that path. (What frees these two pads from
// being WP# and HOLD# on the chip side is the quad-enable bit, which the driver sets while
// initialising the chip; that is also why erases and writes are unaffected.)
//
// Connecting the pads is the bootloader's job - bootloader_enable_qio_mode() calls this same ROM
// function - but it does so only when the bootloader itself is built for QIO or QOUT, and on the
// boards this has been measured on it does not happen: the pads are still unconnected when the
// application starts. Which bootloader a given module carries cannot be told from here, and it is
// not ours to replace either - an over-the-air update carries the application image alone - so
// the application does the routing itself. Doing it again where the bootloader already did it is
// harmless.
//
// Hence a global constructor and not setup(): do_global_ctors() runs before
// esp_startup_start_app(), while the first SPI1 reads of the application happen inside
// initArduino(), before setup() is entered. One of them is nvs_flash_init(), which erases the
// whole NVS partition - the module's identity and credentials - when a read makes it look like
// the partition has no free pages.
//
// What this does not get is a place ahead of the other global constructors: the linker collects
// them in link order and do_global_ctors() walks that table backwards, so the order is a
// property of the link, and a constructor priority does not change it. Being ahead of app_main()
// is what this needs and what it has; a global constructor that reads flash would have to be
// ordered by hand.
__attribute__((constructor)) static void connectFlashWpHdPins()
{
    const uint32_t spiconfig = esp_rom_efuse_get_flash_gpio_info();
    // With the default pin configuration the flash sits on the IO_MUX pads and the ROM function
    // ignores the WP number passed to it; with any other value the flash is routed through the
    // GPIO matrix and the WP pad number comes from efuse.
    const uint8_t wpGpioNum = (spiconfig == ESP_ROM_EFUSE_FLASH_DEFAULT_SPI)
                                  ? static_cast<uint8_t>(SPI_WP_GPIO_NUM)
                                  : static_cast<uint8_t>(esp_rom_efuse_get_flash_wp_gpio());
    esp_rom_spiflash_select_qio_pins(wpGpioNum, spiconfig);
}
