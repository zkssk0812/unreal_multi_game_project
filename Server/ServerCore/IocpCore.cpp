#include "IocpCore.h"
#include "IocpEvent.h"

IocpCore::IocpCore()
{
	_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (_iocp == nullptr)
		LOG_ERROR("CreateIoCompletionPort 실패: {}", ::GetLastError());
}

IocpCore::~IocpCore()
{
	if (_iocp != nullptr)
		::CloseHandle(_iocp);
}

bool IocpCore::Register(HANDLE handle)
{
	// 완료 키는 쓰지 않는다(0). 누가 처리할지는 IocpEvent::owner가 알려 준다.
	return ::CreateIoCompletionPort(handle, _iocp, 0, 0) != nullptr;
}

bool IocpCore::Dispatch(uint32 timeoutMs)
{
	std::array<OVERLAPPED_ENTRY, kBatch> entries;
	ULONG count = 0;

	if (!::GetQueuedCompletionStatusEx(_iocp, entries.data(), kBatch, &count, timeoutMs, FALSE))
	{
		const DWORD err = ::GetLastError();
		if (err == WAIT_TIMEOUT)
			return true;
		// IOCP포트 핸들이 닫혔다: 앞으로 아무것도 오지 않는다.
		if (err == ERROR_ABANDONED_WAIT_0 || err == ERROR_INVALID_HANDLE)
			return false;
		LOG_ERROR("GetQueuedCompletionStatusEx 실패: {}", err);
		return true;
	}

	// 한 묶음 안에 종료 신호 뒤로 일반 완료가 섞여 있을 수 있다.
	// 버리면 owner 참조가 새므로 끝까지 처리하고 나서 종료를 알린다.
	uint32 shutdownCount = 0;
	for (ULONG i = 0; i < count; ++i)
	{
		const OVERLAPPED_ENTRY& entry = entries[i];
		if (entry.lpCompletionKey == kShutdownKey)
		{
			++shutdownCount;
			continue;
		}

		auto* event = static_cast<IocpEvent*>(entry.lpOverlapped);

		// 진행 중 참조를 이벤트에서 꺼낸다. 지역 변수가 핸들러 동안 대상을 살려 두고,
		// 핸들러는 같은 이벤트에 새 owner를 넣어 곧바로 다시 걸 수 있다.
		IocpObjectRef owner = std::move(event->owner);
		if (!owner)
		{
			LOG_ERROR("owner 없는 완료 (event type {})", static_cast<int>(event->type));
			continue;
		}

		// GQCSEx는 I/O별 성공·실패를 반환값으로 알려 주지 않는다.
		// OVERLAPPED::Internal(NTSTATUS)이 0이 아니면 WSAGetOverlappedResult로 WSA 오류 코드를 얻는다.
		DWORD error = 0;
		if (event->Internal != 0)
		{
			DWORD bytes = 0;
			DWORD flags = 0;
			const auto socket = reinterpret_cast<SOCKET>(owner->GetHandle());
			if (!::WSAGetOverlappedResult(socket, event, &bytes, FALSE, &flags))
				error = static_cast<DWORD>(::WSAGetLastError());
		}

		owner->Dispatch(event, entry.dwNumberOfBytesTransferred, error);
	}

	if (shutdownCount == 0)
		return true;

	// 종료 신호는 Worker마다 1개씩 보냈지만, 한 묶음에 여러 개가 들어올 수 있다.
	// 내 몫 1개만 쓰고 나머지는 다시 넣어야 다른 Worker도 종료 신호를 받는다(안 그러면 JoinAll이 영원히 기다린다).
	for (uint32 i = 1; i < shutdownCount; ++i)
		::PostQueuedCompletionStatus(_iocp, 0, kShutdownKey, nullptr);
	return false;
}

void IocpCore::PostShutdown(uint32 workerCount)
{
	for (uint32 i = 0; i < workerCount; ++i)
		::PostQueuedCompletionStatus(_iocp, 0, kShutdownKey, nullptr);
}
