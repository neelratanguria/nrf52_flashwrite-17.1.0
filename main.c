#include <stdbool.h>
#include <stdio.h>
#include "nrf.h"
#include "bsp.h"
#include "app_error.h"
#include "nrf_nvmc.h"
#include "nordic_common.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "app_timer.h"
#include "nrf_drv_clock.h"

#define FLASHWRITE_EXAMPLE_MAX_STRING_LEN       (62u)
#define FLASHWRITE_EXAMPLE_BLOCK_VALID          (0xA55A5AA5)
#define FLASHWRITE_EXAMPLE_BLOCK_INVALID        (0xA55A0000)
#define FLASHWRITE_EXAMPLE_BLOCK_NOT_INIT       (0xFFFFFFFF)

typedef struct
{
   uint32_t magic_number;
   uint32_t buffer[FLASHWRITE_EXAMPLE_MAX_STRING_LEN + 1]; // + 1 for end of string
} flashwrite_example_flash_data_t;

typedef struct
{
    uint32_t addr;
    uint32_t pg_size;
    uint32_t pg_num;
    flashwrite_example_flash_data_t * m_p_flash_data;
} flashwrite_example_data_t;

static flashwrite_example_data_t m_data;

static ret_code_t clock_config(void)
{
    ret_code_t err_code;

    err_code = nrf_drv_clock_init();
    if (err_code != NRF_SUCCESS && err_code != NRF_ERROR_MODULE_ALREADY_INITIALIZED)
    {
        return err_code;
    }

    nrf_drv_clock_lfclk_request(NULL);

    return NRF_SUCCESS;
}

static void flash_page_init(void)
{
    m_data.pg_num = NRF_FICR->CODESIZE - 1;
    m_data.pg_size = NRF_FICR->CODEPAGESIZE;
    m_data.addr = (m_data.pg_num * m_data.pg_size);

    m_data.m_p_flash_data = (flashwrite_example_flash_data_t *)m_data.addr;

    while (1)
    {
        if (m_data.m_p_flash_data->magic_number == FLASHWRITE_EXAMPLE_BLOCK_VALID)
        {
            return;
        }

        if (m_data.m_p_flash_data->magic_number == FLASHWRITE_EXAMPLE_BLOCK_INVALID)
        {
            ++m_data.m_p_flash_data;
            continue;
        }

        nrf_nvmc_page_erase(m_data.addr);
        return;
    }
}

static void flashwrite_read_cmd()
{
    flashwrite_example_flash_data_t * p_data = (flashwrite_example_flash_data_t *)m_data.addr;
    char string_buff[FLASHWRITE_EXAMPLE_MAX_STRING_LEN + 1]; // + 1 for end of string

    if ((p_data == m_data.m_p_flash_data) &&
        (p_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_VALID))
    {
        printf("Please write something first.\r\n");

        return;
    }

    while (p_data <= m_data.m_p_flash_data)
    {
        if ((p_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_VALID) &&
            (p_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_INVALID))
        {
            printf("Corrupted data found.\r\n");
            return;
        }
        uint8_t i;
        for (i = 0 ; i <= FLASHWRITE_EXAMPLE_MAX_STRING_LEN; i++)
        {
            string_buff[i] = (char)p_data->buffer[i];
        }

        printf("data: ");

        printf("%s", string_buff);
        printf("\r\n");
        ++p_data;
    }
}

static void flash_string_write(uint32_t address, const char * src, uint32_t num_words)
{
    uint32_t i;

    // Enable write.
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
    {
    }

    for (i = 0; i < num_words; i++)
    {
        /* Only full 32-bit words can be written to Flash. */
        ((uint32_t*)address)[i] = 0x000000FFUL & (uint32_t)((uint8_t)src[i]);
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
        {
        }
    }

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy)
    {
    }
}

static void flashwrite_erase_cmd()
{
    nrf_nvmc_page_erase(m_data.addr);

    m_data.m_p_flash_data = (flashwrite_example_flash_data_t *)m_data.addr;
}


 uint8_t m_key[2] = {'a','v'};

void flashwrite_write_cmd(char **argv)
{
    static uint16_t const page_size = 4096;

    uint32_t len = strlen(argv);
    if (len > FLASHWRITE_EXAMPLE_MAX_STRING_LEN)
    {
        printf("Too long string. Please limit entered string to %d chars.\r\n",
                        FLASHWRITE_EXAMPLE_MAX_STRING_LEN);
        return;
    }

    if ((m_data.m_p_flash_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_NOT_INIT) &&
        (m_data.m_p_flash_data->magic_number != FLASHWRITE_EXAMPLE_BLOCK_VALID))
    {
        printf("Flash corrupted, please errase it first.");
        return;
    }

    if (m_data.m_p_flash_data->magic_number == FLASHWRITE_EXAMPLE_BLOCK_VALID)
    {
        uint32_t new_end_addr = (uint32_t)(m_data.m_p_flash_data + 2);
        uint32_t diff = new_end_addr - m_data.addr;
        if (diff > page_size)
        {
            printf("Not enough space - please erase flash first.\r\n");
            return;
        }
        nrf_nvmc_write_word((uint32_t)&m_data.m_p_flash_data->magic_number,
                            FLASHWRITE_EXAMPLE_BLOCK_INVALID);
        ++m_data.m_p_flash_data;
    }

    //++len -> store also end of string '\0'
    flash_string_write((uint32_t)m_data.m_p_flash_data->buffer, argv, ++len);
    nrf_nvmc_write_word((uint32_t)&m_data.m_p_flash_data->magic_number,
                        FLASHWRITE_EXAMPLE_BLOCK_VALID);
}

int main(void)
{
    uint32_t err_code;

    APP_ERROR_CHECK(NRF_LOG_INIT(NULL));

    err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    err_code = clock_config();
    APP_ERROR_CHECK(err_code);

    flash_page_init();

    // Unncommnet to erase the flash
    //flashwrite_erase_cmd();

    char write_string[20] = "BACKINBLACK";

    flashwrite_write_cmd(write_string);

    flashwrite_read_cmd();

    while (true)
    {
        UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());
    }
}