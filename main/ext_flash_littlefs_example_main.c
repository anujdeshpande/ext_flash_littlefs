/* Example of FAT filesystem on external Flash.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   This sample shows how to store files inside a FAT filesystem.
   FAT filesystem is stored in a partition inside SPI flash, using the
   flash wear levelling library.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_flash.h"
#include "esp_flash_spi_init.h"
#include "esp_partition.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "soc/spi_pins.h"
#include "esp_littlefs.h"
#include "esp_timer.h"

// h2 and c2 will not support external flash
#define EXAMPLE_FLASH_FREQ_MHZ      25

static const char *TAG = "example";

// Pin mapping
// ESP32 (VSPI)
#ifdef CONFIG_IDF_TARGET_ESP32
#define HOST_ID  SPI3_HOST
#define PIN_MOSI SPI3_IOMUX_PIN_NUM_MOSI
#define PIN_MISO SPI3_IOMUX_PIN_NUM_MISO
#define PIN_CLK  SPI3_IOMUX_PIN_NUM_CLK
#define PIN_CS   SPI3_IOMUX_PIN_NUM_CS
#define PIN_WP   SPI3_IOMUX_PIN_NUM_WP
#define PIN_HD   SPI3_IOMUX_PIN_NUM_HD
#define SPI_DMA_CHAN SPI_DMA_CH_AUTO
#else // Other chips (SPI2/HSPI)
#define HOST_ID  SPI2_HOST
#define PIN_MOSI 7
#define PIN_MISO 4
#define PIN_CLK  6
#define PIN_CS   5
//#define PIN_WP   SPI2_IOMUX_PIN_NUM_WP
//#define PIN_HD   SPI2_IOMUX_PIN_NUM_HD
#define SPI_DMA_CHAN SPI_DMA_CH_AUTO
#endif

// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

// Mount path for the partition
const char *base_path = "/extflash";

static esp_flash_t* example_init_ext_flash(void);
static const esp_partition_t* example_add_partition(esp_flash_t* ext_flash, const char* partition_label);
static void example_list_data_partitions(void);
static bool example_mount_fatfs(const char* partition_label);
static bool example_mount_littlefs(const char* partition_label);

void app_main(void)
{
    // Set up SPI bus and initialize the external SPI Flash chip
    esp_flash_t* flash = example_init_ext_flash();
    if (flash == NULL) {
        return;
    }

    // Add the entire external flash chip as a partition
    const char *partition_label = "storage";
    esp_partition_t* my_partition = example_add_partition(flash, partition_label);

    // List the available partitions
    example_list_data_partitions();

    // Initialize FAT FS in the partition
    // if (!example_mount_fatfs(partition_label)) {
    //     return;
    // }

    if (!example_mount_littlefs(partition_label)) {
        return;
    }


    // list files in extflash
    void list_files_in_extflash(void) {
        DIR *dir = opendir("/extflash");
        struct dirent *entry;
        struct stat st;
        char path[300];
        while ((entry = readdir(dir)) != NULL) {
            sprintf(path, "/extflash/%s", entry->d_name);
            stat(path, &st);
            printf("File name: %s, size: %ld bytes\n",path, st.st_size);
        }
        closedir(dir); 
        size_t bytes_total = 0, bytes_used = 0;
        esp_err_t err = esp_littlefs_partition_info(my_partition, &bytes_total, &bytes_used);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get LittleFS info: %s", esp_err_to_name(err));
        }
        else {
            printf("LittleFS: %d B total, %d B used\n", bytes_total, bytes_used);
        }
    }

    // delete all files in extflash
    void delete_all_files_in_extflash(void) {
        DIR *dir = opendir("/extflash");
        struct dirent *entry;
        char path[300];
        struct stat st;
        while ((entry = readdir(dir)) != NULL) {
            printf("Deleting file: %s\n", entry->d_name);
        sprintf(path, "/extflash/%s", entry->d_name);
            stat(path, &st);
            unlink(path);
        }
        closedir(dir); 
    }

    void write_timestamp_and_data_to_file(const char *file_path, uint64_t timestamp, uint32_t data) {
        FILE *f = fopen(file_path, "a");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writing");
            return;
        }
        // Write timestamp (8 bytes) and data (4 bytes) to the file
        fwrite(&timestamp, sizeof(timestamp), 1, f);
        fwrite(&data, sizeof(data), 1, f);
        fclose(f);
        // ESP_LOGI(TAG, "Timestamp and data written to file");
    }
    
    size_t get_littlefs_free_space(void) {
        size_t total_space = 0, used_space = 0, free_space = 0;
        esp_err_t err = esp_littlefs_partition_info(my_partition, &total_space, &used_space);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get LittleFS info: %s", esp_err_to_name(err));
        } else {
            free_space = total_space - used_space;
        }
        return free_space;
    }

    void fill_flash_with_data(const char *file_path) {
        uint64_t timestamp, timestamp2 = 0;
        uint32_t data = 0;
        size_t free_space = get_littlefs_free_space();
        size_t data_size = sizeof(timestamp) + sizeof(data);

        while (free_space >= data_size) {
            timestamp = esp_timer_get_time();
            write_timestamp_and_data_to_file(file_path, timestamp, data);            
            timestamp2 = esp_timer_get_time();
            printf("Time taken to write: %lld us\n", timestamp2 - timestamp);
            free_space = get_littlefs_free_space();
            printf("Free space: %ld %d bytes\n", data, free_space);
            data++;
            if(data %1000==0){
                // break;
                list_files_in_extflash();            
            }
        }
        ESP_LOGI(TAG, "Flash is full. No more space to write data.");
    }

    // delete_all_files_in_extflash();
    // list_files_in_extflash();
    // fill_flash_with_data("/extflash/test.txt");
    list_files_in_extflash();


    // delete_all_files_in_extflash();
    // list_files_in_extflash();


    // Create a file in FAT FS
    // ESP_LOGI(TAG, "Opening file");
    // FILE *f = fopen("/extflash/nexsens.txt", "a");
    // if (f == NULL) {
    //     ESP_LOGE(TAG, "Failed to open file for writing");
    //     return;
    // }
    // fprintf(f, "Written using ESP-IDF %s\n", esp_get_idf_version());
    // for (int i = 0; i < 1000; i++) {
    //     fprintf(f, "1234567890");
    // }
    // fclose(f);
    // ESP_LOGI(TAG, "File written");

    // // Open file for reading
    // ESP_LOGI(TAG, "Reading file");
    // f = fopen("/extflash/hello.txt", "r");
    // if (f == NULL) {
    //     ESP_LOGE(TAG, "Failed to open file for reading");
    //     return;
    // }
    // char line[128];
    // fgets(line, sizeof(line), f);
    // fclose(f);
    // // strip newline
    // char *pos = strchr(line, '\n');
    // if (pos) {
    //     *pos = '\0';
    // }
    // ESP_LOGI(TAG, "Read from file: '%s'", line);
}

static esp_flash_t* example_init_ext_flash(void)
{
    const spi_bus_config_t bus_config = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_CLK,
        // .quadhd_io_num = PIN_HD,
        // .quadwp_io_num = PIN_WP,
    };

    const esp_flash_spi_device_config_t device_config = {
        .host_id = HOST_ID,
        .cs_id = 0,
        .cs_io_num = PIN_CS,
        .io_mode = SPI_FLASH_DIO,
        .freq_mhz = EXAMPLE_FLASH_FREQ_MHZ,
    };

    ESP_LOGI(TAG, "Initializing external SPI Flash");
    ESP_LOGI(TAG, "Pin assignments:");
    ESP_LOGI(TAG, "MOSI: %2d   MISO: %2d   SCLK: %2d   CS: %2d",
        bus_config.mosi_io_num, bus_config.miso_io_num,
        bus_config.sclk_io_num, device_config.cs_io_num
    );

    // Initialize the SPI bus
    ESP_LOGI(TAG, "DMA CHANNEL: %d", SPI_DMA_CHAN);
    ESP_ERROR_CHECK(spi_bus_initialize(HOST_ID, &bus_config, SPI_DMA_CHAN));

    // Add device to the SPI bus
    esp_flash_t* ext_flash;
    ESP_ERROR_CHECK(spi_bus_add_flash_device(&ext_flash, &device_config));

    // Probe the Flash chip and initialize it
    esp_err_t err = esp_flash_init(ext_flash);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize external Flash: %s (0x%x)", esp_err_to_name(err), err);
        return NULL;
    }

    // Print out the ID and size
    uint32_t id;
    ESP_ERROR_CHECK(esp_flash_read_id(ext_flash, &id));
    ESP_LOGI(TAG, "Initialized external Flash, size=%" PRIu32 " KB, ID=0x%" PRIx32, ext_flash->size / 1024, id);

    return ext_flash;
}

static const esp_partition_t* example_add_partition(esp_flash_t* ext_flash, const char* partition_label)
{
    ESP_LOGI(TAG, "Adding external Flash as a partition, label=\"%s\", size=%" PRIu32 " KB", partition_label, ext_flash->size / 1024);
    const esp_partition_t* fat_partition;
    const size_t offset = 0;
    ESP_ERROR_CHECK(esp_partition_register_external(ext_flash, offset, ext_flash->size, partition_label, ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, &fat_partition));

    // Erase space of partition on the external flash chip
    // ESP_LOGI(TAG, "Erasing partition range, offset=%u size=%" PRIu32 " KB", offset, ext_flash->size / 1024);
    // ESP_ERROR_CHECK(esp_partition_erase_range(fat_partition, offset, ext_flash->size));
    return fat_partition;
}

static void example_list_data_partitions(void)
{
    ESP_LOGI(TAG, "Listing data partitions:");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);

    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI(TAG, "- partition '%s', subtype %d, offset 0x%" PRIx32 ", size %" PRIu32 " kB",
        part->label, part->subtype, part->address, part->size / 1024);
    }

    esp_partition_iterator_release(it);
}

// static bool example_mount_fatfs(const char* partition_label)
// {
//     ESP_LOGI(TAG, "Mounting FAT filesystem");
//     const esp_vfs_fat_mount_config_t mount_config = {
//             .max_files = 4,
//             .format_if_mount_failed = true,
//             .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
//     };
//     esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(base_path, partition_label, &mount_config, &s_wl_handle);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
//         return false;
//     }
//     return true;
// }

static bool example_mount_littlefs(const char* partition_label)
{
    ESP_LOGI(TAG, "Mounting LittleFS filesystem");
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/extflash",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(err));
        return false;
    }
    return true;
}