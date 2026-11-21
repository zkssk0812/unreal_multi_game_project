#pragma once

// DummyClient.exe room
//   실제 서버에 3명이 붙어 로비·방·게임 시작 흐름을 확인한다(P2·P3).
//   방 만들기 → 목록 → 참여 → 캐릭터(중복 거절) → 나가기 → 준비 → SC_GAME_LOAD → 늦은 입장 거절
//   → 로딩 완료 → SC_GAME_START → 이동이 상대 스냅샷에 보임 → 행동 거절·손전등 → 모두 끊기면 방 삭제
//   ContentTests가 콘텐츠만 시험한다면, 이 시험은 브리지(디코더·인코더·패킷 표)까지 포함한 전체를 본다.

int RunRoomTest();
