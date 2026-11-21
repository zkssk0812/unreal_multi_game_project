#pragma once

// 닉네임·방 이름 규칙. 상태가 없는 순수 함수라 어느 스레드에서 불러도 된다.
//
//   1) 앞뒤 공백(스페이스·탭)을 지운다
//   2) 올바른 UTF-8이어야 한다 (깨진 바이트가 있으면 거절)
//   3) 제어 문자(0x00~0x1F, 0x7F)가 없어야 한다
//   4) 글자 수가 1 ~ maxChars (한글·영문 모두 한 글자 = 1)
//
// 통과하면 정리된 이름을, 아니면 nullopt를 돌려준다.

#include <optional>
#include <string>

namespace Content::NameRule {

std::optional<std::string> Normalize(const std::string& input, size_t maxChars);

// 닉네임 중복 비교용 키: 영문 대소문자를 구분하지 않는다("Kim"과 "kim"은 같은 이름)
std::string CompareKey(const std::string& normalized);

} // namespace Content::NameRule
