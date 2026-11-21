#pragma once

// 최고 기록 저장소 (F-08, psd.md A-21). 구현은 브리지(GameServer/Db/SqlRecordStore: SQL Server).
// 콘텐츠는 "닉네임 → 최고 점수"만 안다. DB가 무엇인지, 어느 스레드에서 도는지 모른다.
//
// - 두 함수 모두 바로 돌아오고(막지 않음), 결과는 나중에 done으로 알린다.
// - done은 아무 스레드에서나 불릴 수 있다. 방은 done 안에서 자기 JobQueue로 일을 넘긴다(DoAsync).
// - DB에 연결할 수 없으면 ok = false로 알린다. 게임은 기록 없이 계속한다.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Content {

class IRecordStore
{
public:
	using BestMap = std::unordered_map<std::string, uint32_t>;		// 닉네임 → 지난 최고 점수 (기록 없으면 빠짐)

	virtual ~IRecordStore() = default;

	// 닉네임들의 지난 최고 기록을 읽는다
	virtual void LoadBest(std::vector<std::string> nicknames, std::function<void(bool ok, BestMap best)> done) = 0;

	// 점수를 저장한다. 지난 최고 기록보다 높을 때만 바뀐다(DB가 한 문장으로 판단)
	virtual void SaveScore(std::string nickname, uint16_t characterId, uint32_t score) = 0;
};

// 저장소가 없을 때(DB 설정 없음, 시험): 기록이 하나도 없는 것처럼 동작한다
class NullRecordStore final : public IRecordStore
{
public:
	void LoadBest(std::vector<std::string>, std::function<void(bool, BestMap)> done) override { done(false, {}); }
	void SaveScore(std::string, uint16_t, uint32_t) override {}
};

} // namespace Content
