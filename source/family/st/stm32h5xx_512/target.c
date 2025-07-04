/**
 * @file    target.c
 * @brief   Target information for the STM32G0B1RE
 *
 * DAPLink Interface Firmware
 * Copyright (c) 2017-2019, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "target_config.h"
#include "target_family.h"

// The file flash_blob.c must only be included in target.c
#include "flash_blob.c"

// target information
target_cfg_t target_device = {
    .version                        = kTargetConfigVersion,
    .sectors_info                   = sectors_info,
    .sector_info_length             = (sizeof(sectors_info))/(sizeof(sector_info_t)),
    .flash_regions[0].start         = 0x08000000,
    .flash_regions[0].end           = 0x08080000,
    .flash_regions[0].flags         = kRegionIsDefault,
    .flash_regions[0].flash_algo    = (program_target_t *) &flash,
    .ram_regions[0].start           = 0x20000000,
    .ram_regions[0].end             = 0x20020000,
    .ram_regions[1].start           = 0x20020000,
    .ram_regions[1].end             = 0x20034000,
    .ram_regions[2].start           = 0x20034000,
    .ram_regions[2].end             = 0x20044000,
    .target_vendor                  = "STMicroelectronics",
    .target_part_number             = "STM32H523RETx",
};

const target_family_descriptor_t g_m33_series = {
    .family_id = kStub_M33_FamilyID, //ID not maching the predefined family ids
    .default_reset_type = kHardwareReset,
    .apsel = (1 << 24), // AP1, Cortex M33 debug access port AHB-AP
    .csw = 0x43000050,
};

const target_family_descriptor_t *g_target_family = &g_m33_series;
