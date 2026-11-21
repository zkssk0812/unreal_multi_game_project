#pragma once

// SQL Server 연결 하나 (ODBC). 한 스레드(SqlRecordStore의 DB 스레드)에서만 쓴다 → 락 없음.
//
// ODBC는 Windows에 기본으로 있는 DB 접속 표준이다. 드라이버("ODBC Driver 18 for SQL Server")만 설치하면
// 접속 문자열로 SQL Server에 붙는다. 순서: 환경(ENV) → 연결(DBC) → 문장(STMT)마다 만들고 → 실행 → 정리.
// 쿼리는 모두 "?" 자리에 값을 따로 묶는다(SQLBindParameter). 닉네임을 SQL 문자열에 붙이지 않으므로
// 닉네임에 따옴표가 들어 있어도 SQL이 깨지거나 주입되지 않는다.

#include <cstdint>
#include <optional>
#include <string>

class DbConnection
{
public:
	DbConnection() = default;
	~DbConnection();
	DbConnection(const DbConnection&) = delete;
	DbConnection& operator=(const DbConnection&) = delete;

	bool Connect(const std::wstring& connectionString, std::string& error);
	void Disconnect();
	bool Connected() const { return _dbc != nullptr; }

	// dbo.PlayerRecord에서 닉네임의 최고 점수. 기록이 없으면 best = nullopt (성공)
	bool QueryBest(const std::wstring& nickname, std::optional<uint32_t>& best, std::string& error);
	// 더 높을 때만 갱신(없으면 넣기). updated = 실제로 바뀌었나
	bool SaveScore(const std::wstring& nickname, uint16_t characterId, uint32_t score, bool& updated, std::string& error);

private:
	void* _env = nullptr;	// SQLHENV (sql.h를 헤더에 퍼뜨리지 않으려고 void*)
	void* _dbc = nullptr;	// SQLHDBC
};

// UTF-8(콘텐츠의 문자열) → UTF-16(SQL Server NVARCHAR)
std::wstring Utf8ToWide(const std::string& text);
