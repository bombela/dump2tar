#ifndef CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_DUMP_READER_H_
#define CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_DUMP_READER_H_

#include "./dump_format.h"

#include <iostream>
#include <unordered_map>
#include <vector>
#include <cassert>
#include <iomanip>

namespace dump {

struct Inode {
  uint32_t inode_id;
  uint16_t hardlink_cnt;
  Mode     mode;
  uint32_t uid;
  uint32_t gid;
  uint64_t size;
  uint64_t atime_us;
  uint64_t mtime_us;
  uint64_t ctime_us;
};

std::ostream& operator<<(std::ostream& os, const Inode& i) {
  os << "Inode {\n"
     << "  inode_id: " << i.inode_id << "\n"
     << "  links: " << i.hardlink_cnt << "\n"
     << "  type: " << std::oct << i.mode.type_value << std::dec << "\n"
     << "  perms: " << std::oct << i.mode.perms_value << std::dec << "\n"
     << "  uid: " << i.uid << "\n"
     << "  gid: " << i.gid << "\n"
     << "  size: " << i.size << "\n"
     << "}";
  return os;
}

struct NextAction {
  enum ActionKindEnum {
    FEED_BLOCK,   // Feed me another block please.
    INODE,        // An inode was found in the stream && some DATA might
                  // follow if inode.size > 0.
    DATA,         // A DATA section corresponding to the previous INODE should
                  // be consumed directly from the stream. More than one DATA
                  // section in a row is possible.
    SKIP,         // A section to be skipped without further processing.
    DONE,         // The dump reached the end.
  } kind;

  union {
    struct Inode inode; /* If action == INODE */

    struct { /* If action == DATA */
      size_t size;    /* The useful payload size from the data. */
      size_t padding; /* Padding to discard afterward. */
    } data;

    struct { /* If action == SKIP */
      size_t size;         /* Size bytes to discard from the stream */
    } skip;
  };
};

struct FileEntry {
  std::string name;
  uint32_t parent_inode;
};

class StreamReader {
 public:
  virtual ~StreamReader() = default;
  StreamReader() {
  }

  void SetBlock(char block[BLOCK_SIZE]) {
    _block = block;
  }

