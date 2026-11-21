#include "ServerCore/CorePch.h"
#include "GameServer/Db/DbConnection.h"

#include <sql.h>
#include <sqlext.h>

#pragma comment(lib, "odbc32.lib")

namespace {

// 실패한 ODBC 호출의 진단 메시지 (SQLSTATE + 설명)
std::string Diagnose(SQLSMALLINT handleType, SQLHANDLE handle)
{
	SQLWCHAR state[6] = {};
	SQLWCHAR text[512] = {};
	SQLINTEGER native = 0;
	SQLSMALLINT length = 0;
	if (!SQL_SUCCEEDED(SQLGetDiagRecW(handleType, handle, 1, state, &native, text, 512, &length)))
		return "알 수 없는 ODBC 오류";
	const std::wstring wide = std::wstring(reinterpret_cast<wchar_t*>(state)) + L" " + reinterpret_cast<wchar_t*>(text);
	const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(size), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
	return out;
}

// 문장 핸들을 범위가 끝나면 닫아 준다
struct Statement
{
	SQLHSTMT handle = SQL_NULL_HSTMT;
	explicit Statement(void* dbc) { SQLAllocHandle(SQL_HANDLE_STMT, static_cast<SQLHDBC>(dbc), &handle); }
	~Statement()
	{
		if (handle != SQL_NULL_HSTMT)
			SQLFreeHandle(SQL_HANDLE_STMT, handle);
	}
};

// 갱신: 행이 없으면 넣고, 있으면 더 높을 때만 바꾼다 — 한 문장이라 동시에 두 판이 끝나도 안전 (HOLDLOCK)
constexpr const wchar_t* SaveSql =
	L"MERGE dbo.PlayerRecord WITH (HOLDLOCK) AS t "
	L"USING (SELECT CAST(? AS NVARCHAR(16)) AS nickname, CAST(? AS BIGINT) AS score, CAST(? AS SMALLINT) AS character_id) AS s "
	L"ON t.nickname = s.nickname "
	L"WHEN MATCHED AND t.best_score < s.score THEN "
	L"  UPDATE SET best_score = s.score, character_id = s.character_id, updated_at = SYSUTCDATETIME() "
	L"WHEN NOT MATCHED THEN "
	L"  INSERT (nickname, best_score, character_id) VALUES (s.nickname, s.score, s.character_id);";

constexpr const wchar_t* QuerySql = L"SELECT best_score FROM dbo.PlayerRecord WHERE nickname = ?";

} // namespace

std::wstring Utf8ToWide(const std::string& text)
{
	if (text.empty())
		return {};
	const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(size), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
	return out;
}

DbConnection::~DbConnection()
{
	Disconnect();
}

bool DbConnection::Connect(const std::wstring& connectionString, std::string& error)
{
	Disconnect();
	SQLHENV env = SQL_NULL_HENV;
	SQLHDBC dbc = SQL_NULL_HDBC;
	if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env)))
	{
		error = "ODBC 환경을 만들지 못함";
		return false;
	}
	SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
	if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc)))
	{
		error = Diagnose(SQL_HANDLE_ENV, env);
		SQLFreeHandle(SQL_HANDLE_ENV, env);
		return false;
	}
	SQLSetConnectAttr(dbc, SQL_ATTR_LOGIN_TIMEOUT, reinterpret_cast<SQLPOINTER>(5), 0);	// 5초 안에 못 붙으면 포기

	std::wstring cs = connectionString;
	const SQLRETURN rc = SQLDriverConnectW(dbc, nullptr, reinterpret_cast<SQLWCHAR*>(cs.data()), SQL_NTS,
		nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
	if (!SQL_SUCCEEDED(rc))
	{
		error = Diagnose(SQL_HANDLE_DBC, dbc);
		SQLFreeHandle(SQL_HANDLE_DBC, dbc);
		SQLFreeHandle(SQL_HANDLE_ENV, env);
		return false;
	}
	_env = env;
	_dbc = dbc;
	return true;
}

void DbConnection::Disconnect()
{
	if (_dbc != nullptr)
	{
		SQLDisconnect(static_cast<SQLHDBC>(_dbc));
		SQLFreeHandle(SQL_HANDLE_DBC, static_cast<SQLHDBC>(_dbc));
		_dbc = nullptr;
	}
	if (_env != nullptr)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, static_cast<SQLHENV>(_env));
		_env = nullptr;
	}
}

bool DbConnection::QueryBest(const std::wstring& nickname, std::optional<uint32_t>& best, std::string& error)
{
	best.reset();
	Statement stmt(_dbc);
	SQLLEN nickLen = SQL_NTS;
	SQLBindParameter(stmt.handle, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 16, 0,
		const_cast<wchar_t*>(nickname.c_str()), 0, &nickLen);
	if (!SQL_SUCCEEDED(SQLExecDirectW(stmt.handle, const_cast<SQLWCHAR*>(reinterpret_cast<const SQLWCHAR*>(QuerySql)), SQL_NTS)))
	{
		error = Diagnose(SQL_HANDLE_STMT, stmt.handle);
		return false;
	}
	if (SQLFetch(stmt.handle) == SQL_SUCCESS)
	{
		SQLBIGINT value = 0;
		SQLLEN indicator = 0;
		SQLGetData(stmt.handle, 1, SQL_C_SBIGINT, &value, sizeof(value), &indicator);
		if (indicator != SQL_NULL_DATA && value >= 0)
			best = static_cast<uint32_t>(value);
	}
	return true;
}

bool DbConnection::SaveScore(const std::wstring& nickname, uint16_t characterId, uint32_t score, bool& updated, std::string& error)
{
	updated = false;
	Statement stmt(_dbc);
	SQLLEN nickLen = SQL_NTS;
	SQLBIGINT scoreValue = score;
	SQLSMALLINT character = static_cast<SQLSMALLINT>(characterId);
	SQLBindParameter(stmt.handle, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 16, 0,
		const_cast<wchar_t*>(nickname.c_str()), 0, &nickLen);
	SQLBindParameter(stmt.handle, 2, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &scoreValue, 0, nullptr);
	SQLBindParameter(stmt.handle, 3, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_SMALLINT, 0, 0, &character, 0, nullptr);
	if (!SQL_SUCCEEDED(SQLExecDirectW(stmt.handle, const_cast<SQLWCHAR*>(reinterpret_cast<const SQLWCHAR*>(SaveSql)), SQL_NTS)))
	{
		error = Diagnose(SQL_HANDLE_STMT, stmt.handle);
		return false;
	}
	SQLLEN rows = 0;
	SQLRowCount(stmt.handle, &rows);
	updated = rows > 0;
	return true;
}
