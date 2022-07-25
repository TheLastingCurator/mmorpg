// Copyright (c) <year> Your name

#include "engine/easy.h"
#include "engine/arctic_platform_tcpip.h"
#include "engine/unicode.h"
#include "engine/decorated_frame.h"
#include <map>
#include <unordered_map>
#include "string32.hpp"
#include "script.hpp"

using namespace arctic;  // NOLINT

const char *kNetworkServerAddress = "176.126.85.38";
constexpr uint16_t kNetworkPort = 27000;

constexpr Ui32 kAvatarCount = 100'000;

constexpr Ui32 kUiiIdxBits = 20;
constexpr Ui32 kUiiUidBits = 32 - kUiiIdxBits;

constexpr Ui32 kUiiIdxMask = (Ui32(1) << kUiiIdxBits) - 1;
constexpr Ui32 kUiiUidMask = ((Ui32(1) << kUiiUidBits) - 1);

constexpr Ui32 kUiiUidStep = (Ui32(1) << kUiiIdxBits);

struct Uii {
  Ui32 value;
  Ui32 GetIdx() const {
    return (value & ((Ui32(1) << kUiiIdxBits) - 1));
  }
  Ui32 GetUid() const {
    return (value >> kUiiIdxBits);
  }
  void Set(Ui32 idx, Ui32 uid) {
    value = ((idx & kUiiIdxMask) | ((uid & kUiiUidMask) << kUiiIdxBits));
  }
  void NextUid() {
    value += kUiiUidStep;
  }
  Uii(Ui32 idx, Ui32 uid) {
    Set(idx, uid);
  }
  Uii()
    : value(Ui32(-1)) {
  }
  bool operator==(const Uii& right) const {
    return value == right.value;
  }
  bool operator!=(const Uii& right) const {
    return value != right.value;
  }
};

const Uii kInvalidUii = Uii(kUiiIdxMask, kUiiUidMask);

class MapCell {
  Ui32 items_ : kUiiIdxBits;
  Ui32 type_ : 32 - kUiiIdxBits;
 public:
  MapCell()
    : items_(kUiiIdxMask)
    , type_(0) {
  }
  void SetItems(Ui32 items) {
    items_ = items;
  }
  Ui32 GetItems() {
    return items_;
  }
};
static_assert(sizeof(MapCell) == 4, "sizeof(MapCell) must be 4, error!");

template <class T>
class UniqueItemVector;

class UniqueItemBase;

class UniqueItemBase {
 protected:
  UniqueItemBase *next_ = nullptr; // Either next free or next on map
  UniqueItemBase *prev_ = nullptr; // Either next free or next on map
  MapCell *cell_ = nullptr;
 public:
  Uii uii;
  template <class T> friend class UniqueItemVector;
  void AddToListBefore(UniqueItemBase *p) {
    Check(p->prev_ == nullptr, "UniqueItemBase can't AddToList item that is already in a list!");
    Check(p->next_ == nullptr, "UniqueItemBase can't AddToList item that is already in a list!");
    p->prev_ = this->prev_;
    p->next_ = this;
    if (p->prev_) {
      p->prev_->next_ = p;
    }
    this->prev_ = p;
  }
  void SetCell(MapCell *cell) {
    cell_ = cell;
  }
  MapCell* GetCell() {
    return cell_;
  }

  template <class T> friend class UniqueItemVector;

  template <class T>
  void AddToCell(MapCell *cell, UniqueItemVector<T> &v) {
    cell_ = cell;
    if (cell_->GetItems() != kInvalidUii.GetIdx()) {
      UniqueItemBase* list = &v[cell->GetItems()];
      list->AddToListBefore(this);
    }
    cell->SetItems(uii.GetIdx());
  }

  UniqueItemBase* RemoveFromListGetNext() {
    UniqueItemBase *next = next_;
    if (next_) {
      next_->prev_ = prev_;
      next_ = nullptr;
    }
    if (prev_) {
      prev_->next_ = next;
      prev_ = nullptr;
    } else {
      if (cell_) {
        Uii uii = kInvalidUii;
        if (next) {
          uii = next->uii;
        }
        cell_->SetItems(uii.GetIdx());
      }
    }
    return next;
  }

};

template <class T>
class UniqueItemVector {
 protected:
  static_assert(std::is_base_of<UniqueItemBase, T>::value, "T must derive from UniqueItemBase");
  std::vector<T> items_;
  Ui64 size_ = 0;
  UniqueItemBase *free_ = nullptr;
  Ui64 next_uid_ = 1;
 public:
  void Prepare(Ui64 capacity) {
    Check(items_.size() == 0, "UniqueItemVector must be prepared only once!");
    items_.resize(capacity);
  }

  T& operator[](Ui64 idx) {
    Check(idx < size_, "UniqueItemVector can't access item with idx out of bounds.");
    return items_[idx];
  }

  T* TryGetItem(Uii uii) {
    if (items_.size() < uii.GetIdx()) {
      if (items_[uii.GetIdx()].uii == uii) {
        return &items_[uii.GetIdx()];
      }
    }
    return nullptr;
  }

  Ui64 Size() {
    return size_;
  }

  void FreeItem(Uii uii) {
    Check(uii.GetIdx() < size_, "UniqueItemVector can't free item with idx out of bounds.");
    Check(items_[uii.GetIdx()].uii == uii, "UniqueItemVector can't free item, uii mismatch.");
    Check(items_[uii.GetIdx()].next_ == nullptr, "UniqueItemVector can't free item, item is still on map or double free attempted, next_ != 0.");
    Check(items_[uii.GetIdx()].prev_ == nullptr, "UniqueItemVector can't free item, item is still on map or double free attempted. prev_ != 0.");
    items_[uii.GetIdx()].next_ = free_;
    if (free_) {
      free_->prev_ = &items_[uii.GetIdx()];
    }
    free_ = &items_[uii.GetIdx()];
    free_->uii.NextUid();
  }

  Uii AddItem() {
    Uii uii = kInvalidUii;
    if (free_) {
      Check(free_->uii.GetIdx() < size_, "UniqueItemVector can't add item, idx corruption detected (oob).");
      Check(&items_[free_->uii.GetIdx()] == free_, "UniqueItemVector can't add item, idx corruption detected (wrong).");
      free_->uii.NextUid();
      uii = free_->uii;
      free_ = std::exchange(free_->next_, nullptr);
      if (free_) {
        free_->prev_ = nullptr;
      }
    } else if (size_ + 1 < items_.size()) {
      items_[size_]->next_ = nullptr;
      items_[size_]->prev_ = nullptr;
      items_[size_]->uii.Set(size_, 0);
      uii = items_[size_]->uii;
      ++size_;
    }
    return uii;
  }
};

class UiiQueue {
  std::vector<Uii> queue_;
  std::vector<Ui64> queue_position_;
  size_t front_idx_ = 0;
  size_t back_idx_ = 0;
  size_t length_ = 0;
  size_t capacity_ = 0;
 public:

  void Prepare(Ui64 capacity) {
    Check(queue_.size() == 0, "UiiQueue must be prepared only once!");
    queue_.resize(capacity);
    queue_position_.resize(capacity);
    for (size_t i = 0; i < capacity; ++i) {
      queue_position_[i] = capacity;
    }
    capacity_ = capacity;
  }

  void PushBack(Uii uii) {
    Check(uii.GetIdx() < capacity_, "UiiQueue cant PushBack item with idx out of bounds!");
    if (queue_position_[uii.GetIdx()] == capacity_) {
      Check(length_ < capacity_, "UiiQueue cant PushBack item, capacity reached!");
      queue_position_[uii.GetIdx()] = back_idx_;
      queue_[back_idx_] = uii;
      ++back_idx_;
      if (back_idx_ >= capacity_) {
        back_idx_ = 0;
      }
      ++length_;
      return;
    }
    Ui64 pos = queue_position_[uii.GetIdx()];
    Check(pos < capacity_, "UiiQueue can't PushBack, queue_position_ is out of bounds");
    if (queue_[pos].GetUid() != uii.GetUid()) {
      queue_[pos] = uii;
    }
  }

  size_t Length() {
    return length_;
  }

  Uii PreviewFront() {
    Check(length_, "UiiQueue cant PreviewFront, it is empty!");
    return queue_[front_idx_];
  }

  Uii PopFront() {
    Check(length_, "UiiQueue cant PopFront, it is empty!");
    Uii uii = queue_[front_idx_];
    ++front_idx_;
    if (front_idx_ >= capacity_) {
      front_idx_ = 0;
    }
    --length_;
    queue_position_[uii.GetIdx()] = capacity_;
    return uii;
  }
};

class Map {
  Ui32 width_;
  Ui32 height_;
  std::vector<MapCell> cells_;
 public:

  Map(Ui32 width, Ui32 height)
    : width_(width)
    , height_(height)
    , cells_(width_*height_) {
  }

  MapCell& At(Ui32 x, Ui32 y) {
    return cells_[x*width_ + y];
  }

  const MapCell& At(Ui32 x, Ui32 y) const {
    return cells_[x*width_ + y];
  }
};

struct Character;
struct Arrow;



enum ChState {
  kChStateIdle = 0,
  kChStateWalkToPoint,
  kChStateWalkToItem,
  kChStateWalkToAttack,
  kChStatePlayAttack,
  kChStatePlayDying,
  kChStateDead,
  kChStateCount
};

enum ChDir {
  kChDirRight = 0,
  kChDirLeft,
  kChDirCount
};

struct NetChFrame {
  Ui64 uid;
  Ui8 state;
  Ui8 dir;
};

struct NetFrameHeader {
  double time;
  Ui32 ch_frame_count;
};