  NextAction Next() {
    switch (_state) {
      case State::WAITING_FIRST_BLOCK: {
        SetState(State::READING_TAPE_HEADER);
        return NextAction{ NextAction::FEED_BLOCK };
      }
      case State::READING_TAPE_HEADER: {
        auto record = ValidateRecord();
        if (record.type != format::Record::Type::TAPE) {
          std::cerr << "Expecting TAPE record" << std::endl;
          abort();
        }
        SetState(State::READING_CLRI_HEADER);
        return NextAction{ NextAction::FEED_BLOCK };
      }
      case State::READING_CLRI_HEADER: {
        auto record = ValidateRecord();
        if (record.type != format::Record::Type::CLRI) {
          std::cerr << "Expecting CLRI record" << std::endl;
          abort();
        }
        // case fall through.
      }
      [[clang::fallthrough]];
      case State::SKIPPING_CLRI_MAP: {
        auto record = Record();
        WaitIfContinuationThenElse(State::SKIPPING_CLRI_MAP,
                                   State::READING_BITS_HEADER);
        return NextAction{ NextAction::SKIP,
          .skip.size = record.count * BLOCK_SIZE };
      }
      case State::READING_BITS_HEADER: {
        auto record = ValidateRecord();
        if (record.type != format::Record::Type::BITS) {
          std::cerr << "Expecting BITS record" << std::endl;
          abort();
        }
        // case fall through.
      }
      [[clang::fallthrough]];
      case State::SKIPPING_BITS_MAP: {
        auto record = Record();
        SetState(State::SKIPPING_BITS_MAP);
        WaitIfContinuationThenElse(State::SKIPPING_BITS_MAP,
                                   State::READING_ROOT_INODE);
        return NextAction{ NextAction::SKIP,
          .skip.size = record.count * BLOCK_SIZE };
      }
      case State::READING_ROOT_INODE: {
        auto record = ValidateRecord();
        if (record.type != format::Record::Type::INODE) {
          std::cerr << "Expecting INODE record" << std::endl;
          abort();
        }
        if (record.inode_id != 2) {
          std::cerr << "Expecting ROOD INODE" << std::endl;
          abort();
        }
        SetState(State::WAITING_DIRECTORY_CONTENT);
        _reverse_tree.emplace(2, FileEntry { .name = "/", .parent_inode = 0 });
        _current_inode = record.inode_id;
        _blocks_left = record.count;
        return NextAction{ NextAction::INODE, .inode = ReadInodeInfo(record) };
      }
      case State::WAITING_DIRECTORY_CONTENT: {
        SetState(State::READING_DIRECTORY_CONTENT);
        return NextAction{ NextAction::FEED_BLOCK };
      }
      case State::READING_DIRECTORY_CONTENT: {
        assert(_block != nullptr);
        for (const auto* begin = _block; begin < _block + BLOCK_SIZE;) {
          const auto& entry = reinterpret_cast<
              const format::DirectoryEntry&>(*begin);
          if (entry.inode_id == 0) {
            begin += entry.record_length;
            continue;
          }
          if (entry.name_len <= 2) {
            if (entry.name[0] == '.') {
              if (entry.name_len == 1 || entry.name[1] == '.') {
                begin += entry.record_length;
                continue;
              }
            }
          }
          _reverse_tree.emplace(entry.inode_id, FileEntry {
            .name = { entry.name, entry.name_len },
            .parent_inode = _current_inode,
          });
          begin += entry.record_length;
        }
        if (--_blocks_left == 0) {
          IfContinuationThenElse(State::READING_DIRECTORY_CONTENT,
                                 State::READING_VALIDATED_INODE);
        }
        return NextAction{ NextAction::FEED_BLOCK };
      }
      case State::WAITING_INODE: {
        SetState(State::READING_INODE);
        return NextAction{ NextAction::FEED_BLOCK };
      }
      case State::READING_INODE: {
        ValidateRecord();
        // case fall through.
      }
      [[clang::fallthrough]];
      case State::READING_VALIDATED_INODE: {
        auto record = Record();

        if (record.type == format::Record::Type::END) {
          SetState(State::DONE);
          return NextAction{ NextAction::SKIP,
            .skip.size = record.count * BLOCK_SIZE };
        }

        if (record.type != format::Record::Type::INODE) {
          std::cerr << "Expecting INODE record, got "
                    << static_cast<int>(record.type.ToHost())
                    << std::endl;
          abort();
        }
        const Mode mode = record.inode.mode;
        if (mode.type == Mode::Type::DIRECTORY) {
          SetState(State::WAITING_DIRECTORY_CONTENT);
          _current_inode = record.inode_id;
          _blocks_left = record.count;
        } else {
          if (record.inode.size) {
            _content_left = record.inode.size;
            SetState(State::SKIPPING_INODE_CONTENT);
          } else {
            SetState(State::WAITING_INODE);
          }
        }
        return NextAction{
          NextAction::INODE, .inode = ReadInodeInfo(record) };
      }
      case State::SKIPPING_INODE_CONTENT: {
        auto record = Record();
        WaitIfContinuationThenElse(State::SKIPPING_INODE_CONTENT,
                                   State::READING_VALIDATED_INODE);
        const uint64_t total_size = record.count * BLOCK_SIZE;
        const auto content_size = std::min(_content_left, total_size);
        _content_left -= content_size;
        return NextAction{ NextAction::DATA, .data.size = content_size,
          .data.padding = total_size - content_size };
      }
      case State::WAITING_CONTINUATION: {
        SetState(State::READING_CONTINUATION);
        return NextAction{ NextAction::FEED_BLOCK };
      }
      case State::READING_CONTINUATION: {
        auto record = ValidateRecord();
        if (record.type == format::Record::Type::ADDR) {
          SetState(_continuation_then);
        } else {
          SetState(_continuation_else);
        }
        return Next();
      }
      case State::DONE: {
        return NextAction{ NextAction::DONE };
      }
    }
    std::cerr << "STATE NOT IMPLEMENTED" << std::endl;
    abort();
  }

  /* Return all possible path for the given inode. Only regular files inodes can
   * return more than one entry (hardlinks). */
  std::vector<std::string> ResolvePaths(uint32_t inode) {
    assert(inode != 0);
    if (inode == 2) {
      return { "/" };
    }
    std::vector<std::string> r;
    auto range = _reverse_tree.equal_range(inode);
    for (auto it = range.first; it != range.second; ++it) {
      const FileEntry& file_entry = it->second;
      assert(file_entry.name != "." && file_entry.name != "..");
      r.push_back(_ResolveDirectoryPath(file_entry.parent_inode) +
                  file_entry.name);
    }
    return r;
  }

