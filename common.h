/* Copyright 2016 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_COMMON_H_
#define CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_COMMON_H_

#include <cstdint>

union Permissions {
  uint16_t raw;
  struct {
    uint16_t other: 3;
    uint16_t group: 3;
    uint16_t user: 3;
    uint16_t sbits: 3;
  };
  struct {
    uint16_t other_exec : 1;
    uint16_t other_write: 1;
    uint16_t other_read : 1;

    uint16_t group_exec : 1;
    uint16_t group_write: 1;
    uint16_t group_read : 1;

    uint16_t user_exec : 1;
    uint16_t user_write: 1;
    uint16_t user_read : 1;

    uint16_t sticky_vtx: 1;
    uint16_t set_gid   : 1;
    uint16_t set_uid   : 1;
  };

  operator uint16_t () const {
    return raw & 0x0FFF;
  }
};

#endif  // CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_COMMON_H_
