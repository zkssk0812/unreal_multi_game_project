#pragma once

// 블로킹 소켓으로 서버와 "보내고 → 기다렸다 → 확인"하는 시험 도구 (phase 시험용).
// 순서가 코드 순서 그대로라 시나리오 시험에 쓰고, 많은 연결의 부하 시험은 IOCP 세션(main.cpp)을 쓴다.

#include "ServerCore/CorePch.h"
#include "Protocol/protocol.h"
#include "Protocol/ProtocolUtil.h"

namespace TestKit {

// ───────────────── 블로킹 소켓 연결 하나 ─────────────────

struct Received
{
	enum class Kind { Packet, Closed, Reset, Timeout } kind = Kind::Timeout;
	uint16 id = 0;
	std::vector<char> bytes;

	template <class T>
	bool As(PacketId expected, T& out) const
	{
		return kind == Kind::Packet && id == static_cast<uint16>(expected)
			&& Protocol::ReadPacket(bytes.data(), static_cast<uint16>(bytes.size()), out);
	}
};

inline const char* KindName(Received::Kind kind)
{
	switch (kind)
	{
	case Received::Kind::Packet:  return "패킷";
	case Received::Kind::Closed:  return "FIN(정상 종료)";
	case Received::Kind::Reset:   return "RST(재설정)";
	case Received::Kind::Timeout: return "시간 초과";
	}
	return "?";
}

class TestConn
{
public:
	~TestConn()
	{
		if (_socket != INVALID_SOCKET)
			::closesocket(_socket);
	}

	// 소켓을 닫는다 (서버 입장에서는 클라가 끊김)
	void Close()
	{
		if (_socket != INVALID_SOCKET)
			::closesocket(_socket);
		_socket = INVALID_SOCKET;
	}

	bool Connect()
	{
		_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		SOCKADDR_IN address{};
		address.sin_family = AF_INET;
		address.sin_port = ::htons(static_cast<uint16>(PORT));
		::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
		BOOL noDelay = TRUE;
		::setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
		return ::connect(_socket, reinterpret_cast<SOCKADDR*>(&address), sizeof(address)) == 0;
	}

	bool SendRaw(const void* data, size_t len)
	{
		const char* p = static_cast<const char*>(data);
		while (len > 0)
		{
			const int sent = ::send(_socket, p, static_cast<int>(len), 0);
			if (sent <= 0)
				return false;
			p += sent;
			len -= static_cast<size_t>(sent);
		}
		return true;
	}

	template <class T>
	bool Send(const T& packet) { return SendRaw(&packet, packet.size); }

	// 패킷 하나를 받는다. 서버가 끊었으면 Closed(FIN) 또는 Reset(RST).
	Received Recv(uint32 timeoutMs = 2000)
	{
		Received result;
		while (true)
		{
			if (_buffer.size() >= 4)
			{
				uint16 size = 0;
				uint16 id = 0;
				std::memcpy(&size, _buffer.data(), 2);
				std::memcpy(&id, _buffer.data() + 2, 2);
				if (size >= 4 && _buffer.size() >= size)
				{
					result.kind = Received::Kind::Packet;
					result.id = id;
					result.bytes.assign(_buffer.begin(), _buffer.begin() + size);
					_buffer.erase(_buffer.begin(), _buffer.begin() + size);
					return result;
				}
			}

			DWORD timeout = timeoutMs;
			::setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			char chunk[4096];
			const int n = ::recv(_socket, chunk, sizeof(chunk), 0);
			if (n > 0)
			{
				_buffer.insert(_buffer.end(), chunk, chunk + n);
				continue;
			}
			if (n == 0)
				result.kind = Received::Kind::Closed;
			else
				result.kind = ::WSAGetLastError() == WSAETIMEDOUT ? Received::Kind::Timeout : Received::Kind::Reset;
			return result;
		}
	}

private:
	SOCKET _socket = INVALID_SOCKET;
	std::vector<char> _buffer;
};

// ───────────────── 시나리오 도우미 ─────────────────

inline CS_Login MakeLogin(const char* name, uint32 version = PROTOCOL_VERSION)
{
	auto login = Protocol::MakePacket<CS_Login>(PacketId::CS_LOGIN);
	login.protocol_version = version;
	strcpy_s(login.name, name);
	return login;
}

inline CS_CreateRoom MakeCreateRoom(const char* name)
{
	auto create = Protocol::MakePacket<CS_CreateRoom>(PacketId::CS_CREATE_ROOM);
	strcpy_s(create.name, name);
	return create;
}

// SC_KICK(reason)을 받고, 이어서 서버가 FIN으로 끊어야 한다(RST면 SC_KICK을 못 받을 수 있었다는 뜻)
inline std::string ExpectKick(TestConn& conn, KickReason reason)
{
	SC_Kick kick{};
	const Received first = conn.Recv();
	if (!first.As(PacketId::SC_KICK, kick))
		return std::format("SC_KICK 대신 {} (id {})", KindName(first.kind), first.id);
	kick.message[MAX_MESSAGE_LEN] = '\0';
	if (kick.reason != reason)
		return std::format("SC_KICK 이유 {} (기대 {})", static_cast<int>(kick.reason), static_cast<int>(reason));
	const Received after = conn.Recv();
	if (after.kind != Received::Kind::Closed)
		return std::format("SC_KICK \"{}\" 뒤에 {}", kick.message, KindName(after.kind));
	return {};	// 성공
}

// CS_PING을 보내 같은 값의 SC_PONG이 오면 연결이 살아 있고 앞의 패킷들도 다 처리된 것
inline bool ExpectAlive(TestConn& conn, uint64 token)
{
	auto ping = Protocol::MakePacket<CS_Ping>(PacketId::CS_PING);
	ping.client_time_ms = token;
	if (!conn.Send(ping))
		return false;
	SC_Pong pong{};
	return conn.Recv().As(PacketId::SC_PONG, pong) && pong.client_time_ms == token;
}

inline bool LoginOk(TestConn& conn, const char* name, SC_Login& reply)
{
	return conn.Send(MakeLogin(name)) && conn.Recv().As(PacketId::SC_LOGIN, reply) && reply.result == ResultCode::Ok;
}

// 시나리오 결과 집계 (여러 스레드에서 불러도 되게 락)
struct Scoreboard
{
	int passed = 0;
	int failed = 0;
	std::mutex lock;

	void Report(bool ok, const std::string& name, const std::string& detail)
	{
		std::lock_guard guard(lock);
		(ok ? passed : failed)++;
		std::printf("  %s %-58s %s\n", ok ? "[통과]" : "[실패]", name.c_str(), detail.c_str());
	}
};

} // namespace TestKit
