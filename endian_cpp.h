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
#ifndef CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_ENDIAN_CPP_H_
#define CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_ENDIAN_CPP_H_

#include <endian.h>
#include <cstdint>

template <typename T, typename RT = T>
    struct BigEndianValue {
      T bvalue;

      static_assert(sizeof bvalue == sizeof (RT), "T and RT must be of equal size");
      static_assert(sizeof bvalue == 2 || sizeof bvalue == 4
                    || sizeof bvalue == 8, "T must be of size 2, 4 or 8");

      T ToHost() const {
        auto r = *reinterpret_cast<const RT*>(&bvalue);
        switch (sizeof bvalue) {
          case 2: r = be16toh(r); break;
          case 4: r = be32toh(r); break;
          case 8: r = be64toh(r); break;
        }
        return *reinterpret_cast<T*>(&r);
      }

      operator T () const {
        return ToHost();
      }

      T operator*() const {
        return ToHost();
      }
    };

struct BigEndianStruct {
  using buint16_t = BigEndianValue<::uint16_t>;
  using buint32_t = BigEndianValue<::uint32_t>;
  using buint64_t = BigEndianValue<::uint64_t>;
  using bint16_t  = BigEndianValue<::int16_t>;
  using bint32_t  = BigEndianValue<::int32_t>;
  using bint64_t  = BigEndianValue<::int64_t>;
};

#endif  // CORP_STORAGE_SHREC_MOIRA_DUMP2TAR_ENDIAN_CPP_H_
