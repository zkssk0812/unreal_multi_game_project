#pragma once

// 콘텐츠 전역 초기화와 브리지가 주입하는 객체.
// main.cpp(브리지)가 시작할 때 인코더와 기록 저장소를 넣어 주고, 콘텐츠는 Encoder()/Records()로 꺼내 쓴다.
// 시험에서는 FakeEncoder(와 가짜 저장소)를 넣는다. 콘텐츠는 어느 쪽인지 모른다.

namespace Content {

class IMessageEncoder;
class IRecordStore;
class ILobbyApi;

// 인코더·저장소는 콘텐츠보다 오래 살아야 한다(Shutdown 이후에 지울 것).
// records가 nullptr이면 기록을 저장하지 않는다(NullRecordStore). Initialize가 로비를 만든다.
void Initialize(IMessageEncoder& encoder, IRecordStore* records = nullptr);
void Shutdown();

IMessageEncoder& Encoder();
IRecordStore& Records();

// 브리지가 로비에 명령을 넣는 입구. 브리지는 구현 클래스(GameLobby)를 모른다.
ILobbyApi& Lobby();

const char* Version();

} // namespace Content