enum PlayerCmd {
  kPlayerCmdWalkToPoint = 0,
  kPlayerCmdInteractWithItem,
  kPlayerCmdAttack
};

struct NetPlayerCmd {
  Uii my_uii;
  PlayerCmd cmd;
  Vec2Si32 pos;
  Uii uii;
};

enum CharacterAnimation {
  kCharacterAnimationWalk = 0,
  kCharacterAnimationAttack,
  kCharacterAnimationIdle,
  kCharacterAnimationDie,
  kCharacterAnimationCount
};

std::vector<std::string> g_character_animation_name {
  "walk",
  "attack",
  "idle",
  "die"
};

std::vector<std::string> g_character_direction_name {
  "r",
  "l"
};

class Avatar : public UniqueItemBase {
 public:
  Ui32 connection_idx = std::numeric_limits<Ui32>::max();
  Ui8 unit_type;
  ChState state;
  Vec2Si32 begin_pos;
  Vec2Si32 end_pos;
  Ui32 begin_tick;
  Ui32 end_tick;
  Uii target_uii;
};

enum ConnState {
  kConnStateInvalid = 0,
  kConnStateJustConnected,
  kConnStateRegistered
};

constexpr Si32 kConnBufferSize = 516;

enum MsgType {
  kMsgTypeRegistrationRequest = 0,
  kMsgTypeRegistrationResponse = 1,
  kMsgTypePing = 2,
  kMsgTypePong = 3,
  kMsgTypePlayerCmdWalkToPoint = 4,
  kMsgTypePlayerCmdInteractWithItem = 5,
  kMsgTypePlayerCmdAttack = 6,
  kMsgTypeAvatarState = 7,
  kMsgTypeCount
};

#pragma pack(push,1)
struct MsgHeader {
  Ui8 msg_size;
  Ui8 msg_type;
};
struct MsgRegistrationRequest {
  Ui32 protocol_version;
};
struct MsgRegistrationResponse {
  enum Result {
    kResultSuccess = 0,
    kResultUnknownError = 1,
    kResultProtocolVersionMismatch = 2
  };
  Ui32 protocol_version;
  Ui32 result;
  Ui32 avatar_uii;
};
struct MsgPing {
  double c_time;
};
struct MsgPong {
  double c_time;
  double s_time;
};
struct MsgPlayerCmdWalkToPoint {
  Ui32 avatar_uii;
  Ui32 x;
  Ui32 y;
};
struct MsgPlayerCmdInteractWithItem {
  Ui32 avatar_uii;
  Ui32 item_uii;
};
struct MsgPlayerCmdAttack {
  Ui32 avatar_uii;
  Ui32 target_uii;
};
struct MsgAvatarState {
  Uii uii;
  Ui8 unit_type;
  Ui8 state;
  Ui32 begin_tick;
  Ui32 begin_x;
  Ui32 begin_y;
  Ui16 duration_ticks;
  Si16 end_offset_x;
  Si16 end_offset_y;
  Uii target_uii;
};
#pragma pack(pop)

Ui8 g_msg_size[kMsgTypeCount] = {
  sizeof(MsgRegistrationRequest),
  sizeof(MsgRegistrationResponse),
  sizeof(MsgPing),
  sizeof(MsgPong),
  sizeof(MsgPlayerCmdWalkToPoint),
  sizeof(MsgPlayerCmdInteractWithItem),
  sizeof(MsgPlayerCmdAttack),
  sizeof(MsgAvatarState)
};

class NetServerState;

class Connection {
  ConnectionSocket socket;
  ConnState state = kConnStateInvalid;
  Si32 buffer_used = 0;
  char buffer[kConnBufferSize];
  UiiQueue queue;
  Uii uii;
  Ui32 idx = 0;
  char outgoing[kConnBufferSize];
  Si32 outgoing_used = 0;
  Si32 outgoing_sent = 0;

 public:
  Uii GetUii() {
    return uii;
  }
  void SetIdx(Ui32 in_idx) {
    idx = in_idx;
  }

  void Init(ConnectionSocket &&in_socket, Ui32 in_idx) {
    socket = std::move(in_socket);
    state = kConnStateJustConnected;
    queue.Prepare(kAvatarCount);
    idx = in_idx;
  }

  void HandleMsgRegistrationRequest() {
    MsgRegistrationRequest m;
    memcpy(&m, buffer + sizeof(MsgHeader), sizeof(m));
  }
  void HandleMsgPing() {
    MsgPing m;
    memcpy(&m, buffer + sizeof(MsgHeader), sizeof(m));
  }
  void HandleMsgPlayerCmdWalkToPoint() {
    MsgPlayerCmdWalkToPoint m;
    memcpy(&m, buffer + sizeof(MsgHeader), sizeof(m));
  }
  void HandleMsgPlayerCmdInteractWithItem() {
    MsgPlayerCmdInteractWithItem m;
    memcpy(&m, buffer + sizeof(MsgHeader), sizeof(m));
  }
  void HandleMsgPlayerCmdAttack() {
    MsgPlayerCmdAttack m;
    memcpy(&m, buffer + sizeof(MsgHeader), sizeof(m));
  }

  void PrepareOutgoingData(NetServerState *server);
  void Update(NetServerState *server);

  bool IsValid() {
    return socket.IsValid();
  }
};

class NetServerState {
 public:
  UniqueItemVector<Avatar> avatars;
  Map map;
  std::deque<Connection> connections;
  ListenerSocket listener_socket;

  void UpdateServer() {
    if (!listener_socket.IsValid()) {
      *Log() << Time() << " UpdateServer listener_socket is invalid, starting a new one";
      listener_socket = ListenerSocket(AddressFamily::kIpV4, SocketProtocol::kTcp);
      if (!listener_socket.IsValid()) {
        *Log() << "UpdateServer ListenerSocket: " << listener_socket.GetLastError();
        return;
      }
      SocketResult res = listener_socket.SetSoLinger(false, 0);
      if (res != kSocketOk) {
        *Log() << "UpdateServer SetSoLinger: " << listener_socket.GetLastError();
        listener_socket = ListenerSocket();
        return;
      }
      res = listener_socket.Bind(kNetworkServerAddress, kNetworkPort);
      if (res != kSocketOk) {
        *Log() << "UpdateServer Bind: " << listener_socket.GetLastError();
        listener_socket = ListenerSocket();
        return;
      }
      res = listener_socket.SetSoNonblocking(true);
      if (res != kSocketOk) {
        *Log() << "UpdateServer SetSoNonblocking: " << listener_socket.GetLastError();
        listener_socket = ListenerSocket();
        return;
      }
    }

    // Try accepting a new connection
    ConnectionSocket socket = listener_socket.Accept();
    if (socket.IsValid()) {
      *Log() << Time() << " UpdateServer accepted a new connection";

      SocketResult res = socket.SetSoNonblocking(true);
      if (res != kSocketOk) {
        *Log() << "UpdateServer SetSoNonblocking error: " << socket.GetLastError();
      } else {
        connections.emplace_back();
        Connection &rec = connections.back();
        rec.Init(std::move(socket), Si32(connections.size()) - 1);
      }
    } else {
      //*Log() << Time() << " UpdateServer no new connections";
    }

    for (Si32 idx = 0; idx < connections.size(); ++idx) {
      Connection &rec = connections[idx];
      if (rec.IsValid()) {
        rec.Update(this);
      } else {
        // TODO: handle the disconnected players character in a way that makes sense
        Avatar *avatar = avatars.TryGetItem(rec.GetUii());
        if (avatar) {
          avatar->connection_idx = std::numeric_limits<Ui32>::max();
        }
        if (idx != connections.size() - 1) {
          rec = std::move(connections[connections.size() - 1]);
          rec.SetIdx(idx);
          avatar = avatars.TryGetItem(rec.GetUii());
          if (avatar) {
            avatar->connection_idx = idx;
          }
        }
        connections.pop_back();
      }
    }

  }
};

const Si32 kMaxClientCmdBucketSize = 4;
const double kClientCmdBucketFillDelay = 0.25;

class NetClientState {
  UniqueItemVector<Avatar> avatars;

  ConnectionSocket socket;
  ConnState state = kConnStateInvalid;
  Si32 buffer_used = 0;
  char buffer[kConnBufferSize];
  char outgoing[kConnBufferSize];
  Si32 outgoing_used = 0;
  Si32 outgoing_sent = 0;
  Uii uii;
  double server_time_to_client_time;

  Si32 cmd_bucket = 0;
  double time_to_fill_bucket_at = 0;
  NetPlayerCmd next_cmd;
  bool is_next_cmd_sent = false;

  void HandleMsgRegistrationResponse() {
    MsgRegistrationResponse m;
    memcpy(&m, buffer + sizeof(MsgHeader), sizeof(m));
  }
  void HandleMsgPong() {
    MsgPong m;
    memcpy(&m, buffer + sizeof(MsgHeader), sizeof(m));
  }
  void HandleMsgAvatarState() {
    MsgAvatarState m;
    memcpy(&m, buffer + sizeof(MsgHeader), sizeof(m));
  }

