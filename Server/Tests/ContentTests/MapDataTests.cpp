#include "ServerCore/CorePch.h"

#include "Content/Game/MapData.h"
#include "Content/GameData.h"
#include "Content/Game/PathFinder.h"
#include "TestFramework.h"

// 서버 시작 때 Data/Maps/mansion_1f_server.json(← Mansion_1F_Collision.json, tools/collision_to_map.py)을 읽어
// 만든 맵이 제대로 들어왔는지. 맵 파일이 바뀌어 규칙에 안 맞으면(방이 모자람, 스폰이 벽 안 등) 여기서 먼저 걸린다.

using namespace Content;

namespace {
Vec3 FarAwayRoomPoint() { return { 3875.0f, 2075.0f, 0.0f }; }	// 동쪽 객실 GuestRoom7
} // namespace

TEST_CASE(Map_Counts_MatchGameRules)
{
	const MapData& map = DefaultMap();
	CHECK_EQ(map.key, std::string("mansion_1f"));
	CHECK_EQ(map.thiefSpawns.size(), size_t{ 3 });		// 방 최대 3명
	CHECK_EQ(map.guardSpawns.size(), size_t{ 3 });		// 경비원 최대 3명
	CHECK_EQ(map.rooms.size(), size_t{ 35 });			// 벽으로 나뉜 공간 (복도·문 없는 방 포함)
	CHECK(map.colliders.size() > 700);					// 벽 275 + 유리 23 + 기둥 96 + 소품 324 + 닫은 바깥 문 2
}

namespace {
const MapRoom* RoomById(const MapData& map, const std::string& id)
{
	for (const MapRoom& room : map.rooms)
		if (room.id == id)
			return &room;
	return nullptr;
}
} // namespace

TEST_CASE(Map_Rooms_FromWalls)
{
	// 방은 벽으로 막힌 공간 (A-27). 복도·문 없는 방에는 반납 지점·금고·아이템을 두지 않는다
	const MapData& map = DefaultMap();
	const MapRoom* hall = RoomById(map, "CentralHall");
	CHECK(hall != nullptr && hall->Contains({ -300.0f, -600.0f, 0.0f }) && !hall->returnCells.empty());
	CHECK(hall != nullptr && !hall->Contains({ -300.0f, 2700.0f, 0.0f }));		// 현관은 다른 방
	const MapRoom* corridor = RoomById(map, "Corridor");
	CHECK(corridor != nullptr && !corridor->returnPoint && corridor->returnCells.empty());
	CHECK(corridor != nullptr && corridor->areas.size() > 1);					// 꺾인 방 = 직사각형 여러 개
	const MapRoom* closed = RoomById(map, "Area_2");								// 문 없는 방
	CHECK(closed != nullptr && closed->returnCells.empty());
	const auto usable = std::count_if(map.rooms.begin(), map.rooms.end(), [](const MapRoom& r) { return !r.returnCells.empty(); });
	CHECK_EQ(static_cast<size_t>(usable), size_t{ 31 });
}

TEST_CASE(Map_NavGrid_Shape)
{
	const NavGrid& nav = DefaultMap().nav;
	CHECK(nav.cellSize == 50.0f);
	CHECK_EQ(nav.cells.size(), static_cast<size_t>(nav.rows));
	for (const std::string& row : nav.cells)
		CHECK_EQ(row.size(), static_cast<size_t>(nav.cols));
}

TEST_CASE(Map_NavGrid_CellRoundTrip)
{
	// 칸 → 월드 → 칸 이 같은 칸으로 돌아와야 좌표 공식이 맞는 것
	const NavGrid& nav = DefaultMap().nav;
	int row = 0, col = 0;
	const Vec3 center = nav.ToWorld(40, 70);
	CHECK(nav.ToCell(center, row, col) && row == 40 && col == 70);
	CHECK(!nav.ToCell({ 99999.0f, 0.0f, 0.0f }, row, col));		// 맵 밖
	CHECK_EQ(nav.At(-1, 0), '#');									// 격자 밖은 막힘으로 본다
}

TEST_CASE(Map_SpawnsAndInteractPositions_AreWalkable)
{
	// 서 있어야 하는 자리(스폰, 상호작용 위치, 순찰 지점)가 벽 안이면 맵 데이터가 잘못된 것
	const MapData& map = DefaultMap();
	for (const MapPoint& s : map.thiefSpawns)
		CHECK(map.nav.IsWalkable(s.pos));
	for (const MapPoint& s : map.guardSpawns)
		CHECK(map.nav.IsWalkable(s.pos));
	for (const Vec3& p : map.patrolNodes)
		CHECK(map.nav.IsWalkable(p) && map.nav.Connected(map.thiefSpawns[0].pos, p));
	CHECK(map.warnings.empty());
}

TEST_CASE(Map_NavGrid_EveryWallIsBlocked)
{
	// 서버가 충돌체로 격자를 만든다: 이동을 막는 정적 충돌체는 가운데 칸이 반드시 막혀 있어야 한다
	// (클라 격자는 칸 가운데만 봐서 20cm 벽의 40%가 빠져 있었다)
	const MapData& map = DefaultMap();
	int checked = 0;
	for (const MapCollider& c : map.colliders)
	{
		if (!c.blocksMovement || c.dynamic || c.shape != MapCollider::Shape::Box)
			continue;
		CHECK(!map.nav.IsWalkable({ (c.xmin + c.xmax) / 2, (c.ymin + c.ymax) / 2, 0.0f }));
		++checked;
	}
	CHECK(checked > 50);
}

