/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/* sha_core.h is the v5.5 name for the combined SHA hardware API.
 * Include sha_dma.h for the DMA-based functions and add esp_sha_set_mode. */
#include "sha/sha_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the mode for the SHA engine
 *
 * @note Call esp_sha_acquire_hardware() before calling this function.
 *
 * @param sha_type The SHA algorithm type
 */
void esp_sha_set_mode(esp_sha_type sha_type);

/**
 * @brief Execute SHA block operation (non-DMA)
 *
 * @note Call esp_sha_acquire_hardware() and esp_sha_set_mode() before calling this.
 *
 * @param sha_type SHA algorithm to use.
 * @param data_block Pointer to the input data block.
 * @param is_first_block True if this is the first block of the hash.
 */
void esp_sha_block(esp_sha_type sha_type, const void *data_block, bool is_first_block);

#ifdef __cplusplus
}
#endif