  void PrepareOutgoingData() {
    if (Time() >= time_to_fill_bucket_at) {
      if (cmd_bucket < kMaxClientCmdBucketSize) {
        cmd_bucket = cmd_bucket + 1;
        time_to_fill_bucket_at = Time() + kClientCmdBucketFillDelay;
      }
    }
    if (cmd_bucket) {
      if (!is_next_cmd_sent) {
        switch(next_cmd.cmd) {
          case kPlayerCmdAttack: {
              MsgHeader h;
              h.msg_type = kMsgTypePlayerCmdAttack;
              h.msg_size = sizeof(MsgPlayerCmdAttack);
              memcpy(outgoing + outgoing_used, &h, sizeof(MsgHeader));
              outgoing_used += sizeof(MsgHeader);
              MsgPlayerCmdAttack m;
              m.avatar_uii = next_cmd.uii.value;
              m.target_uii = next_cmd.uii.value;
              memcpy(outgoing + outgoing_used, &m, sizeof(MsgPlayerCmdAttack));
              outgoing_used += sizeof(m);
            }
            break;
          case kPlayerCmdWalkToPoint:{
            MsgHeader h;
            h.msg_type = kMsgTypePlayerCmdWalkToPoint;
            h.msg_size = sizeof(MsgPlayerCmdWalkToPoint);
            memcpy(outgoing + outgoing_used, &h, sizeof(MsgHeader));
            outgoing_used += sizeof(MsgHeader);
            MsgPlayerCmdWalkToPoint m;
            m.avatar_uii = next_cmd.uii.value;
            m.x = next_cmd.pos.x;
            m.y = next_cmd.pos.y;
            memcpy(outgoing + outgoing_used, &m, sizeof(MsgPlayerCmdWalkToPoint));
            outgoing_used += sizeof(m);
          }
            break;
          case kPlayerCmdInteractWithItem:
            break;
        }
        is_next_cmd_sent = true;
        cmd_bucket--;
      }
    }
  }

  void UpdateClient() {
    for (Si32 i = 0; i < 128; ++i) {
      size_t read = 0;
      size_t bytes_to_read = 0;
      if (buffer_used < sizeof(MsgHeader)) {
        bytes_to_read = sizeof(MsgHeader) - buffer_used;
      } else {
        MsgHeader h;
        memcpy(&h, buffer, sizeof(MsgHeader));
        if (buffer_used < sizeof(MsgHeader) + h.msg_size) {
          bytes_to_read = h.msg_size - buffer_used;
        } else {
          if (h.msg_type >= kMsgTypeCount) {
            *Log() << Time() << " Message type unknown!";
          } else if (h.msg_size < g_msg_size[h.msg_type]) {
            *Log() << Time() << " Message size error!";
          } else {
            switch (h.msg_type) {
              case kMsgTypeRegistrationResponse:
                HandleMsgRegistrationResponse();
                break;
              case kMsgTypePong:
                HandleMsgPong();
                break;
              case kMsgTypeAvatarState:
                HandleMsgAvatarState();
                break;
              default:
                *Log() << Time() << " Message type error!";
                break;
            }
          }
          bytes_to_read = sizeof(MsgHeader);
          buffer_used = 0;
        }
      }
      SocketResult res = socket.Read(buffer + buffer_used, bytes_to_read, &read);
      buffer_used += read;
      if (res != kSocketOk) {
        *Log() << Time() << " UpdateClient Read error: " << socket.GetLastError();
        if (res == kSocketConnectionReset) {
          // TODO: handle disconnection in a way that makes sense
        }
      } else if (read == 0) {
        //*Log() << Time() << " UpdateServer g_connections[" << idx << "] read: 0";

        // nothing to read
        break;
      }
    }

    if (outgoing_used == 0) {
      PrepareOutgoingData();
    }
    if (outgoing_sent < outgoing_used && socket.IsValid()) {
      size_t written = 0;
      SocketResult res = socket.Write(outgoing + outgoing_sent,
        outgoing_used - outgoing_sent, &written);
      if (res != kSocketOk) {
        *Log() << Time() << " UpdateClient Write error: " << socket.GetLastError();
      } else {
        outgoing_sent += written;
        if (outgoing_sent == outgoing_used) {
          outgoing_sent = 0;
          outgoing_used = 0;
        }
      }
    }
  }
};


void Connection::PrepareOutgoingData(NetServerState *server) {
  constexpr Si32 kMaxSize = kConnBufferSize - sizeof(MsgAvatarState) - sizeof(MsgHeader);
  while (outgoing_used < kMaxSize) {
    if (queue.Length()) {
      Uii uii = queue.PopFront();
      Avatar* a = server->avatars.TryGetItem(uii);
      if (a) {
        MsgHeader h;
        h.msg_size = sizeof(MsgAvatarState);
        h.msg_type = kMsgTypeAvatarState;
        memcpy(outgoing + outgoing_used, &h, sizeof(MsgHeader));
        outgoing_used += sizeof(MsgHeader);
        MsgAvatarState m;
        m.uii = a->uii;
        m.state = a->state;
        m.unit_type = a->unit_type;
        m.begin_tick = a->begin_tick;
        m.begin_x = a->begin_pos.x;
        m.begin_y = a->begin_pos.y;
        m.duration_ticks = a->end_tick - a->begin_tick;
        m.end_offset_x = a->end_pos.x - a->begin_pos.x;
        m.end_offset_y = a->end_pos.y - a->begin_pos.y;
        m.target_uii = a->target_uii;
        memcpy(outgoing + outgoing_used, &m, sizeof(MsgHeader));
        outgoing_used += sizeof(MsgAvatarState);
      }
    } else {
      break;
    }
  }
}

void Connection::Update(NetServerState *server) {
  size_t read = 0;
  size_t bytes_to_read = 0;
  if (buffer_used < sizeof(MsgHeader)) {
    bytes_to_read = sizeof(MsgHeader) - buffer_used;
  } else {
    MsgHeader h;
    memcpy(&h, buffer, sizeof(MsgHeader));
    if (buffer_used < sizeof(MsgHeader) + h.msg_size) {
      bytes_to_read = h.msg_size - buffer_used;
    } else {
      if (h.msg_type >= kMsgTypeCount) {
        *Log() << Time() << " Message type unknown!";
      } else if (h.msg_size < g_msg_size[h.msg_type]) {
        *Log() << Time() << " Message size error!";
      } else {
        switch (h.msg_type) {
          case kMsgTypeRegistrationRequest:
            HandleMsgRegistrationRequest();
            break;
          case kMsgTypePing:
            HandleMsgPing();
            break;
          case kMsgTypePlayerCmdWalkToPoint:
            HandleMsgPlayerCmdWalkToPoint();
            break;
          case kMsgTypePlayerCmdInteractWithItem:
            HandleMsgPlayerCmdInteractWithItem();
            break;
          case kMsgTypePlayerCmdAttack:
            HandleMsgPlayerCmdAttack();
            break;
          default:
            *Log() << Time() << " Message type error!";
            break;
        }
      }
      bytes_to_read = sizeof(MsgHeader);
      buffer_used = 0;
    }
  }
  SocketResult res = socket.Read(buffer + buffer_used, bytes_to_read, &read);
  buffer_used += read;
  if (res != kSocketOk) {
    *Log() << Time() << " UpdateServer connections[" << idx << "] Read error: " << socket.GetLastError();
    if (res == kSocketConnectionReset) {
      // TODO: handle disconnection in a way that makes sense
    }
  } else if (read == 0) {
    //*Log() << Time() << " UpdateServer g_connections[" << idx << "] read: 0";
    // nothing to read
  }

  if (outgoing_used == 0) {
    PrepareOutgoingData(server);
  }
  if (outgoing_sent < outgoing_used && socket.IsValid()) {
    size_t written = 0;
    SocketResult res = socket.Write(outgoing + outgoing_sent,
      outgoing_used - outgoing_sent, &written);
    if (res != kSocketOk) {
      *Log() << Time() << " UpdateServer connections[" << idx << "] Write error: " << socket.GetLastError();
    } else {
      outgoing_sent += written;
      if (outgoing_sent == outgoing_used) {
        outgoing_sent = 0;
        outgoing_used = 0;
      }
    }
  }
}


bool g_show_variables = false;

DecoratedFrame g_border;
DecoratedFrame g_button_normal;
DecoratedFrame g_button_hover;
DecoratedFrame g_button_down;
Font g_font;
Sound g_snd_button_down;
Sound g_snd_button_up;
Sound g_snd_open_box;
Sound g_snd_sharp_echo;
Sound g_snd_throw;
Sound g_snd_pain;
Sound g_snd_fall;

Sprite g_loading_eyeballs;
Sprite g_loading_pupils;
Sprite g_loading_title;

Sprite g_border_0;
Sprite g_border_1;
Sprite g_bar_0;
Sprite g_bar_1;

Sprite g_placeholder;

ScriptVirtualMachine g_vm;

Sprite g_background_clone;


std::string g_template_action = "%ACTION%";
std::string g_template_frame = "%FRAME%";
std::string g_template_direction = "%DIRECTION%";

void OpenBox(std::string box_label);
void OnVariableChange(std::string var_name, double value);

std::shared_ptr<Button> MakeButton(Ui64 tag, Vec2Si32 pos,
    KeyCode hotkey, Ui32 tab_order, std::string text,
    Vec2Si32 size = Vec2Si32(0, 0),
    std::shared_ptr<Text> *out_text = nullptr) {
  Vec2Si32 button_text_size = g_font.EvaluateSize(text.c_str(), false);
  Vec2Si32 button_size = button_text_size + Vec2Si32(13 * 2 + 4, 4);
  if (size != Vec2Si32(0, 0)) {
    button_size = size;
  }
  Sprite button_normal = g_button_normal.DrawExternalSize(button_size);
  Sprite button_hover = g_button_hover.DrawExternalSize(button_size);
  Sprite button_down = g_button_down.DrawExternalSize(button_size);

  Sound silent;
  std::shared_ptr<Button> button(new Button(tag, pos,
    button_normal, button_down, button_hover,
    g_snd_button_down, g_snd_button_up, hotkey, tab_order));
  std::shared_ptr<Text> button_textbox(new Text(
    0, Vec2Si32(2, 8), Vec2Si32(button_size.x - 4, button_text_size.y),
    0, g_font, kTextOriginBottom, Rgba(255, 255, 255), text, kAlignCenter));
  button->AddChild(button_textbox);
  if (out_text) {
    *out_text = button_textbox;
  }
  return button;
}


