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
#include <list>
#include <string>
#include <iostream>
#include <istream>

#include "./tar_writer.h"
#include "./dump_reader.h"

int main() {
  char blank[512];
  memset(blank, 0, sizeof blank);

  tar::StreamWriter tar;

  tar::StreamWriter::Result tar_result;
  bool copying_file = false;
  std::unordered_map<uint32_t, tar::File> dirs;

  dump::StreamReader reader;
  char buf[dump::BLOCK_SIZE];
  reader.SetBlock(buf);
  size_t offset = 0;
  while (42) {
    auto action = reader.Next();
    // std::cout << "action: " << action.kind << std::endl;
    switch (action.kind) {
      case dump::NextAction::FEED_BLOCK:
        // std::cout << "offset: " << offset << "\n";
        std::cin.read(buf, sizeof buf);
        if (!std::cin) {
          std::cerr << "Read error" << std::endl;
          abort();
        }
        offset += sizeof buf;
        break;
      case dump::NextAction::SKIP:
        // std::cerr << "SKIP, before @(" << offset << ") " << action.skip.size
        // << std::endl;
        for (auto remaining = action.skip.size; remaining > 0;) {
          auto amount = std::min(sizeof buf, remaining);
          std::cin.read(buf, amount);
          if (!std::cin) {
            std::cerr << "Read error" << std::endl;
            abort();
          }
          offset += amount;
          remaining -= amount;
        }
        // std::cerr << "SKIP, after @(" << offset << ")" << std::endl;
        break;
      case dump::NextAction::INODE: {
        // std::cerr << "Got " << action.inode << std::endl;
        const auto& inode = action.inode;

        if (inode.hardlink_cnt == 0) {
          break;
        }

        if (inode.inode_id == 2) {
          break;  // ignore root inode.
        }

        auto links = reader.ResolvePaths(inode.inode_id);
        std::string filename = "N/A";
        if (links.empty()
            && inode.mode.type != dump::Mode::Type::DIRECTORY) {
          std::cerr << "ABORT: Shit no names: " << inode << std::endl;
          abort();
        }
        if (!links.empty()) {
          filename = links.back();
        }

        tar::File f{
          .perms     = inode.mode.perms,
          .size      = 0,
          .uid       = inode.uid,
          .gid       = inode.gid,
          .mtime     = inode.mtime_us / 1000000.,
          .atime     = inode.atime_us / 1000000.,
          .ctime     = inode.ctime_us / 1000000.,
        };

        switch (inode.mode.type) {
          case dump::Mode::Type::SOCKET:
            std::cerr << "ignoring socket file " << filename << std::endl;
            goto done;
            break;
          case dump::Mode::Type::DIRECTORY:
            f.type = tar::FileType::DIRECTORY;
            break;
          case dump::Mode::Type::LINK:
            f.type = tar::FileType::SYMLINK;
            break;
          case dump::Mode::Type::REGULAR:
            f.type = tar::FileType::REGULAR;
            break;
          case dump::Mode::Type::CHAR_DEV:
            f.type = tar::FileType::CHAR_DEV;
            break;
          case dump::Mode::Type::BLOCK_DEV:
            f.type = tar::FileType::BLOCK_DEV;
            break;
          case dump::Mode::Type::FIFO:
            f.type = tar::FileType::FIFO;
            break;
        }

        if (inode.mode.type == dump::Mode::Type::DIRECTORY) {
          if (links.size()) {
            f.filename = links.back() + "/";
          } else {
            f.filename = "NOT_KNOWN";
          }
        } else {
          assert(links.size());
          f.filename = links.back();
        }

        switch (inode.mode.type) {
          case dump::Mode::Type::SOCKET:
            break;
          case dump::Mode::Type::DIRECTORY:
            break;
          case dump::Mode::Type::LINK:
            // TODO read data as link destination.
            std::cerr << "symlink !implemented " << filename << std::endl;
            goto done;
            break;
          case dump::Mode::Type::REGULAR:
            f.size = inode.size;
            copying_file = true;
            break;
          case dump::Mode::Type::FIFO:
            std::cerr << "fifo !implemented " << filename << std::endl;
            goto done;
            break;
          case dump::Mode::Type::CHAR_DEV:
          case dump::Mode::Type::BLOCK_DEV:
            std::cerr << "dev files !implemented " << filename << std::endl;
            goto done;
            break;
        }

        // Per netapp documentation:
        // https://library.netapp.com/ecmdocs/ECMP1368865/html/GUID-34EFEE5F-E97D-4CAA-8E7E-93AE65E486D9.html
        // we always get directories before files content, so we can resolve all
        // the possible paths (hardlinks) of a file when we encounter one. But
        // directories themselves are not necessarily in a perfectly top-down
        // order, so we must postpone writing out directory entries before we
        // get them all. Additionally, we wait until we encounter a file of the
        // directory before writing it out, so we somewhat keep directories and
        // files together (not required by tar, but its somewhat nice to do for
        // extraction locality, and loosely follow the filesystem hierarchy).
        // This also means that empty directories, are all written out at the
        // end of the tar archive.
        if (inode.mode.type == dump::Mode::Type::DIRECTORY) {
          std::cerr << "ready to use directory entry #" << inode.inode_id
            << " - " << filename << std::endl;
          dirs.insert(std::make_pair(inode.inode_id, f));
        } else {
          for (auto parent_inode : reader.Parents(inode.inode_id)) {
            auto it = dirs.find(parent_inode);
            if (it != dirs.end()) {
              auto links = reader.ResolvePaths(parent_inode);
              if (!links.empty()) {
                const auto& filename = links.back();
                std::cerr << "flushing directory entry #" << parent_inode
                  << " - " << filename << std::endl;
                it->second.filename = filename;
                auto tar_result = tar.AddFile(it->second);
                std::cout.write(tar_result.buffer.data(),
                    tar_result.buffer.size());
                dirs.erase(it);
              } else {
                std::cerr << "directory !yet resolved #" << parent_inode
                  << std::endl;
              }
            }
          }

          tar_result = tar.AddFile(f);
          std::cout.write(tar_result.buffer.data(), tar_result.buffer.size());
        }

        if (!links.empty()) {
          links.pop_back();
        }

        if (!links.empty()) {
          std::cerr << "hardlinks !implemented" << filename << std::endl;
        }
done:
        break;
      }
      case dump::NextAction::DATA:
        // std::cerr << "action data: "
                  // << action.data.size
                  // << " " << action.data.content_size
                  // << std::endl;
        for (auto remaining = action.data.size; remaining > 0;) {
          auto amount = std::min(sizeof buf, remaining);
          std::cin.read(buf, amount);
          if (!std::cin) {
            std::cerr << "Read error" << std::endl;
            abort();
          }

          if (copying_file) {
            if (tar_result.content_size < remaining) {
              std::cerr << "Dafuk you didn't read enough! "
                << remaining << "/" << tar_result.content_size
                << std::endl;
              abort();
            }
            std::cout.write(buf, amount);
            tar_result.content_size -= amount;

            if (tar_result.content_size == 0) {
              std::cout.write(blank, tar_result.padding);
              copying_file = 0;
            }
          }

          offset += amount;
          remaining -= amount;
        }
        for (auto remaining = action.data.padding; remaining > 0;) {
          auto amount = std::min(sizeof buf, remaining);
          std::cin.read(buf, amount);
          if (!std::cin) {
            std::cerr << "Read error" << std::endl;
            abort();
          }
          offset += amount;
          remaining -= amount;
        }
        break;
      case dump::NextAction::DONE:
        // std::cerr << "DONE (" << offset << ")" << std::endl;
        // reader.PrintTree(std::cerr);
        std::cerr << "DONE (" << offset << ")" << std::endl;
        {
          for (auto dir : dirs) {
            auto links = reader.ResolvePaths(dir.first);
            if (links.size()) {
              const auto& filename = links.back();
              std::cerr << "flushing directory entry #" << dir.first
                << " - " << filename << std::endl;
              dir.second.filename = filename;
              auto tar_result = tar.AddFile(dir.second);
              std::cout.write(tar_result.buffer.data(),
                  tar_result.buffer.size());
            } else {
              std::cerr << "directory entry never resolved #" << dir.first
                << std::endl;
            }
          }
        }
        {
          auto r = tar.Close();
          while (r.padding) {
            const auto to_write = std::min(r.padding, sizeof blank);
            std::cout.write(blank, to_write);
            r.padding -= to_write;
          }
        }
        return 0;
    }
  }
}
