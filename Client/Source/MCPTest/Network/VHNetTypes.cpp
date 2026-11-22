#include "Network/VHNetTypes.h"
#include "Network/VHProtocol.h"
#include "Gameplay/VHRanking.h"

using namespace RobProto;

FText UVHNetText::ResultCodeText(int32 Code)
{
	// 서버 프로토콜 v3.0 ResultCode
	switch (static_cast<ResultCode>(Code))
	{
	case ResultCode::Ok:				return FText::FromString(TEXT("성공"));
	case ResultCode::InvalidFormat:		return FText::FromString(TEXT("요청 형식이 올바르지 않습니다."));
	case ResultCode::ServerBusy:		return FText::FromString(TEXT("서버가 혼잡합니다. 잠시 후 다시 시도해 주세요."));
	case ResultCode::InternalError:		return FText::FromString(TEXT("서버 내부 오류가 발생했습니다."));
	case ResultCode::NotAllowedNow:		return FText::FromString(TEXT("지금은 할 수 없는 요청입니다."));
	case ResultCode::NameInUse:			return FText::FromString(TEXT("이미 접속 중인 이름입니다. 다른 이름을 입력해 주세요."));
	case ResultCode::NameInvalid:		return FText::FromString(TEXT("사용할 수 없는 이름입니다."));
	case ResultCode::VersionMismatch:
		// 클라 버전을 함께 보여 줘서 서버 시작 로그의 "protocol 0x..." 와 바로 비교할 수 있게 한다
		return FText::FromString(FString::Printf(
			TEXT("게임 버전이 서버와 다릅니다. (이 클라 프로토콜 v%u.%u = 0x%08X)\n최신 클라이언트를 받고 다시 빌드했는지, 서버도 최신인지 확인해 주세요."),
			PROTOCOL_VERSION >> 16, PROTOCOL_VERSION & 0xFFFF, PROTOCOL_VERSION));
	case ResultCode::RoomNotFound:		return FText::FromString(TEXT("방을 찾을 수 없습니다. 이미 사라진 방일 수 있습니다."));
	case ResultCode::RoomFull:			return FText::FromString(TEXT("방이 가득 찼습니다."));
	case ResultCode::RoomNotWaiting:	return FText::FromString(TEXT("이미 게임이 시작된 방입니다."));
	case ResultCode::AlreadyInRoom:		return FText::FromString(TEXT("이미 방에 들어가 있습니다."));
	case ResultCode::RoomNameInvalid:	return FText::FromString(TEXT("사용할 수 없는 방 이름입니다."));
	case ResultCode::CharacterTaken:	return FText::FromString(TEXT("다른 플레이어가 이미 고른 캐릭터입니다."));
	case ResultCode::CharacterInvalid:	return FText::FromString(TEXT("선택할 수 없는 캐릭터입니다."));
	case ResultCode::PerkInvalid:		return FText::FromString(TEXT("선택할 수 없는 특성입니다. (최대 3개)"));
	case ResultCode::PerkDuplicate:		return FText::FromString(TEXT("같은 특성을 두 번 고를 수 없습니다."));
	case ResultCode::NotReadyable:		return FText::FromString(TEXT("캐릭터를 고른 뒤 준비할 수 있습니다."));
	default: break;
	}
	return FText::FromString(FString::Printf(TEXT("알 수 없는 오류 (코드 %d)"), Code));
}

FText UVHNetText::DifficultyText(EVHDifficulty Difficulty)
{
	switch (Difficulty)
	{
	case EVHDifficulty::Easy:	return FText::FromString(TEXT("쉬움"));
	case EVHDifficulty::Normal:	return FText::FromString(TEXT("보통"));
	case EVHDifficulty::Hard:	return FText::FromString(TEXT("어려움"));
	}
	return FText::GetEmpty();
}

FText UVHNetText::RoomStateText(EVHRoomState State)
{
	switch (State)
	{
	case EVHRoomState::Waiting:	return FText::FromString(TEXT("대기중"));
	case EVHRoomState::Loading:	return FText::FromString(TEXT("로딩"));
	case EVHRoomState::Playing:	return FText::FromString(TEXT("게임중"));
	case EVHRoomState::Result:	return FText::FromString(TEXT("결과"));
	}
	return FText::GetEmpty();
}

FText UVHNetText::MatchResultText(EVHMatchResult Result)
{
	switch (Result)
	{
	case EVHMatchResult::Clear:			return FText::FromString(TEXT("탈출 성공"));
	case EVHMatchResult::PartialClear:	return FText::FromString(TEXT("일부 탈출"));
	case EVHMatchResult::Failed:		return FText::FromString(TEXT("탈출 실패"));
	case EVHMatchResult::Aborted:		return FText::FromString(TEXT("게임 중단"));
	case EVHMatchResult::Finished:		return FText::FromString(TEXT("게임 종료"));
	}
	return FText::GetEmpty();
}

FText UVHNetText::EndReasonText(EVHEndReason Reason)
{
	switch (Reason)
	{
	case EVHEndReason::TimeUp:		return FText::FromString(TEXT("제한 시간 종료"));
	case EVHEndReason::Aborted:		return FText::FromString(TEXT("게임 중단"));
	}
	return FText::GetEmpty();
}

FText UVHNetText::KickReasonText(int32 Reason)
{
	switch (static_cast<KickReason>(Reason))
	{
	case KickReason::ProtocolViolation:	return FText::FromString(TEXT("통신 규칙 위반으로 연결이 끊어졌습니다."));
	case KickReason::ServerShutdown:	return FText::FromString(TEXT("서버가 종료되었습니다."));
	case KickReason::Duplicate:			return FText::FromString(TEXT("같은 이름으로 다른 곳에서 접속했습니다."));
	case KickReason::Admin:				return FText::FromString(TEXT("관리자에 의해 연결이 끊어졌습니다."));
	default: break;
	}
	return FText::FromString(TEXT("서버가 연결을 끊었습니다."));
}

FText UVHNetText::LeaveReasonText(int32 Reason)
{
	switch (static_cast<LeaveReason>(Reason))
	{
	case LeaveReason::Self:			return FText::FromString(TEXT("방에서 나왔습니다."));
	case LeaveReason::Disconnected:	return FText::FromString(TEXT("연결이 끊겨 방에서 나왔습니다."));
	case LeaveReason::GameEnded:	return FText::FromString(TEXT("게임이 끝나 로비로 돌아왔습니다."));
	default: break;
	}
	return FText::FromString(TEXT("방에서 나왔습니다."));
}

FText UVHNetText::DurationText(int32 Ms)
{
	const int32 TotalSec = FMath::Max(0, Ms) / 1000;
	return FText::FromString(FString::Printf(TEXT("%d분 %02d초"), TotalSec / 60, TotalSec % 60));
}

void VHRankResults(TArray<FVHPlayerResult>& Players)
{
	// 반납 금액이 많은 순, 같은 금액이면 같은 등수 (1, 1, 3) — 서버 결과 규칙(A-20)과 같다. 서버 순위가 없을 때(샘플)만 쓴다
	TArray<int32> Values;
	for (const FVHPlayerResult& P : Players)
	{
		Values.Add(P.ValueDelivered);
	}
	const TArray<int32> Ranks = VHRanking::CompetitionRanks(Values);
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		Players[i].Rank = Ranks[i];
	}
	Players.StableSort([](const FVHPlayerResult& A, const FVHPlayerResult& B) { return A.Rank < B.Rank; });
}