void ShowVariables() {
  std::map<std::string, double> sorted;
  for (auto it = g_vm.variables.begin(); it != g_vm.variables.end(); ++it) {
    sorted.emplace(it->first, it->second);
  }
  std::stringstream str;
  str << u8"Состояние переменных:\n";
  for (auto it = sorted.begin(); it != sorted.end(); ++it) {
    str << it->first << " = " << it->second << "\n";
  }
  g_font.Draw(str.str().c_str(), 32, 96, kTextOriginBottom, kDrawBlendingModeColorize, kFilterNearest,
    Rgba(255, 200, 255));
}


Ui64 ShowModal(std::shared_ptr<Panel> gui) {
  Ui64 clicked_button = Ui64(-1);
  while (true) {
    if (g_show_variables) {
      Clear();
    } else {
      g_background_clone.Draw(0, 0);
    }
    gui->SetPos((ScreenSize() - gui->GetSize()) / 2);
    gui->Draw(Vec2Si32(0, 0));
    if (g_show_variables) {
      ShowVariables();
    }
    ShowFrame();
    if (IsKeyDownward(kKeyEscape) || clicked_button != Ui64(-1)) {
      break;
    }
    std::deque<GuiMessage> messages;
    for (Si32 idx = 0; idx < InputMessageCount(); ++idx) {
      gui->ApplyInput(GetInputMessage(idx), &messages);
    }
    for (auto it = messages.begin(); it != messages.end(); ++it) {
      if (it->kind == kGuiButtonClick) {
        clicked_button = it->panel->GetTag();
      }
    }
  }
  return clicked_button;
}


struct MenuDesc {
  Ui64 first_idx;
  std::string main_text;
  std::deque<std::string> item_text;

  MenuDesc(Ui64 in_first_idx, std::string in_main_text) {
    first_idx = in_first_idx;
    main_text = in_main_text;
  }
};

std::shared_ptr<Panel> MakeMenu(MenuDesc &desc) {
  Vec2Si32 main_size = g_font.EvaluateSize(desc.main_text.c_str(), false);
  Vec2Si32 buttons_size(0, 0);
  for (Si32 idx = 0; idx < (Si32)desc.item_text.size(); ++idx) {
    Vec2Si32 text_size = g_font.EvaluateSize(desc.item_text[idx].c_str(), false);
    Vec2Si32 button_size = text_size + Vec2Si32(16, 16) + Vec2Si32(64, 0);
    buttons_size.x = std::max(buttons_size.x, button_size.x);
    buttons_size.y += button_size.y + 16;
  }
  Vec2Si32 total_size(std::max(main_size.x, buttons_size.x) + 64,
                      main_size.y + buttons_size.y + 64);

  std::shared_ptr<Panel> gui(new Panel(0, Vec2Si32(0, 0),
    total_size, 0, g_border.DrawExternalSize(total_size)));

  Ui32 y = gui->GetSize().y-32;

  std::shared_ptr<Text> textbox(new Text(
    0, Vec2Si32(32, y), main_size,
    0, g_font, kTextOriginTop, Rgba(255, 255, 255), desc.main_text.c_str(), kAlignCenter));
  gui->AddChild(textbox);
  y -= main_size.y;

  for (Si32 idx = 0; idx < (Si32)desc.item_text.size(); ++idx) {
    Vec2Si32 text_size = g_font.EvaluateSize(desc.item_text[idx].c_str(), false);
    Vec2Si32 button_size = text_size + Vec2Si32(16, 16);
    y -= 16 + button_size.y;
    std::shared_ptr<Button> create_button = MakeButton(
      desc.first_idx + idx, Vec2Si32(32, y), static_cast<KeyCode>(kKey1 + idx),
      1, desc.item_text[idx].c_str(), Vec2Si32(gui->GetSize().x - 64, button_size.y));
    gui->AddChild(create_button);
  }
  return gui;
}

std::string RunNode(ScriptVirtualMachine &vm, std::string name) {
  auto it = vm.script.nodes.find(name);
  if (it == vm.script.nodes.end()) {
    return std::string();
  }
  vm.variables[name] += 1.0;
  ScriptNode &n = it->second;
  n.code.Execute(vm);

  std::deque<ScriptChoice*> choices;
  for (ScriptChoice& c: n.choices) {
    double res = c.condition.Calculate(vm);
    if (std::abs(res) >= 0.5) {
      choices.push_back(&c);
    }
  }

  MenuDesc desc(1, n.text);
  for (Si32 idx = 0; idx < (Si32)choices.size(); ++idx) {
    desc.item_text.push_back(choices[idx]->text);
  }
  std::shared_ptr<Panel> gui = MakeMenu(desc);
  Ui64 clicked_button = ShowModal(gui);

  if (clicked_button <= 0 || clicked_button > choices.size()) {
    return std::string();
  }
  ScriptChoice *res = choices[size_t(clicked_button - 1)];
  res->code.Execute(vm);
  return res->divert;
}


bool Porbe(Sprite sprite, Vec2Si32 pos) {
  Vec2Si32 rel = pos + sprite.Pivot();
  if (rel.x < 0 || rel.y < 0 || rel.x >= sprite.Size().x || rel.y >= sprite.Size().y) {
    return false;
  }
  Rgba c = *(sprite.RgbaData() + sprite.StridePixels() * rel.y + rel.x);
  if (c.a == 0) {
    return false;
  }
  return true;
}

struct Arrow {
  Vec2F target_pos;
  Vec2F pos;
  Vec2F direction;
  Character *target_character = nullptr;
  bool is_flying = true;
  float damage = 0.f;
};

Character* FindCharacterByLabel(const std::string &label);
void LaunchAnArrow(Character &shooter_ch, Vec2F target_pos, Character* target_ch, float damage);
void DropLoot(Character &ch);

struct HitInfo {
  enum Type {
    kMap,
    kBox,
    kCharacter
  };

  Type type = kMap;
  std::string label;
  Vec2F pos = Vec2F(0.f, 0.f);
};

struct Item {
  std::string label;
  std::string name;
  double weight;
};

struct Box {
  std::string label;
  Vec2F pos;
  std::deque<std::string> items;
  bool is_loot;
};

struct TreeType {
  Sprite sprite;
  Vec2F base = Vec2F(0.f, 0.f);
};

struct Tree {
  Vec2F pos;
  size_t tree_type_idx;
  float prev_alpha = 255.f;
};

const double kInteractionDistance = 75.0;

std::vector<Arrow> g_arrows;
std::vector<Character> g_characters;
std::vector<Tree> g_trees;
std::vector<TreeType> g_tree_types;

size_t g_human_idx = 0;

std::unordered_map<std::string, Item> g_items;
std::unordered_map<std::string, Box> g_boxes;

HitInfo g_last_frame_hit;

double g_prev_time;
Sprite g_map;
Sprite g_box;
Sprite g_loot;
Vec2F g_view_pos;
Sound g_music;

struct Character {
  enum Type {
    kChPlayer,
    kChTalker,
    kChAgressive,
    kChPosition,
    kChReversive
  };

  float max_hp = 100.f;
  float hp = 100.f;
  Type type = kChTalker;
  float sight_dist = 0.f;
  Vec2F face_dir = Vec2F(1.f, 0.f);
  Vec2F pos = Vec2F(0.f, 0.f);
  HitInfo dst_hit_info;
  Ui32 frame = 0;
  double time_to_frame = 0.f;
  double frame_duration = 0.1f;
  bool is_walking = false;
  bool is_attacking = false;
  bool is_dead = false;
  float walk_vel = 180.f;
  float walk_vel_multiplier = 1.f;
  std::string name;
  std::string label;
  std::vector<std::vector<std::vector<Sprite>>> action_direction_frames;
  std::deque<std::string> items;
  double attack_cooldown = 0.0;
  float attack_range = 0.0;
  double time_to_stand = 0.0;
  double time_to_spawn = 0.0;

  std::shared_ptr<Character> state_at_spawn;


  float GetApproxHeight() {
    Sprite *p = TryGetSprite(kCharacterAnimationWalk, kChDirRight, 0);
    if (p) {
      return p->Height();
    }
    return 200.f;
  }

  void SetSprite(size_t action_idx, size_t direction_idx, size_t frame_idx, const arctic::Sprite &sp) {
    if (action_direction_frames.size() <= action_idx) {
      action_direction_frames.resize(action_idx + 1);
    }
    auto &direction_frames = action_direction_frames[action_idx];
    if (direction_frames.size() <= direction_idx) {
      direction_frames.resize(direction_idx+1);
    }
    auto &frames = direction_frames[direction_idx];
    if (frames.size() <= frame_idx) {
      frames.resize(frame_idx + 1);
    }
    frames[frame_idx] = sp;
  }

  Sprite* TryGetSprite(size_t action_idx, size_t direction_idx, size_t frame_idx) {
    if (action_idx < action_direction_frames.size()) {
      auto &direction_frames = action_direction_frames[action_idx];
      if (direction_idx < direction_frames.size()) {
        auto &frames = direction_frames[direction_idx];
        if (frame_idx < frames.size()) {
          return &frames[frame_idx];
        }
      }
    }
    return nullptr;
  }

  Sprite* TryGetSpriteClamp(size_t action_idx, size_t direction_idx, size_t frame_idx) {
    if (action_idx < action_direction_frames.size()) {
      auto &direction_frames = action_direction_frames[action_idx];
      if (direction_idx < direction_frames.size()) {
        auto &frames = direction_frames[direction_idx];
        if (frames.size()) {
          return &frames[std::min(frames.size() - 1, frame_idx)];
        }
      }
    }
    return nullptr;
  }

