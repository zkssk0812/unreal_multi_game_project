#pragma once

#include "CoreMinimal.h"

/**
 * 전송 계층 인터페이스. 서브시스템은 이 인터페이스만 알고, 실제로는
 *  - FVHTcpTransport : 진짜 서버와 TCP(FSocket)로 통신
 *  - FVHMockTransport: 같은 바이트 패킷을 받아 프로토콜대로 응답하는 인프로세스 가짜 서버
 * 중 하나를 설정(bUseMockServer)에 따라 끼워 쓴다.
 *
 * 둘 다 "바이트 흐름"만 주고받는다. 패킷 재조립과 해석은 서브시스템이 공통으로 한다.
 * 모든 콜백은 Tick() 안에서, 즉 게임 스레드에서 호출된다.
 */
class IVHTransport
{
public:
	virtual ~IVHTransport() = default;

	virtual void Connect(const FString& Host, int32 Port) = 0;
	/** 우리 쪽에서 끊는다. 이후 OnDisconnected는 호출되지 않는다 */
	virtual void Disconnect() = 0;
	virtual bool Send(const TArray<uint8>& Bytes) = 0;
	virtual void Tick(float DeltaSeconds) = 0;
	virtual bool IsConnected() const = 0;
	virtual FString GetDescription() const = 0;

	TFunction<void()> OnConnected;
	TFunction<void(const FString& Reason)> OnConnectFailed;
	/** 상대(서버)가 끊었거나 오류로 끊김 */
	TFunction<void(const FString& Reason)> OnDisconnected;
	TFunction<void(const uint8* Data, int32 Num)> OnBytesReceived;
};
