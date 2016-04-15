
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