  Sprite* TryGetSpriteLoop(size_t action_idx, size_t direction_idx, size_t frame_idx) {
    if (action_idx < action_direction_frames.size()) {
      auto &direction_frames = action_direction_frames[action_idx];
      if (direction_idx < direction_frames.size()) {
        auto &frames = direction_frames[direction_idx];
        if (frames.size()) {
          return &frames[frame_idx % frames.size()];
        }
      }
    }
    return nullptr;
  }

  void UpdateAnimation(double dt) {
    time_to_frame -= dt * walk_vel_multiplier;
    if (time_to_frame <= 0.f) {
      time_to_frame = frame_duration;
      if (is_walking || is_attacking || is_dead) {
        frame++;
      } else {
        if (TryGetSprite(kCharacterAnimationIdle, 0, 0)) {
          frame++;
        }
      }
    }
  }

  void UpdateMovement(double dt) {
    attack_cooldown = std::max(0.0, attack_cooldown - dt);
    if (type == kChPlayer) {
      if (is_dead) {
        time_to_spawn -= dt;
        if (time_to_spawn <= 0) {
          Respawn();
          return;
        }
        return;
      }
      if (is_walking) {
        if (dst_hit_info.type == HitInfo::kCharacter && is_attacking) {
          // Update target pos
          Character *target = FindCharacterByLabel(dst_hit_info.label);
          if (target) {
            dst_hit_info.pos = target->pos;
          }
        }
        if (dst_hit_info.type == HitInfo::kBox ||
            dst_hit_info.type == HitInfo::kCharacter) {
          if (Length(pos - dst_hit_info.pos) < kInteractionDistance) {
            // Is close enought for interaction

            if (is_attacking) {
              if (attack_cooldown <= 0) {
                is_walking = false;
                walk_vel_multiplier = 1.f;
                frame = 0;
                attack_cooldown = 1.f;

                Character *target = FindCharacterByLabel(dst_hit_info.label);
                if (target) {
                  target->hp -= 50.f;
                  if (target->hp <= 0.f) {
                    target->Die();
                  }
                }

                dst_hit_info.pos = pos;
                dst_hit_info.type = HitInfo::kMap;
              }
            } else {
              is_walking = false;
              walk_vel_multiplier = 1.f;


              std::string nextNode(dst_hit_info.label);
              g_background_clone.Clone(GetEngine()->GetBackbuffer());
              for (Si32 y = 0; y < g_background_clone.Size().y; ++y) {
                for (Si32 x = 0; x < g_background_clone.Size().x; ++x) {
                  Rgba color = GetPixel(g_background_clone, x, y);
                  color.a = 255;
                  SetPixel(g_background_clone, x, y, color);
                }
              }
              if (!nextNode.empty()) {
                g_snd_sharp_echo.Play();
              }
              while (!nextNode.empty()) {
                nextNode = RunNode(g_vm, nextNode);
              }
              OpenBox(dst_hit_info.label);


              dst_hit_info.pos = pos;
              dst_hit_info.type = HitInfo::kMap;
            }
            return;
          }
        }
      } else {
        if (is_attacking) {
          if (!TryGetSprite(kCharacterAnimationAttack, 0, frame)) {
            is_attacking = false;
          }
          return;
        }
      }

      if (pos.x == dst_hit_info.pos.x && pos.y == dst_hit_info.pos.y) {
        is_walking = false;
        walk_vel_multiplier = 1.f;
        return;
      }
      is_walking = true;
      Walk(dst_hit_info.pos, walk_vel, dt);
    }
    if (time_to_stand > 0.f) {
      time_to_stand -= dt;
    }

    if (type == kChPosition || type == kChReversive || type == kChAgressive) {
      if (is_dead) {
        time_to_spawn -= dt;
        if (time_to_spawn <= 0) {
          Respawn();
          return;
        }
        return;
      }

      Vec2F hcpos = g_characters[g_human_idx].pos;
      float dist = Length(hcpos - pos);
      if (dist > sight_dist) {
        is_walking = false;
        return;
      }
      if (dist < kInteractionDistance) {
        AttackPlayer(dt);
      }
      if (attack_range > 0.f && dist < attack_range) {
        if (attack_cooldown == 0.0) {
          RangedAttackPlayer();
          time_to_stand = 1.f;
        }

        if (type == kChReversive) {
          Vec2F dst = pos + NormalizeSafe(pos - hcpos) * 200.f;
          if (time_to_stand <= 0.f) {
            Walk(dst, walk_vel * 0.5, dt);
          } else {
            is_walking = false;
          }
          if (Length(hcpos - pos) >= attack_range) {
            time_to_stand = 1.f;
            is_walking = false;
          }
          return;
        }
      }
      if (type == kChPosition) {
        is_walking = false;
        return;
      }
      if (time_to_stand <= 0.f) {
        Walk(hcpos, walk_vel * 0.5, dt);
      } else {
        is_walking = false;
      }
    }

  }

  void AttackPlayer(double dt) {
    if (attack_cooldown == 0.0) {
      attack_cooldown = 3.0;
      g_snd_sharp_echo.Play();
      g_characters[g_human_idx].hp -= 5.f;
    }
  }

  void RangedAttackPlayer() {
    if (attack_cooldown == 0.0) {
      attack_cooldown = 3.0;
      LaunchAnArrow(*this, Vec2F(0.f, 0.f), &g_characters[g_human_idx], 5.f);
    }
  }

  void Respawn() {
    std::shared_ptr<Character> spawn = state_at_spawn;
    *this = *spawn;
    OnSpawn();
  }

  void OnSpawn() {
    std::shared_ptr<Character> spawn(new Character(*this));
    state_at_spawn = spawn;
  }

  void Walk(Vec2F dst, double vel, double dt) {
    is_walking = true;
    Vec2F pos_to_dst = dst - pos;
    Vec2F dir = Normalize(pos_to_dst);
    face_dir = dir;
    float dist = Length(pos_to_dst);
    float max_dist = float(dt * vel * walk_vel_multiplier);
    if (max_dist > dist) {
      pos = dst;
    } else {
      pos += dir * max_dist;
    }

  }

  void Die() {
    DropLoot(*this);
    is_walking = false;
    is_attacking = false;
    is_dead = true;
    frame = 0;
    time_to_spawn = 30.0;
  }

  bool Draw(Vec2F view_pos, Vec2F probe_pos) {
    bool is_hit = false;
    size_t dir = (face_dir.x >= 0.f ? kChDirRight : kChDirLeft);
    Sprite *s = nullptr;
    if (is_dead) {
      s = TryGetSpriteClamp(kCharacterAnimationDie, dir, frame);
      if (!s) {
        s = &g_placeholder;
      }
    } else if (!is_walking) {
      if (is_attacking) {
        s = TryGetSpriteLoop(kCharacterAnimationAttack, dir, frame);
        if (!s) {
          s = &g_placeholder;
        }
      } else {
        s = TryGetSpriteLoop(kCharacterAnimationIdle, dir, frame);
      }
    }
    if (!s) {
      s = TryGetSpriteLoop(kCharacterAnimationWalk, dir, frame);
    }
    if (!s) {
      s = &g_placeholder;
    }
    s->Draw(Vec2Si32(pos - view_pos));

    Vec2Si32 rel(probe_pos - (pos - view_pos));
    if (Porbe(*s, rel)) {
      is_hit = true;
    }
    Vec2Si32 name_size = g_font.EvaluateSize(name.c_str(), false);
    Vec2Si32 name_pos = Vec2Si32(pos - view_pos) + Vec2Si32(-name_size.x / 2, s->Size().y);
    g_font.Draw(name.c_str(), name_pos.x, name_pos.y, kTextOriginBottom, kDrawBlendingModeColorize, kFilterNearest,
                Rgba(128, 255, 128));
    return is_hit;
  }
};

Character* FindCharacterByLabel(const std::string &label) {
  for (auto it = g_characters.begin(); it != g_characters.end(); ++it) {
    if (it->label == label) {
      return &*it;
    }
  }
  return nullptr;
}



void OnAcquireItem(std::string item_label) {
  g_vm.variables[item_label] += 1.0;
}

void OnLooseItem(std::string item_label) {
  g_vm.variables[item_label] -= 1.0;
}

void OnVariableChange(std::string var_name, double value) {
  if (g_items.find(var_name) == g_items.end()) {
    return;
  }

  double count = 0;
  auto &hi = g_characters[g_human_idx].items;
  for (auto &item: hi) {
    if (item == var_name) {
      count++;
    }
  }
  while (count + 0.5 < value) {
    hi.push_back(var_name);
    count++;
  }
  if (value + 0.5 < count) {
    for (Ui64 i = 0; i < hi.size(); ++i) {
      if (hi[(size_t)i] == var_name) {
        hi[(size_t)i] = hi[hi.size() - 1];
        hi.pop_back();
        count--;
        if (value + 0.5 >= count) {
          break;
        }
      }
    }
  }
}

