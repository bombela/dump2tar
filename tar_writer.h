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
#ifndef CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_TAR_WRITER_H_
#define CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_TAR_WRITER_H_

#include "./tar_format.h"


namespace tar {

enum class FileType {
  REGULAR,
  LINK,
  SYMLINK,
  CHAR_DEV,
  BLOCK_DEV,
  DIRECTORY,
  FIFO,
};

struct File {
  FileType    type;
  Permissions perms;

  std::string filename;
  std::string linkname;

  uint32_t    uid;
  uint32_t    gid;

  std::string username;
  std::string groupname;

  uint64_t    size;

  double      mtime;
  double      ctime;
  double      atime;

  uint32_t    device_major;
  uint32_t    device_minor;
};

class StreamWriter {
 public:
  StreamWriter() {}

  struct Result {
    std::vector<char> buffer;        // buffer to write out.
    size_t            content_size;  // content to write out.
    size_t            padding;       // zeroes to write out.
  };

  Result AddFile(const File& file) {
    std::vector<char> buffer;

    buffer.reserve(BLOCK_SIZE*3);
    buffer.resize(BLOCK_SIZE);
    auto& pax_record = reinterpret_cast<tar::format::FileHeader&>(buffer[0]);
    memset(&pax_record, '\0', sizeof pax_record);

    pax_record.filename = ("././pax_entry_" +
        std::to_string(_pax_entry_counter++));
    pax_record.perms = Permissions{ 0600 };
    pax_record.type = format::FileHeader::Type::PAX_ATTR;

    tar::format::FileHeader file_record;
    memset(&file_record, '\0', sizeof file_record);

#define ADD_PAX_ENTRY(attr, pax_name) do { \
    if (file_record.attr.Set(file.attr) != format::FitResult::FIT_ALL) { \
      AddPaxEntry(pax_name, file.attr, &buffer); } } while (0)

    ADD_PAX_ENTRY(filename, "path");
    file_record.perms = file.perms;
    ADD_PAX_ENTRY(uid, "uid");
    ADD_PAX_ENTRY(gid, "gid");
    ADD_PAX_ENTRY(size, "size");

    if (file.mtime) {
      if (file.mtime == uint64_t(file.mtime)) {
        // Second only precision.
        ADD_PAX_ENTRY(mtime, "mtime");
      } else {
        // Sub-second precision, only pax extension can handle it.
        file_record.mtime = file.mtime;
        AddPaxEntry("mtime", file.mtime, &buffer);
      }
    }
    if (file.ctime) AddPaxEntry("ctime", file.ctime, &buffer);
    if (file.atime) AddPaxEntry("atime", file.atime, &buffer);

    using type = format::FileHeader::Type;
    switch (file.type) {
      case FileType::REGULAR: file_record.type = type::REGULAR; break;
      case FileType::LINK: file_record.type = type::LINK; break;
      case FileType::SYMLINK: file_record.type = type::SYMLINK; break;
      case FileType::CHAR_DEV: file_record.type = type::CHAR_DEV; break;
      case FileType::BLOCK_DEV: file_record.type = type::BLOCK_DEV; break;
      case FileType::DIRECTORY: file_record.type = type::DIRECTORY; break;
      case FileType::FIFO: file_record.type = type::FIFO; break;
    }
    ADD_PAX_ENTRY(linkname, "linkpath");
    ADD_PAX_ENTRY(username, "uname");
    ADD_PAX_ENTRY(groupname, "gname");
    ADD_PAX_ENTRY(device_major, "SCHILY.devmajor");
    ADD_PAX_ENTRY(device_minor, "SCHILY.devmajor");

#undef ADD_PAX_ENTRY

    const auto pax_size = buffer.size() - sizeof pax_record;
    file_record.Finalize();

    if (pax_size) {
      pax_record.size = pax_size;
      pax_record.Finalize();

      const auto padding = BLOCK_SIZE - (buffer.size() % BLOCK_SIZE);
      const auto extra_size = padding + sizeof file_record;
      buffer.resize(buffer.size() + extra_size);
      auto end = &buffer[buffer.size() - extra_size];
      memset(end, '\0', padding);
      memcpy(end + padding, &file_record, sizeof file_record);
    } else {
      memcpy(buffer.data(), &file_record, sizeof file_record);
      buffer.resize(sizeof file_record);
    }

    return {
      .buffer = std::move(buffer),
      .content_size = file.size,
      .padding = (BLOCK_SIZE-1) - (file.size + BLOCK_SIZE - 1) % BLOCK_SIZE,
    };
  }

  Result Close() {
    return {
      .padding = BLOCK_SIZE * 2,
    };
  }

 private:
  size_t _pax_entry_counter = 0;

  template <typename T>
    void AddPaxEntry(const char* key, T&& value, std::vector<char>* buffer) {
      const tar::format::PaxEntry<T> pax_entry { key, std::forward<T>(value) };
      pax_entry.Serialize(buffer);
    }
};

}  // namespace tar


#endif  // CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_TAR_WRITER_H_
