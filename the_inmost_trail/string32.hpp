#ifndef string32_hpp
#define string32_hpp

#include <engine/arctic_types.h>
#include <vector>

namespace arctic {

struct String32 {
  std::vector<Ui32> data;

  String32() {
  }

  String32(const Ui32* p, const Ui32* p1) {
    data.resize(p1 - p + 1);
    if (data.size() > 1) {
      memcpy(data.data(), p, (p1 - p) * sizeof(Ui32));
    }
    data[data.size() - 1] = 0;
  }

  virtual ~String32() {
  }
};

}  // namespace arctic

namespace std {
  template <>
  struct ::std::hash<arctic::String32> {
    size_t operator()(arctic::String32 const& x) const {
      size_t h = 37;
      for (const arctic::Ui32 i: x.data) {
         h = (h * 54059) ^ (static_cast<size_t>(i) * 76963);
      }
      return h; // or return h % 86969;
    }
  };
};

inline bool operator==(const arctic::String32& lhs, const arctic::String32& rhs) {
  return lhs.data.size() == rhs.data.size() &&
    memcmp(lhs.data.data(), rhs.data.data(), rhs.data.size() * sizeof(arctic::Ui32)) == 0;
}

inline bool operator!=(const arctic::String32& lhs, const arctic::String32& rhs) {
  return !(lhs == rhs);
}


#endif /* string32_hpp */