  std::vector<uint32_t> Parents(uint32_t inode) {
    std::vector<uint32_t> r;
    auto range = _reverse_tree.equal_range(inode);
    for (auto it = range.first; it != range.second; ++it) {
      const FileEntry& file_entry = it->second;
      assert(file_entry.name != "." && file_entry.name != "..");
      r.push_back(file_entry.parent_inode);
    }
    return r;
  }

  void PrintTree(std::ostream* os = &std::cout) {
    for (const auto& item : _reverse_tree) {
      for (const auto& p : ResolvePaths(item.first)) {
        *os << std::setw(10) << item.first << " - " << p << std::endl;
      }
    }
  }

 private:
  enum class State {
    WAITING_FIRST_BLOCK,
    READING_TAPE_HEADER,
    READING_CLRI_HEADER,
    SKIPPING_CLRI_MAP,
    READING_BITS_HEADER,
    SKIPPING_BITS_MAP,
    READING_ROOT_INODE,
    WAITING_DIRECTORY_CONTENT,
    READING_DIRECTORY_CONTENT,
    WAITING_INODE,
    READING_INODE,
    READING_VALIDATED_INODE,
    WAITING_CONTINUATION,
    READING_CONTINUATION,
    SKIPPING_INODE_CONTENT,
    DONE,
  };

  const format::Record& ValidateRecord() {
    assert(_block != nullptr);
    const auto& record = reinterpret_cast<const format::Record&>(*_block);
    if (!record.Checksum()) {
      std::cerr << "Invalid checksum" << std::endl;
      abort();
    }
    if (record.magic != format::MAGIC_NFS) {
      std::cerr << "Invalid MAGIC" << std::endl;
      abort();
    }
    return record;
  }

  const format::Record& Record() {
    assert(_block != nullptr);
    return reinterpret_cast<const format::Record&>(*_block);
  }

  uint64_t TimeValToUs(const format::TimeVal& tv) {
    return uint64_t(tv.sec) * uint64_t(1000000) + uint64_t(tv.usec);
  }

  Inode ReadInodeInfo(const format::Record& record) {
    return {
      .inode_id = record.inode_id,
      .hardlink_cnt = record.inode.hardlink_cnt,
      .mode = record.inode.mode,
      .uid = record.inode.uid_big ?
          record.inode.uid_big : record.inode.uid_small,
      .gid = record.inode.gid_big ?
          record.inode.gid_big : record.inode.gid_small,
      .size = record.inode.size,
      .atime_us = TimeValToUs(record.inode.atime),
      .mtime_us = TimeValToUs(record.inode.mtime),
      .ctime_us = TimeValToUs(record.inode.ctime),
    };
  }

  void SetState(State new_state) {
    _state = new_state;
  }

  void WaitIfContinuationThenElse(State then_state, State else_state) {
    _continuation_then = then_state;
    _continuation_else = else_state;
    SetState(State::WAITING_CONTINUATION);
  }

  void IfContinuationThenElse(State then_state, State else_state) {
    _continuation_then = then_state;
    _continuation_else = else_state;
    SetState(State::READING_CONTINUATION);
  }

  /* There is no hardlinks on directory except for '.' && '..'. But there
   * should be none of theses in _reverse_tree. We just have to recursively
   * follow up every parent directory until we reach root. */
  std::string _ResolveDirectoryPath(uint32_t inode) {
    assert(inode != 0);
    if (inode == 2) {
      return "/";
    }
    for (auto it = _reverse_tree.find(inode);
         it != _reverse_tree.end();
         it = _reverse_tree.find(inode)) {
      const FileEntry& file_entry = it->second;
      assert(file_entry.name != "." && file_entry.name != "..");
      return (_ResolveDirectoryPath(file_entry.parent_inode)
              + file_entry.name + '/');
    }
    return {};
  }

  State _state  = State::WAITING_FIRST_BLOCK;
  State _continuation_then;
  State _continuation_else;
  char* _block = nullptr;
  std::unordered_multimap<uint32_t, FileEntry> _reverse_tree;

  // Directory walking.
  uint32_t _current_inode;
  uint32_t _blocks_left;
  uint64_t _content_left;
};

}  // namespace dump

#endif  // CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_DUMP_READER_H_
