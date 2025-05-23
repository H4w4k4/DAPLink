/**
 * @file    stm32l412k8_stm32g0b1re.c
 * @brief   board ID for the stm32l412k8_stm32g0b1re board
 *
 * DAPLink Interface Firmware
 * Copyright (c) 2009-2019, ARM Limited, All Rights Reserved
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

#include "target_family.h"
#include "target_board.h"

const board_info_t g_board_info = {
    .info_version = kBoardInfoVersion,
    .board_id = "D004",
    .family_id = kStub_HWReset_FamilyID,
    .target_cfg = &target_device,
    .board_vendor = "h4w4k4",
    .board_name = "stm32l412k8_stm32h523re",
};