void OpenBox(std::string box_label) {
  if (g_boxes.find(box_label) == g_boxes.end()) {
    return;
  }
  g_snd_open_box.Play();

  if (g_boxes[box_label].is_loot) {
    auto &hi = g_characters[g_human_idx].items;
    auto &bi = g_boxes[box_label].items;
    for (Si32 idx = 0; idx < (Si32)bi.size() ; ++idx) {
      OnAcquireItem(bi[(size_t)idx]);
      hi.push_back(bi[(size_t)idx]);
    }
    bi.clear();
    g_boxes.erase(box_label);
    return;
  }

  while (true) {
    MenuDesc desc(1, u8"Вещи игрока");
    auto &hi = g_characters[g_human_idx].items;
    for (Si32 idx = 0; idx < (Si32)hi.size(); ++idx) {
      auto it = g_items.find(hi[idx]);
      if (it == g_items.end()) {
        desc.item_text.push_back(hi[idx]);
      } else {
        desc.item_text.push_back(it->second.name);
      }
    }
    std::shared_ptr<Panel> gui1 = MakeMenu(desc);
    MenuDesc desc2(1000000, u8"Содержимое сундука");
    auto &bi = g_boxes[box_label].items;
    for (Si32 idx = 0; idx < (Si32)bi.size() ; ++idx) {
      auto it = g_items.find(bi[idx]);
      if (it == g_items.end()) {
        desc2.item_text.push_back(bi[idx]);
      } else {
        desc2.item_text.push_back(it->second.name);
      }

    }
    std::shared_ptr<Panel> gui2 = MakeMenu(desc2);
    MenuDesc desc3(3000000, u8" ");
    desc3.item_text.push_back(u8"Готово");
    std::shared_ptr<Panel> gui3 = MakeMenu(desc3);

    Vec2Si32 size(gui1->GetSize().x + gui2->GetSize().x + gui3->GetSize().x,
                  std::max(std::max(gui1->GetSize().y, gui2->GetSize().y), gui3->GetSize().y));

    Sprite empty;
    std::shared_ptr<Panel> gui(new Panel(0, Vec2Si32(0, 0), size, 0, empty));
    gui->AddChild(gui1);
    gui->AddChild(gui2);
    gui->AddChild(gui3);
    gui2->SetPos(Vec2Si32(gui1->GetSize().x, 0));
    gui3->SetPos(Vec2Si32(gui1->GetSize().x + gui2->GetSize().x, 0));

    Ui64 clicked_button = ShowModal(gui);

    if (clicked_button <= 0 || clicked_button > 2000000) {
      return;
    }
    if (clicked_button < 1000000) {
      Ui64 i = clicked_button - 1;
      OnLooseItem(hi[(size_t)i]);
      bi.push_back(hi[(size_t)i]);
      for (; i < hi.size() - 1; ++i) {
        hi[(size_t)i] = hi[(size_t)i+1];
      }
      hi.pop_back();
    } else {
      Ui64 i = clicked_button - 1000000;
      OnAcquireItem(bi[(size_t)i]);
      hi.push_back(bi[(size_t)i]);
      for (; i < bi.size() - 1; ++i) {
        bi[(size_t)i] = bi[(size_t)i+1];
      }
      bi.pop_back();
    }
  }
}

void DrawLoadingIndicator() {
  g_loading_eyeballs.Draw(ScreenSize().x-g_loading_eyeballs.Size().x, 0);
  double t = Time();
  g_loading_pupils.Draw(ScreenSize().x-g_loading_pupils.Size().x, Si32(cos(t*2.0)*3.0));
}

void ShowLoadingScreen() {
  Clear();
  g_loading_title.Draw(0,0);
  DrawLoadingIndicator();
  ShowFrame();
}