TEST_CASE(Map_OutsideBuilding_NotReachable)
{
	// 바깥 문 2곳은 닫았다 (A-27): 현관 바깥 문 자리는 막혔고, 건물 밖은 의적 쪽과 이어지지 않는다
	const MapData& map = DefaultMap();
	const Vec3 start = map.thiefSpawns[0].pos;
	CHECK(!map.nav.IsWalkable({ -200.0f, 2900.0f, 0.0f }));			// 현관 바깥 문
	CHECK(!map.nav.Connected(start, { 0.0f, 4000.0f, 0.0f }));		// 현관 앞 마당
	CHECK(!map.nav.Connected(start, { 4800.0f, -900.0f, 0.0f }));	// 주방 옆 바깥 문 밖
	CHECK(map.nav.Connected(start, FarAwayRoomPoint()));			// 안쪽은 모두 이어짐
}

TEST_CASE(Map_ThiefCanReachEverythingNeeded)
{
	const MapData& map = DefaultMap();
	const Vec3 start = map.thiefSpawns[0].pos;
	for (const MapRoom& room : map.rooms)
		for (const Vec3& cell : room.returnCells)
			CHECK(map.nav.IsWalkable(cell) && map.nav.Connected(start, cell) && room.Contains(cell));
	for (const MapPoint& g : map.guardSpawns)
		CHECK(map.nav.Connected(start, g.pos));
}

TEST_CASE(Map_UnreachablePoints_PathFailsWithoutSearching)
{
	// 구역 번호가 다르면 A*가 맵 전체를 뒤지지 않고 바로 실패해야 한다 (문 없는 방의 순찰 지점 등)
	const MapData& map = DefaultMap();
	const Vec3 start = map.thiefSpawns[0].pos;
	for (const Vec3& node : map.patrolNodes)
	{
		if (map.nav.Connected(start, node))
			continue;
		std::vector<Vec3> path;
		PathStats stats;
		CHECK(!FindPath(map.nav, start, node, path, &stats));
		CHECK_EQ(stats.expanded, 0u);
	}
}

TEST_CASE(Map_PatrolEdges_PointToNodes)
{
	const MapData& map = DefaultMap();
	for (const MapPatrolEdge& e : map.patrolEdges)
		CHECK(e.a < map.patrolNodes.size() && e.b < map.patrolNodes.size() && e.a != e.b);
}

TEST_CASE(Map_Bounds_ContainSpawns)
{
	const MapData& map = DefaultMap();
	for (const MapPoint& s : map.thiefSpawns)
		CHECK(map.bounds.Contains(s.pos));
	for (const MapPoint& s : map.guardSpawns)
		CHECK(map.bounds.Contains(s.pos));
}

// ───────────────── 읽기 실패 ───────────────── (서버가 시작하지 않고 이유를 로그로 남기는 경우들)

TEST_CASE(MapLoad_MissingFile_Fails)
{
	MapData map;
	std::string error;
	// 한글 경로는 u8"" (UTF-8) 리터럴로 만든다. 그냥 ""로 path를 만들면 Windows 코드 페이지로 해석하다 예외가 난다
	CHECK(!LoadMapFile(std::filesystem::path(u8"없는_파일.json"), map, error));
	CHECK(error.find("파일을 열 수 없음") != std::string::npos);
}

TEST_CASE(MapLoad_BrokenJson_Fails)
{
	MapData map;
	std::string error;
	CHECK(!LoadMapJson("{ \"meta\": ", map, error));	// 문법 오류
	CHECK(!error.empty());
}

TEST_CASE(MapLoad_MissingSection_ReportsWhich)
{
	MapData map;
	std::string error;
	CHECK(!LoadMapJson("{ \"meta\": { \"map_id\": \"x\", \"version\": 1, \"bounds\": "
		"{ \"xmin\": 0, \"xmax\": 1, \"ymin\": 0, \"ymax\": 1 } } }", map, error));
	CHECK(error.find("spawns") != std::string::npos);	// 어느 항목이 없는지 메시지에 나온다
}

TEST_CASE(MapLoad_RuleViolation_Fails)
{
	// 형식은 맞지만 규칙에 안 맞는 맵: 실제 맵을 고쳐 놓을 칸이 있는 방을 반납 지점 수만큼만 남긴다
	// (반납 지점이 반납 때마다 "다른 방"으로 옮겨 가야 하므로 그보다 1곳 이상 필요)
	MapData map = DefaultMap();
	size_t kept = 0;
	for (MapRoom& room : map.rooms)
		if (!room.returnCells.empty() && ++kept > GameData::ReturnPointCount)
			room.returnCells.clear();
	CHECK(ValidateMap(map).find("반납 지점을 놓을 수 있는 방") != std::string::npos);

	MapData wall = DefaultMap();
	wall.thiefSpawns[1].pos = { -1700.0f, 0.0f, 0.0f };		// 중앙 홀 서쪽 벽 안
	CHECK(ValidateMap(wall).find("의적 스폰 1") != std::string::npos);
	CHECK(ValidateMap(DefaultMap()).empty());		// 원래 맵은 통과
}
