#pragma once

// DummyClient.exe phase
//   연결 단계(ClientPhase)별 허용 패킷 규칙을 시나리오마다 새 연결로 확인한다.
//   "보내고 → 기대한 응답이 오는지 → 서버가 FIN으로 끊는지"를 차례로 보는 시험이라
//   IOCP 대신 블로킹 소켓을 쓴다(순서가 코드 순서 그대로라 읽기 쉽다).

int RunPhaseTest();