void Init() {
  double start_t = Time();

  g_loading_eyeballs.Load("data/loading_eyeballs.tga");
  g_loading_pupils.Load("data/loading_pupils.tga");
  g_loading_title.Load("data/loading_title.tga");
  ResizeScreen(960, 540);
  ShowLoadingScreen();
  g_music.Load("data/music.ogg", true);
  g_music.Play(0.5);
  ShowLoadingScreen();
  //ResizeScreen(1024, 640);
  g_prev_time = Time();
  g_font.Load("data/arctic_one_bmf.fnt");
  ShowLoadingScreen();
  g_map.Load("data/grass.tga");
  g_map.SetPivot(Vec2Si32(0, 0));
  ShowLoadingScreen();
  g_box.Load("data/box.tga");
  g_loot.Load("data/loot_gold_3.tga");
  ShowLoadingScreen();
  g_view_pos = Vec2F(0, 0);

  std::vector<Ui8> dialogueScr = ReadFile("data/dialogue.scr");
  ShowLoadingScreen();
  if (!dialogueScr.size() || *dialogueScr.rbegin() != '\0') {
    dialogueScr.push_back('\0');
  }
  g_vm.OnVariableChange = OnVariableChange;
  ParseResult parseResult = ParseScript(g_vm.script, dialogueScr.data());
  if (parseResult.is_ok) {
    Log("data/dialogue.scr script parsed OK");
  } else {
    Log("Error parsing script \"data/dialogue.scr\": ", parseResult.error_message.c_str());
  }
  ShowLoadingScreen();

  Sprite border;
  border.Load("data/border.tga");
  ShowLoadingScreen();
  g_border.Split(border, 32, true, true);
  Sprite button_normal;
  button_normal.Load("data/button_normal.tga");
  ShowLoadingScreen();
  g_button_normal.Split(button_normal, 12, true, true);
  Sprite button_hover;
  button_hover.Load("data/button_hover.tga");
  ShowLoadingScreen();
  g_button_hover.Split(button_hover, 12, true, true);
  Sprite button_down;
  button_down.Load("data/button_down.tga");
  ShowLoadingScreen();
  g_button_down.Split(button_down, 12, true, true);

  g_snd_button_down.Load("data/button_down.wav", true);
  ShowLoadingScreen();
  g_snd_button_up.Load("data/button_up.wav", true);
  ShowLoadingScreen();
  g_snd_open_box.Load("data/open_box.wav", true);
  ShowLoadingScreen();
  g_snd_sharp_echo.Load("data/sharp_echo.wav", true);
  ShowLoadingScreen();
  g_snd_throw.Load("data/throw.wav", true);
  ShowLoadingScreen();
  g_snd_pain.Load("data/pain.wav", true);
  ShowLoadingScreen();
  g_snd_fall.Load("data/fall.wav", true);
  ShowLoadingScreen();


  g_border_0.Load("data/border_0.tga");
  g_border_1.Load("data/border_1.tga");
  g_bar_0.Load("data/bar_0.tga");
  g_bar_1.Load("data/bar_1.tga");

  size_t tree_type_idx = 0;
  while (true) {
    Sprite sprite;
    std::stringstream s;
    s << "data/tree_" << (tree_type_idx) << ".tga";
    sprite.Load(s.str());
    if (sprite.Size().x == 0) {
      break;
    }
    TreeType tt;
    tt.sprite = sprite;
    g_tree_types.push_back(tt);
    ++tree_type_idx;
  }

  g_placeholder.Create(Vec2Si32(100, 200));
  g_placeholder.Clear(Rgba(255,0,0,255));
  g_placeholder.SetPivot(Vec2Si32(50,0));

  CsvTable character_csv;
  bool is_ok = character_csv.LoadFile("data/character.csv");
  ShowLoadingScreen();
  if (!is_ok) {
    Log("Can't open \"data/character.csv\": ", character_csv.GetErrorDescription().c_str());
  }
  g_characters.resize((size_t)character_csv.RowCount());
  for (Ui64 rowIdx = 0; rowIdx < character_csv.RowCount(); ++rowIdx) {
    CsvRow* row = character_csv.GetRow(rowIdx);
    Character &c = g_characters[(size_t)rowIdx];
    c.label = row->GetValue(u8"меткаперсонажа", std::string());
    c.name = (*row)[u8"имя"];
    c.pos = Vec2F(row->GetValue(u8"x", 1000.0f), row->GetValue(u8"y", 1000.0f));
    c.dst_hit_info.pos = c.pos;
    std::string sprite_name = (*row)[u8"спрайт"];
    if (!sprite_name.empty()) {
      // search for %FRAME% and %ACTION%
      bool is_frame_present = (sprite_name.find(g_template_frame) != std::string::npos);
      bool is_direction_present = (sprite_name.find(g_template_direction) != std::string::npos);
      bool is_action_present = (sprite_name.find(g_template_action) != std::string::npos);
      if (!is_frame_present && !is_direction_present && !is_action_present) {
        Sprite sp;
        sp.Load(sprite_name);
        if (sp.Size().x > 0) {
          Si32 frames = row->GetValue(u8"кадров", 1);
          Si32 width = sp.Width()/frames;
          for (Si32 i = 0; i < frames; ++i) {
            Sprite frame;
            frame.Reference(sp, i*width, 0, width, sp.Height());
            frame.SetPivot(Vec2Si32(frame.Width()/2, 0));
            c.SetSprite(kCharacterAnimationWalk, kChDirRight, i, frame);
            Sprite clone;
            clone.Clone(frame, kCloneMirrorLr);
            clone.SetPivot(Vec2Si32(clone.Width()/2, 0));
            c.SetSprite(kCharacterAnimationWalk, kChDirLeft, i, clone);
          }
        }
      } else {
        for (size_t action_idx = 0; action_idx < kCharacterAnimationCount; ++action_idx) {
          if (!is_action_present && action_idx > 0) {
            // TODO: choose some action!
            break;
          } else {
            std::string sprite_a_name = sprite_name;
            if (is_action_present) {
              sprite_a_name.replace(
                sprite_a_name.find(g_template_action),
                g_template_action.size(),
                g_character_animation_name[action_idx]);
            }

            for (size_t direction_idx = 0; direction_idx < kChDirCount; ++direction_idx) {
              if (!is_direction_present && direction_idx != 0) {
                // copy data
                size_t frame_idx = 0;
                while (true) {
                  Sprite *p = c.TryGetSprite(action_idx, 0, frame_idx);
                  if (p) {
                    Sprite clone;
                    clone.Clone(*p, kCloneMirrorLr);
                    clone.SetPivot(Vec2Si32(clone.Width()/2, 0));
                    c.SetSprite(action_idx, direction_idx, frame_idx, clone);
                  } else {
                    break;
                  }
                  ++frame_idx;
                }
              } else {
                std::string sprite_ad_name = sprite_a_name;
                if (is_direction_present) {
                  sprite_ad_name.replace(
                    sprite_ad_name.find(g_template_direction),
                    g_template_direction.size(),
                    g_character_direction_name[direction_idx]);
                }

                size_t frame_idx = 0;
                while (true) {
                  std::string sprite_adf_name = sprite_ad_name;
                  if (!is_frame_present && frame_idx != 0) {
                    break;
                  } else {
                    if (is_frame_present) {
                      std::stringstream str;
                      str << (frame_idx + 1);
                      std::string idx_name = str.str();

                      sprite_adf_name.replace(
                        sprite_adf_name.find(g_template_frame),
                        g_template_frame.size(),
                        idx_name);
                    }

                    Sprite sp;
                    sp.Load(sprite_adf_name);
                    if (sp.Size().x > 0 && sp.Size().y > 0) {
                      sp.SetPivot(Vec2Si32(sp.Width()/2, 0));
                      c.SetSprite(action_idx, direction_idx, frame_idx, sp);
                    } else {
                      break;
                    }
                  }
                  ++frame_idx;
                } // while (true)

              }
            } // for direction_idx

          }

        } // for action_idx
      }

    }
    if (!c.TryGetSprite(kCharacterAnimationWalk, kChDirRight, 0)) {
      c.SetSprite(kCharacterAnimationWalk, kChDirRight, 0, g_placeholder);
    }
    if (!c.TryGetSprite(kCharacterAnimationWalk, kChDirLeft, 0)) {
      c.SetSprite(kCharacterAnimationWalk, kChDirLeft, 0, g_placeholder);
    }

    std::string type = (*row)[u8"тип"];
    if (type == std::string(u8"игрок")) {
      c.type = Character::kChPlayer;
    }
    if (type == std::string(u8"болтун")) {
      c.type = Character::kChTalker;
    }
    if (type == std::string(u8"агрессивный")) {
      c.type = Character::kChAgressive;
    }
    if (type == std::string(u8"позиционка")) {
      c.type = Character::kChPosition;
    }
    if (type == std::string(u8"реверсивный")) {
      c.type = Character::kChReversive;
    }

    c.sight_dist = row->GetValue(u8"зрение", 0.f);
    c.attack_range = row->GetValue(u8"дальнобойность", 0.f);

    if (c.label == std::string(u8"п_человек")) {
      g_human_idx = (size_t)rowIdx;
    }

    c.OnSpawn();
  }
  ShowLoadingScreen();



  CsvTable item_csv;
  is_ok = item_csv.LoadFile("data/item.csv");
  ShowLoadingScreen();
  if (!is_ok) {
    Log("Can't open \"data/item.csv\": ", item_csv.GetErrorDescription().c_str());
  }
  for (Ui64 rowIdx = 0; rowIdx < item_csv.RowCount(); ++rowIdx) {
    CsvRow* row = item_csv.GetRow(rowIdx);
    std::string label = row->GetValue(u8"меткавещи", std::string());
    Item &item = g_items[label];
    if (item.label == label) {
      Log("Can't add item, as it is already there: ", item.label.c_str());
    } else {
      item.label = label;
      item.name = (*row)[u8"имя"];
      item.weight = row->GetValue(u8"вес", 0.123);
    }
  }


  CsvTable box_csv;
  is_ok = box_csv.LoadFile("data/box.csv");
  ShowLoadingScreen();
  if (!is_ok) {
    Log("Can't open \"data/box.csv\": ", box_csv.GetErrorDescription().c_str());
  }
  for (Ui64 rowIdx = 0; rowIdx < box_csv.RowCount(); ++rowIdx) {
    CsvRow* row = box_csv.GetRow(rowIdx);
    std::string label = row->GetValue(u8"меткаящика", std::string());
    Box &box = g_boxes[label];
    if (box.label == label) {
      Log("Can't add box, as it is already there: ", box.label.c_str());
    } else {
      box.label = label;
      box.pos = Vec2F(row->GetValue(u8"x", 1000.f), row->GetValue(u8"y", 1000.f));
      box.is_loot = false;
    }
  }


  CsvTable boxitem_csv;
  is_ok = boxitem_csv.LoadFile("data/box_item.csv");
  ShowLoadingScreen();
  if (!is_ok) {
    Log("Can't open \"data/box_item.csv\": ", boxitem_csv.GetErrorDescription().c_str());
  }
  for (Ui64 rowIdx = 0; rowIdx < boxitem_csv.RowCount(); ++rowIdx) {
    CsvRow* row = boxitem_csv.GetRow(rowIdx);
    std::string box_label = row->GetValue(u8"меткаящика", std::string());
    std::string item_label = row->GetValue(u8"меткавещи", std::string());
    if (g_boxes.find(box_label) == g_boxes.end()) {
      Log("Can't find box to put item: ", box_label.c_str());
    } else {
      if (g_items.find(item_label) == g_items.end()) {
        Log("Can't find item to put into a box: ", item_label.c_str());
      } else {
        g_boxes[box_label].items.push_back(item_label);
      }
    }
  }


  CsvTable tree_csv;
  is_ok = tree_csv.LoadFile("data/tree.csv");
  ShowLoadingScreen();
  if (!is_ok) {
    Log("Can't open \"data/tree.csv\": ", tree_csv.GetErrorDescription().c_str());
  }
  for (Ui64 rowIdx = 0; rowIdx < tree_csv.RowCount(); ++rowIdx) {
    CsvRow* row = tree_csv.GetRow(rowIdx);
    size_t tt_idx = row->GetValue(u8"индекс", size_t(-1));
    float tt_base_x = row->GetValue(u8"х_базы", 0.f);
    float tt_base_y = row->GetValue(u8"у_базы", 0.f);
    if (tt_idx < g_tree_types.size()) {
      TreeType &tt = g_tree_types[tt_idx];
      tt.base = Vec2F(tt_base_x, tt.sprite.Height() - tt_base_y);
    }
  }


  CsvTable scene_csv;
  is_ok = scene_csv.LoadFile("data/scene.csv");
  ShowLoadingScreen();
  if (!is_ok) {
    Log("Can't open \"data/scene.csv\": ", scene_csv.GetErrorDescription().c_str());
  }
  for (Ui64 rowIdx = 0; rowIdx < scene_csv.RowCount(); ++rowIdx) {
    CsvRow* row = scene_csv.GetRow(rowIdx);
    Tree t;
    t.tree_type_idx = row->GetValue(u8"тип", size_t(-1));
    t.pos.x = row->GetValue(u8"х", 0.f);
    t.pos.y = row->GetValue(u8"у", 0.f);
    if (t.tree_type_idx < g_tree_types.size()) {
      g_trees.push_back(t);
    }
  }

  g_view_pos = g_characters[g_human_idx].pos - Vec2F(ScreenSize())/2.f;
  g_view_pos.x = Clamp(static_cast<float>(g_view_pos.x), 0.f, static_cast<float>(g_map.Size().x - ScreenSize().x));
  g_view_pos.y = Clamp(static_cast<float>(g_view_pos.y), 0.f, static_cast<float>(g_map.Size().y - ScreenSize().y));

  ShowLoadingScreen();


  /*while (Time() < start_t + 1.0) {
    ShowLoadingScreen();
  }*/

  start_t = Time();

  ShowLoadingScreen();

  /*while (Time() < start_t + 4.0) {
    ShowLoadingScreen();
  }*/
  ResizeScreen(1920, 1080);

  //std::stringstream str;
  //str << "row count: " << character_csv.RowCount();
  //Log(str.str().c_str());
}

void LaunchAnArrow(Character &shooter_ch, Vec2F target_pos, Character* target_ch, float damage) {
  Arrow arrow;
  arrow.target_pos = target_pos;
  arrow.pos = shooter_ch.pos + Vec2F(0, shooter_ch.GetApproxHeight() * 0.66f);
  arrow.target_character = target_ch;
  if (target_ch) {
    arrow.target_pos = arrow.target_character->pos +
      Vec2F(0, arrow.target_character->GetApproxHeight() * 0.66f);
  }
  arrow.direction = NormalizeSafe(arrow.target_pos-arrow.pos);
  arrow.damage = damage;
  g_arrows.push_back(arrow);
  g_snd_throw.Play();
}

Ui64 g_next_unique_id = 1;
std::string MakeUniqueLabel(const char* type_name) {
  std::stringstream s;
  s << "глобально_уникальный_" << type_name << "_" << g_next_unique_id;
  ++g_next_unique_id;
  return s.str();
}

void DropLoot(Character &ch) {
  std::string label = MakeUniqueLabel(u8"лут");
  Box &box = g_boxes[label];
  if (box.label == label) {
    Log("Can't add box, as it is already there: ", box.label.c_str());
  } else {
    box.label = label;
    box.pos = ch.pos + Vec2F(Random(-100, 100), Random(-100, 100));
    box.is_loot = true;
  }

  std::string item_label = u8"в_золото";
  if (g_items.find(item_label) == g_items.end()) {
    Log("Can't find item to put into a box: ", item_label.c_str());
  } else {
    box.items.push_back(item_label);
  }
}


