#include "ServerCore/CorePch.h"
#include "ServerCore/CoreGlobal.h"
#include "Content/ContentGlobal.h"
#include "Content/Game/MapData.h"
#include "TestFramework.h"

#include <filesystem>

// ContentTests.exe [이름 일부]
//   콘텐츠 계층을 소켓 없이 실행하는 시험. GameServer(브리지)를 링크하지 않는다:
//   ServerCore(JobQueue·로그·송신 버퍼) + VaultHeistContent + 가짜 채널·인코더만으로 돈다.

int main(int argc, char* argv[])
{
	ServerCore::LogOptions logOptions;
	logOptions.console = false;		// 콘텐츠 로그는 파일(logs/)로만. 화면에는 시험 결과만
	if (!ServerCore::Initialize("ContentTests", logOptions))
		return 1;

	// 서버와 같은 맵 파일을 읽는다 (빌드 후 실행 파일 옆 Data/Maps로 복사됨)
	wchar_t exe[MAX_PATH] = {};
	::GetModuleFileNameW(nullptr, exe, MAX_PATH);
	const std::filesystem::path mapPath = std::filesystem::path(exe).parent_path() / "Data" / "Maps" / "mansion_1f_server.json";
	std::string mapError;
	if (!Content::LoadDefaultMap(mapPath, mapError))
	{
		std::printf("[ContentTests] 맵을 읽지 못함: %s\n", mapError.c_str());
		ServerCore::Shutdown();
		return 1;
	}

	std::printf("[ContentTests] %s, 소켓 없음\n", Content::Version());
	const int failed = Test::RunAll(argc > 1 ? argv[1] : "");

	ServerCore::Shutdown();
	return failed == 0 ? 0 : 1;
}
