#ifndef CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_DUMP_FORMAT_H_
#define CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_DUMP_FORMAT_H_

#include "./common.h"
#include "./endian_cpp.h"

namespace dump {

constexpr const unsigned BLOCK_SIZE = 1024;

struct Mode {
  enum class Type: uint8_t {
    SOCKET    = 014,
    LINK      = 012,
    REGULAR   = 010,
    BLOCK_DEV = 006,
    DIRECTORY = 004,
    CHAR_DEV  = 002,
    FIFO      = 001,
  };

  union {
    uint16_t value;
    Permissions perms;
    struct {
      uint16_t perms_value: 12;
      uint16_t type_value: 4;
    };
    struct {
      uint16_t : 12;
      Type type: 4;
    };
  };
};

namespace format {

// MAGIC constant, NFS because it said so in the GNU dump/restore.
constexpr const auto MAGIC_NFS = 60012;

struct TimeVal: BigEndianStruct {
  buint32_t sec;
  buint32_t usec;
};


using BMode = BigEndianValue<Mode, uint16_t>;

struct Inode: BigEndianStruct {
  BMode     mode;
  buint16_t hardlink_cnt;
  buint16_t uid_small;
  buint16_t gid_small;
  buint64_t size;
  TimeVal   atime;
  TimeVal   mtime;
  TimeVal   ctime;
  buint32_t device_number;  /* For BLOCK/CHAR device files */
  buint32_t direct_blocks[11];
  buint32_t singly_indirect_blocks;
  buint32_t doubly_indirect_blocks;
  buint32_t triply_indirect_blocks;
  buint32_t flags;
  bint32_t  blocks;
  bint32_t  gen;
  buint32_t gid_big;
  buint32_t uid_big;
  bint32_t  spare[2];
};

struct Record: BigEndianStruct {
  enum class Type: int32_t {
    TAPE  = 1,  /* dump tape header */
    INODE = 2,  /* beginning of file record */
    ADDR  = 4,  /* continuation of file record */
    BITS  = 3,  /* map of inodes on tape */
    CLRI  = 6,  /* map of inodes deleted since last dump */
    END   = 5,  /* end of volume marker */
  };
  using BType = BigEndianValue<Type, int32_t>;

  BType     type;
  bint32_t  date;        /* date of this dump */
  bint32_t  previous_date; /* date of previous dump */
  bint32_t  volume_id;     /* dump volume number */
  buint32_t block_id;      /* logical block of this record */
  buint32_t inode_id;      /* inode number */
  bint32_t  magic;         /* magic number (see above) */
  bint32_t  checksum;      /* record checksum */

  Inode     inode;
  bint32_t  count;        /* number of block */

  union {
    uint8_t  blocks_map[512]; /* 1 => data; 0 => hole in inode */
    bint32_t inodes_map[128]; /* table of first inode on each volume */
  };

  char     label[16];      /* dump label */
  bint32_t level;          /* level of this dump */
  char     filesystem[64]; /* name of dumpped file system */
  char     device[64];     /* name of dumpped device */
  char     host[64];       /* name of dumpped host */
  bint32_t flags;          /* additional information */
  bint32_t first_record;   /* first record on volume */
  bint32_t block_size;     /* blocksize on volume */
  bint32_t ext_attributes; /* additional inode info */
  bint32_t spare[30];      /* reserved for future uses */

  bool Checksum() const {
    constexpr const int32_t checksum_seed = 84446;
    const auto& as_bint32 = reinterpret_cast<
        const bint32_t(&)[sizeof *this / sizeof (bint32_t)]
        >(*this);
    int32_t sum = 0;
    for (auto v : as_bint32) {
      sum += v;
    }
    return sum == checksum_seed;
  }

  bool IsMagicNFS() const {
    return magic == MAGIC_NFS;
  }
};
static_assert(sizeof(Record) == BLOCK_SIZE, "Wrong size for record");

struct DirectoryEntry: BigEndianStruct {
  buint32_t inode_id;      /* inode number referenced by this entry */
  buint16_t record_length; /* length if this very entry */
  uint8_t   type;
  uint8_t   name_len;
  char      name[];        /* filename of length name_len */
};

}  // namespace format

}  // namespace dump

#endif  // CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_DUMP_FORMAT_H_