void EasyMain() {
  Init();
  bool is_paused = false;
  bool is_edit_mode = false;

  double time = Time();
  while (!IsKeyDownward(kKeyEscape)) {
    if (!g_music.IsPlaying()) {
      g_music.Play(0.5);
    }
    g_prev_time = time;
    time = Time();
    double dt = time - g_prev_time;
    if (dt > 1.0/8.0) {
      dt = 1.0/8.0;
    }

    Character *target_character = nullptr;
    if (g_last_frame_hit.type == HitInfo::kCharacter) {
      target_character = FindCharacterByLabel(g_last_frame_hit.label);
    }
    if (IsKeyDownward(kKeyE)) {
      is_edit_mode = !is_edit_mode;
    }

    if (is_edit_mode) {
      for (size_t i = 0; i < 10; ++i) {
        if (IsKeyDownward(KeyCode(kKey0 + i))) {
          Tree t;
          t.pos = g_view_pos + Vec2F(MousePos());
          t.tree_type_idx = i;
          g_trees.push_back(t);
        }
      }
      if (IsKeyDownward(kKeyBackspace)) {
        if (g_trees.size()) {
          g_trees.pop_back();
        }
      }
      if (IsKeyDownward(kKeyS)) {
        CsvTable scene_csv;
        scene_csv.LoadFile("data/scene.csv");
        while (scene_csv.RowCount()) {
          scene_csv.DeleteRow(scene_csv.RowCount() - 1);
        };
        for (size_t idx = 0; idx < g_trees.size(); ++idx) {
          std::vector<std::string> row;
          row.resize(3);
          {
            std::stringstream s;
            s << g_trees[idx].tree_type_idx;
            row[0] = s.str();
          }
          {
            std::stringstream s;
            s << g_trees[idx].pos.x;
            row[1] = s.str();
          }
          {
            std::stringstream s;
            s << g_trees[idx].pos.y;
            row[2] = s.str();
          }
          scene_csv.AddRow(idx, row);
        }
        scene_csv.SaveFile();
      }
    } else { // is_edit_mode
      if (IsKeyDownward(kKeyMouseLeft)) {
        //  hc.walk_vel_multiplier = 3.f;
        Character &hc = g_characters[g_human_idx];
        if (!hc.is_dead) {
          if (target_character) {
            switch (target_character->type) {
              case Character::kChPlayer:
                break;
              case Character::kChTalker:
                hc.dst_hit_info = g_last_frame_hit;
                break;
              case Character::kChAgressive:
              case Character::kChPosition:
              case Character::kChReversive:
                hc.dst_hit_info = g_last_frame_hit;
                hc.is_attacking = true;
                hc.is_walking = true;
                // LaunchAnArrow(hc, Vec2F(MousePos()) + g_view_pos, target_character, 50.f);
                break;
            }
          } else {
            hc.dst_hit_info = g_last_frame_hit;
            if (hc.dst_hit_info.type == HitInfo::kMap) {
              hc.dst_hit_info.pos = Vec2F(MousePos()) + g_view_pos;
            }
          }
        }
      }
    } // else is_edit_mode

    if (IsKeyDownward(kKeySpace)) {
      is_paused = !is_paused;
    }
    if (IsKeyDownward(kKeyI)) {
      g_show_variables = !g_show_variables;
    }

    g_view_pos = g_characters[g_human_idx].pos - Vec2F(ScreenSize())*0.5f;

    if (!is_paused) {
      for (size_t i = 0; i < g_characters.size(); ++i) {
        g_characters[i].UpdateMovement(dt);
        g_characters[i].UpdateAnimation(dt);
      }
      for (Si64 i = (Si64)g_arrows.size()-1; i >= 0; --i) {
        if (g_arrows[(size_t)i].is_flying) {
          Vec2F target = g_arrows[(size_t)i].target_pos;
          if (g_arrows[(size_t)i].target_character) {
            target = g_arrows[(size_t)i].target_character->pos +
              Vec2F(0, g_arrows[(size_t)i].target_character->GetApproxHeight() * 0.66f);
          }
          Vec2F to_target = target - g_arrows[(size_t)i].pos;
          float distance = Length(to_target);
          float new_distance = std::max(0.f, distance - float(dt) * 1500.f);
          float multiplier = 0.f;
          if (distance > 1.f) {
            multiplier = new_distance / distance;
          }

          g_arrows[(size_t)i].pos = target - to_target * multiplier;
          if (new_distance <= 1.f) {
            g_arrows[(size_t)i].is_flying = false;
            if (g_arrows[(size_t)i].target_character && g_arrows[(size_t)i].target_character->hp > 0.f) {
              g_arrows[(size_t)i].target_character->hp -= g_arrows[(size_t)i].damage;
              if (g_arrows[(size_t)i].target_character->hp <= 0.f) {
                g_arrows[(size_t)i].target_character->Die();
              }
              g_snd_pain.Play();
              g_arrows[(size_t)i] = g_arrows[g_arrows.size()-1];
              g_arrows.pop_back();
            } else {
              g_snd_fall.Play();
            }
          }
        }
      }
    }

    ////// DRAW
    //Clear();
    g_map.Draw(Vec2Si32(-1 * g_view_pos));
    g_last_frame_hit.type = HitInfo::kMap;
    g_last_frame_hit.label = std::string();
    Vec2F mouse_pos = Vec2F(MousePos());

    struct DrawItem {
      float y;
      Character *character = nullptr;
      Box *box = nullptr;
      Arrow *arrow = nullptr;
      Tree *tree = nullptr;
    };
    std::vector<DrawItem> drawitems;
    for (size_t i = 0; i < g_characters.size(); ++i) {
      DrawItem di;
      di.character = &g_characters[i];
      di.y = g_characters[i].pos.y;
      drawitems.push_back(di);
    }
    for (auto it = g_boxes.begin(); it != g_boxes.end(); ++it) {
      DrawItem di;
      di.box = &it->second;
      di.y = it->second.pos.y;
      drawitems.push_back(di);
    }
    for (auto it = g_arrows.begin(); it != g_arrows.end(); ++it) {
      DrawItem di;
      di.arrow = &*it;
      di.y = it->pos.y;
      drawitems.push_back(di);
    }
    for (auto it = g_trees.begin(); it != g_trees.end(); ++it) {
      DrawItem di;
      di.tree = &*it;
      di.y = it->pos.y;
      drawitems.push_back(di);
    }
    std::sort(drawitems.begin(), drawitems.end(), [](const DrawItem &a, const DrawItem &b){
      return a.y > b.y;
    });

    float y_threshold = ScreenSize().y/2;
    for (size_t n = 0; n < drawitems.size(); ++n) {
      DrawItem &di = drawitems[n];
      if (di.character) {
        bool is_hit = di.character->Draw(g_view_pos, mouse_pos);
        if (is_hit) {
          g_last_frame_hit.type = HitInfo::kCharacter;
          g_last_frame_hit.label = di.character->label;
          g_last_frame_hit.pos = di.character->pos;
        }
      }
      if (di.box) {
        Vec2F pos = di.box->pos;
        Sprite box_sprite;
        if (di.box->is_loot) {
          box_sprite = g_loot;
        } else {
          box_sprite = g_box;
        }
        box_sprite.Draw(Vec2Si32(pos - g_view_pos));
        Vec2Si32 rel(mouse_pos - (pos - g_view_pos));
        if (Porbe(box_sprite, rel)) {
          g_last_frame_hit.type = HitInfo::kBox;
          g_last_frame_hit.label = di.box->label;
          g_last_frame_hit.pos = di.box->pos;
        }
      }
      if (di.arrow) {
        for (int x = -1; x < 2; ++x) {
          for (int y = -1; y < 2; ++y) {
            DrawLine(Vec2Si32(di.arrow->pos - g_view_pos) + Vec2Si32(x, y),
                     Vec2Si32(di.arrow->pos - di.arrow->direction * 100.f - g_view_pos)+ Vec2Si32(x, y),
                     Rgba(255,224,160));
          }
        }
      }
      if (di.tree) {
        size_t idx = di.tree->tree_type_idx;
        if (idx < g_tree_types.size()) {
          Vec2F pos = di.tree->pos - g_view_pos - g_tree_types[idx].base;
          if (di.tree->pos.y - g_view_pos.y < y_threshold) {
            di.tree->prev_alpha = std::max(di.tree->prev_alpha - 100.f*float(dt), 192.f);
          } else {
            di.tree->prev_alpha = std::min(di.tree->prev_alpha + 100.f*float(dt), 255.f);
          }

          if (di.tree->prev_alpha != 255.f) {
            g_tree_types[idx].sprite.Draw(
              Vec2Si32(pos), kDrawBlendingModeColorize, kFilterNearest,
              Rgba(255,255,255,Ui8(di.tree->prev_alpha)));
          } else {
            g_tree_types[idx].sprite.Draw(Vec2Si32(pos));
          }
        } else {
          g_placeholder.Draw(Vec2Si32(di.tree->pos- g_view_pos));
        }
      }
    }

    g_border_1.Draw(0, 0);
    Vec2Si32 to_size = Vec2Si32(Si32((224-17) * g_characters[g_human_idx].hp / g_characters[g_human_idx].max_hp + 17),
                                Si32(g_bar_1.Size().y));
    g_bar_1.Draw(Vec2Si32(0,0), to_size, Vec2Si32(0,0), to_size);

    g_border_0.Draw(ScreenSize().x-g_border_0.Width(), 0);
    g_bar_0.Draw(ScreenSize().x-g_border_0.Width(), 0);

    if (is_paused) {
      float t = float((sin(Time()*6.28*2.0)*0.5+0.5)*100.f+155.f);
      g_font.Draw(u8"ПАУЗА",
                ScreenSize().x/2, ScreenSize().y/2, kTextOriginBottom, kDrawBlendingModeColorize, kFilterNearest,
                Rgba(Ui8(t), Ui8(t), Ui8(t)));
    }
    if (g_show_variables) {
      ShowVariables();
    }

    char score[128];
    snprintf(score, sizeof(score), u8"mouse pos: (%f, %f), hit: %s FPS: %f",
             float(MousePos().x + g_view_pos.x), float(MousePos().y + g_view_pos.y),
             g_last_frame_hit.label.c_str(),
             float(1.0/(dt>0.0 ? dt : 1.0)));
    g_font.Draw(score, 0, ScreenSize().y, kTextOriginTop);
    ShowFrame();
  }
}
