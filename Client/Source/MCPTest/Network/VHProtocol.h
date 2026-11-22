#pragma once

// 서버 팀원의 protocol.h를 내용 그대로 쓰기 위한 래퍼.
// protocol.h는 서버와 동기화해야 하므로 고치지 않고, 언리얼 타입·이름과 충돌하지 않게
// RobProto 네임스페이스 안에 넣는다. 표준 헤더는 네임스페이스 밖에서 먼저 include해 둔다
// (protocol.h 안의 #include는 이미 포함된 헤더라 다시 펼쳐지지 않는다).
// 다른 파일에서는 protocol.h를 직접 include하지 말고 반드시 이 헤더를 쓴다.

#include <cstddef>
#include <cstdint>

namespace RobProto
{
#include "Protocol/protocol.h"
}
