#ifndef CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_TAR_FORMAT_H_
#define CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_TAR_FORMAT_H_

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cassert>

#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

#include "./common.h"

namespace tar {

constexpr const unsigned BLOCK_SIZE = 512;

namespace format {

constexpr const char POSIX_USTAR_MAGIC[6] = { "ustar" };

enum class FitResult {
  FIT_ALL,        // All good, it fits.
  FIT_OVERWRITE,  // It fits if we overwrite the final '\0'.
  FIT_OVERFLOW,   // Doesn't fit at all.
};

template <unsigned SIZE>
struct Field {
  char raw[SIZE];

  void Fill(char c) {
    memset(raw, c, sizeof raw);
  }

 protected:
  static bool _IsPadding(char c) {
    switch (c) {
      case ' ': return true;
      case '\0': return true;
    }
    return false;
  }
  std::pair<const char*, const char*> _Range() const {
    const char* begin = raw;
    const char* end = raw + sizeof raw;
    while (begin != end && _IsPadding(*begin)) { ++begin; }
    while (end != begin && _IsPadding(*end)) { --end; }
    return { begin, end };
  }
};

namespace details {

template <int64_t N, int64_t M>
struct pow {
  enum { value = pow<N, M - 1>::value * N };
};

template <int64_t N>
struct pow<N, 1> {
  enum { value = N };
};

}  // namespace details

template <unsigned SIZE, typename T = int64_t>
struct IntField: public Field<SIZE> {
  typedef T value_t;

  static constexpr const int BASE = 8;

  operator value_t () {
    if (std::is_unsigned<value_t>::value) {
      return strtou64(this->raw, nullptr, BASE);
    } else {
      return strto64(this->raw, nullptr, BASE);
    }
  }

  static FitResult DoesItFit(value_t v) {
    static_assert(SIZE > 3, "SIZE must be > 3");
    if (v < 0) {
      if (v > -(details::pow<BASE, SIZE-2>::value)) {
        return FitResult::FIT_ALL;
      } else if (v > -(details::pow<BASE, SIZE-1>::value)) {
        return FitResult::FIT_OVERWRITE;
      } else {
        return FitResult::FIT_OVERFLOW;
      }
    } else {
      if (v < (details::pow<BASE, SIZE-1>::value)) {
        return FitResult::FIT_ALL;
      } else if (v < (details::pow<BASE, SIZE>::value)) {
        return FitResult::FIT_OVERWRITE;
      } else {
        return FitResult::FIT_OVERFLOW;
      }
    }
  }

  FitResult Set(value_t v) {
    const auto begin = this->raw;
    auto end = this->raw + sizeof this->raw;

    const FitResult fit = DoesItFit(v);
    switch (fit) {
      case FitResult::FIT_ALL: *--end = '\0'; break;
      case FitResult::FIT_OVERWRITE: break;
      case FitResult::FIT_OVERFLOW: return fit;
    }

    auto int_val = static_cast<int64_t>(v);
    if (int_val < 0) {
      do { *--end = '0' - int_val % BASE; } while ((int_val /= BASE) != 0);
      memset(begin + 1, '0', end - begin - 1);
      *begin = '-';
    } else {
      do { *--end = '0' + int_val % BASE; } while ((int_val /= BASE) != 0);
      memset(begin, '0', end - begin);
    }
    return fit;
  }

  IntField& operator=(value_t v) {
    if (Set(v) == FitResult::FIT_OVERFLOW) {
      std::cerr << "OUT OF RANGE value -> str conversion" << std::endl;
      abort();
    }
    return *this;
  }
};

template <unsigned SIZE>
struct TextField: public Field<SIZE> {
  operator std::string() const {
    auto range = this->_Range();
    return std::string(range.first, range.second);
  }

  FitResult Set(const std::string& s) {
    const auto size = s.size();
    if (size > sizeof this->raw) {
      return FitResult::FIT_OVERFLOW;
    }
    memcpy(this->raw, s.data(), size);
    memset(this->raw + size, '\0', sizeof this->raw - size);
    if (this->raw[sizeof this->raw - 1]) {
      return FitResult::FIT_OVERWRITE;
    }
    return FitResult::FIT_ALL;
  }

  TextField& operator=(const std::string& s) {
    if (Set(s) == FitResult::FIT_OVERFLOW) {
      std::cerr << "OUT OF RANGE TextField" << std::endl;
      abort();
    }
    return *this;
  }
};

struct FileHeader {
  TextField<100>           filename;
  IntField<8, Permissions> perms;
  IntField<8>              uid;
  IntField<8>              gid;
  IntField<12>             size;
  IntField<12>             mtime;
  IntField<8>              checksum;

  enum class Type: char {
    REGULAR   = '0',
    LINK      = '1',
    SYMLINK   = '2',
    CHAR_DEV  = '3',
    BLOCK_DEV = '4',
    DIRECTORY = '5',
    FIFO      = '6',
    PAX_ATTR  = 'x',
  };

  Type           type;
  TextField<100> linkname;
  TextField<6>   magic;
  TextField<2>   version;
  TextField<32>  username;
  TextField<32>  groupname;
  IntField<8>    device_major;
  IntField<8>    device_minor;
  TextField<155> filename_prefix;
  char           spare[12];

  void Finalize() {
    magic = POSIX_USTAR_MAGIC;
    version = "00";
    checksum.Fill(' ');
    const auto& as_uint8 = reinterpret_cast<
        const uint8_t(&)[sizeof *this / sizeof (uint8_t)]
        >(*this);
    uint64_t sum = 0;
    for (auto v : as_uint8) {
      sum += v;
    }
    checksum = sum;
  }
};
static_assert(sizeof(FileHeader) == BLOCK_SIZE, "Wrong size for FileHeader");

template <typename T>
struct PaxEntry {
  explicit PaxEntry(const char* k): key(k) {
    assert(k);
  }

  template <typename U>
  PaxEntry(const char* k, U&& v): key(k), value(std::forward<U>(v)) {
    assert(k);
  }

  const char* key;
  T value;

  void Serialize(std::vector<char>* buffer) const {
    std::ostringstream os;
    os << " " << key << "=" << std::fixed << value << "\n";

    auto size = os.str().size();
    int nb_digit = 1;

    if (size >= 9) {
      switch (size) {
        case   9: nb_digit = 2; break;
        case  98: nb_digit = 3; break;
        case 997: nb_digit = 4; break;
        default:
           nb_digit = static_cast<int>(log10(size)) + 1;
           if (size == (pow(10, nb_digit) - nb_digit)) {
             nb_digit += 1;
           }
      }
    }
    size += nb_digit;

    buffer->resize(buffer->size() + size);
    auto end = &(*buffer)[buffer->size() - size];

    const auto size_str = std::to_string(size);
    memcpy(end, size_str.data(), size_str.size());
    memcpy(end + size_str.size(), os.str().data(), os.str().size());
  }
};

}  // namespace format
}  // namespace tar

#endif  // CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_TAR_FORMAT_H_
