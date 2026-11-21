#include "ServerCore/CorePch.h"
#include "Content/Game/MapData.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "ThirdParty/nlohmann/json.hpp"
#include "Content/GameData.h"

// mansion_1f_server.json(tools/collision_to_map.py가 만듦) → MapData (서버 시작 때 한 번).
// JSON 파서는 nlohmann/json(헤더 하나, MIT). 필요한 항목이 없거나 타입이 다르면 json::exception을 던지므로
// 한 곳에서 잡아 "어느 항목이 왜 틀렸는지"를 error로 돌려준다. 서버는 그 메시지를 로그로 남기고 시작하지 않는다.

namespace Content {

namespace {

using Json = nlohmann::json;

MapData g_map;			// 서버가 쓰는 맵 (LoadDefaultMap이 채움)
bool g_loaded = false;

// ── JSON 조각 → 구조체 ── (항목 이름은 클라 팀의 맵 파일 그대로)

Vec3 ReadVec3(const Json& j)		// [x, y, z]
{
	return { j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>() };
}

MapArea ReadArea(const Json& j)		// { "xmin": .., "xmax": .., "ymin": .., "ymax": .. }
{
	return { j.at("xmin").get<float>(), j.at("xmax").get<float>(), j.at("ymin").get<float>(), j.at("ymax").get<float>() };
}

MapPoint ReadPoint(const Json& j)	// 스폰
{
	return { ReadVec3(j.at("pos")), j.value("yaw", 0.0f) };
}

MapCollider ReadCollider(const Json& j, bool dynamic)
{
	// { "xmin", "xmax", "ymin", "ymax", "zmin", "zmax", "blocks_move", "blocks_sight" }
	// 지금 맵은 모두 사각형이지만 "shape": "circle"(cx, cy, r)도 읽을 수 있게 둔다
	MapCollider c;
	if (j.value("shape", std::string("box")) == "circle")
	{
		c.shape = MapCollider::Shape::Circle;
		c.cx = j.at("cx").get<float>();
		c.cy = j.at("cy").get<float>();
		c.r = j.at("r").get<float>();
	}
	else
	{
		c.shape = MapCollider::Shape::Box;
		c.xmin = j.at("xmin").get<float>();
		c.xmax = j.at("xmax").get<float>();
		c.ymin = j.at("ymin").get<float>();
		c.ymax = j.at("ymax").get<float>();
	}
	c.zMin = j.at("zmin").get<float>();
	c.zMax = j.at("zmax").get<float>();
	c.blocksMovement = j.at("blocks_move").get<bool>();
	c.blocksSight = j.at("blocks_sight").get<bool>();
	c.dynamic = dynamic;		// 열리고 닫히는 것 (colliders.dynamic, 지금 맵에는 없음)
	return c;
}

// 배열의 원소마다 read를 적용해 vector로
template <class T, class Read>
std::vector<T> ReadList(const Json& array, Read read)
{
	std::vector<T> out;
	out.reserve(array.size());
	for (const Json& item : array)
		out.push_back(read(item));
	return out;
}

void Parse(const Json& d, MapData& m)
{
	const Json& meta = d.at("meta");
	m.mapId = 1;										// 맵이 하나뿐이라 SC_GAME_LOAD의 map_id는 1
	m.key = meta.at("map_id").get<std::string>();
	m.name = meta.value("display_name", std::string{});
	m.version = meta.at("version").get<uint32_t>();
	m.bounds = ReadArea(meta.at("bounds"));

	m.thiefSpawns = ReadList<MapPoint>(d.at("spawns").at("thief"), ReadPoint);
	m.guardSpawns = ReadList<MapPoint>(d.at("spawns").at("guard"), ReadPoint);
	// 방: 반납 지점·금고·아이템이 놓이는 곳. 모양은 직사각형 여러 개(rects), 없으면 xmin~ymax 하나
	m.rooms = ReadList<MapRoom>(d.at("rooms"), [](const Json& j) {
		MapRoom room;
		room.id = j.at("id").get<std::string>();
		room.name = j.value("name", std::string{});
		if (j.contains("rects"))
			room.areas = ReadList<MapArea>(j.at("rects"), ReadArea);
		else
			room.areas.push_back(ReadArea(j));
		if (room.areas.empty())
			throw std::runtime_error("방 " + room.id + "의 rects가 비어 있음");
		room.returnPoint = j.value("return_point", true);
		return room;
	});

	// 순찰 지점: id 순서대로 넣는다 (연결선이 id로 가리키므로 id == 배열 위치여야 한다)
	const Json& nodes = d.at("patrol").at("nodes");
	m.patrolNodes.assign(nodes.size(), Vec3{});
	for (const Json& n : nodes)
	{
		const size_t id = n.at("id").get<size_t>();
		if (id >= m.patrolNodes.size())
			throw std::runtime_error("patrol.nodes의 id " + std::to_string(id) + "가 개수보다 큼");
		m.patrolNodes[id] = ReadVec3(n.at("pos"));
	}
	m.patrolEdges = ReadList<MapPatrolEdge>(d.at("patrol").at("edges"), [](const Json& e) {
		return MapPatrolEdge{ e.at(0).get<uint16_t>(), e.at(1).get<uint16_t>(), e.size() > 2 ? e.at(2).get<float>() : 0.0f };
	});	// [a, b, 거리cm]

	// 바닥 (FBX의 바닥판, 없어도 됨): 있으면 이 위만 걸을 수 있다
	if (d.contains("floors"))
		m.floors = ReadList<MapArea>(d.at("floors"), ReadArea);

	const Json& colliders = d.at("colliders");
	for (const Json& c : colliders.at("static"))
		m.colliders.push_back(ReadCollider(c, false));
	if (colliders.contains("dynamic"))
		for (const Json& c : colliders.at("dynamic"))
			m.colliders.push_back(ReadCollider(c, true));

	// 경로 격자: 틀(원점·칸 크기·행·열)만 읽고 칸은 BuildNavGrid가 충돌체로 만든다 (rows_data는 쓰지 않음)
	const Json& nav = d.at("nav_grid");
	m.nav.cellSize = nav.at("cell").get<float>();
	m.nav.rows = nav.at("rows").get<int>();
	m.nav.cols = nav.at("cols").get<int>();
	m.nav.originX = nav.at("origin_x").get<float>();
	m.nav.originY = nav.at("origin_y").get<float>();
	if (m.nav.cellSize <= 0.0f || m.nav.rows <= 0 || m.nav.cols <= 0 || m.nav.rows * m.nav.cols > 1000000)
		throw std::runtime_error("nav_grid의 크기가 잘못됨");
}

} // namespace

// ───────────────── 읽기 ─────────────────

bool LoadMapJson(const std::string& jsonText, MapData& out, std::string& error)
{
	try
	{
		MapData map;
		Parse(Json::parse(jsonText), map);		// 문법 오류·항목 없음·타입 다름 → 예외
		BuildNavGrid(map);
		BuildReturnCells(map);
		error = ValidateMap(map);
		if (!error.empty())
			return false;
		map.warnings = MapWarnings(map);
		out = std::move(map);
		return true;
	}
	catch (const std::exception& e)
	{
		// nlohmann의 메시지에는 어느 항목이 문제인지 들어 있다. 예) [json.exception.out_of_range.403] key 'rooms' not found
		error = e.what();
		return false;
	}
}

// filesystem::path로 받는 이유: 경로에 한글(예: C:\Users\김정호)이 있으면 std::string 경로는 Windows 코드 페이지 때문에
// 파일을 못 여는 일이 생긴다. path는 내부적으로 wchar_t(UTF-16)라 안전하다.
bool LoadMapFile(const std::filesystem::path& path, MapData& out, std::string& error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		const std::u8string utf8 = path.u8string();
		error = "파일을 열 수 없음: " + std::string(utf8.begin(), utf8.end());
		return false;
	}
	std::stringstream text;
	text << file.rdbuf();
	return LoadMapJson(text.str(), out, error);
}

// ───────────────── 경로 격자 만들기 ─────────────────

void BuildNavGrid(MapData& map)
{
	NavGrid& nav = map.nav;
	const float pad = GameData::NavWallPaddingCm;
	const float cell = nav.cellSize;

	// ① 바닥: 바닥 정보가 있으면 모두 막힘에서 시작해 칸 가운데가 바닥 위인 칸만 연다 (건물 밖은 막힘)
	if (map.floors.empty())
	{
		nav.cells.assign(static_cast<size_t>(nav.rows), std::string(static_cast<size_t>(nav.cols), '.'));
	}
	else
	{
		nav.cells.assign(static_cast<size_t>(nav.rows), std::string(static_cast<size_t>(nav.cols), '#'));
		for (int r = 0; r < nav.rows; ++r)
			for (int k = 0; k < nav.cols; ++k)
			{
				const Vec3 center = nav.ToWorld(r, k);
				for (const MapArea& floor : map.floors)
					if (floor.Contains(center))
					{
						nav.cells[static_cast<size_t>(r)][static_cast<size_t>(k)] = '.';
						break;
					}
			}
	}

	// ② 벽: 넓혀서 막는다

	for (const MapCollider& c : map.colliders)
	{
		// 있다 없다 하는 것(금고 10곳 중 5곳만 있음, 창고 문·탈출구는 열림)은 막지 않는다
		if (!c.blocksMovement || c.dynamic)
			continue;
		const bool box = c.shape == MapCollider::Shape::Box;
		const float x0 = (box ? c.xmin : c.cx - c.r) - pad, x1 = (box ? c.xmax : c.cx + c.r) + pad;
		const float y0 = (box ? c.ymin : c.cy - c.r) - pad, y1 = (box ? c.ymax : c.cy + c.r) + pad;

		// 넓힌 사각형과 "조금이라도" 겹치는 칸 (클라 격자는 칸 가운데만 봐서 얇은 벽이 빠졌다)
		//   행 r은 x ∈ [originX − (r+1)·cell, originX − r·cell], 열 k는 y ∈ [originY + k·cell, originY + (k+1)·cell]
		const int rowLo = std::max(0, static_cast<int>(std::floor((nav.originX - x1) / cell)));
		const int rowHi = std::min(nav.rows - 1, static_cast<int>(std::ceil((nav.originX - x0) / cell)) - 1);
		const int colLo = std::max(0, static_cast<int>(std::floor((y0 - nav.originY) / cell)));
		const int colHi = std::min(nav.cols - 1, static_cast<int>(std::ceil((y1 - nav.originY) / cell)) - 1);
		const char mark = c.blocksSight ? '#' : '~';		// 시야는 통과하는 낮은 장애물은 '~'

		for (int r = rowLo; r <= rowHi; ++r)
		{
			for (int k = colLo; k <= colHi; ++k)
			{
				if (!box)
				{
					// 원: 칸 안에서 원 중심에 가장 가까운 점까지의 거리로 판정
					const float px = std::clamp(c.cx, nav.originX - (r + 1) * cell, nav.originX - r * cell);
					const float py = std::clamp(c.cy, nav.originY + k * cell, nav.originY + (k + 1) * cell);
					const float dx = px - c.cx, dy = py - c.cy, reach = c.r + pad;
					if (dx * dx + dy * dy > reach * reach)
						continue;
				}
				char& ch = nav.cells[static_cast<size_t>(r)][static_cast<size_t>(k)];
				if (ch != '#')
					ch = mark;
			}
		}
	}
	nav.LabelRegions();
}

void BuildReturnCells(MapData& map)
{
	// 방 안의 칸 중: 걸을 수 있고('.'), 첫 의적 스폰과 이어진 칸의 가운데.
	// 벽·가구에서 NavWallPaddingCm 떨어진 칸만 '.'이므로 반납 지점·금고·상자가 벽에 너무 붙지 않는다
	const NavGrid& nav = map.nav;
	const uint16_t start = map.thiefSpawns.empty() ? 0 : nav.RegionOf(map.thiefSpawns[0].pos);
	for (MapRoom& room : map.rooms)
	{
		room.returnCells.clear();
		if (!room.returnPoint || start == 0)
			continue;
		for (int r = 0; r < nav.rows; ++r)
			for (int k = 0; k < nav.cols; ++k)
			{
				const Vec3 center = nav.ToWorld(r, k);
				if (room.Contains(center) && nav.RegionOf(r, k) == start)
					room.returnCells.push_back(center);
			}
	}
}

// ───────────────── 규칙 검사 ─────────────────

std::string ValidateMap(const MapData& map)
{
	// 하나라도 어긋나면 이유를 돌려준다. 게임 중에 이상한 일(벽 안에서 스폰, 금고에 못 감 등)이 생기기 전에, 서버 시작 때 드러나게.
	if (map.thiefSpawns.size() < GameData::MaxRoomPlayers)
		return "의적 스폰이 " + std::to_string(GameData::MaxRoomPlayers) + "곳보다 적음";
	// 반납 지점은 항상 5곳이고, 반납하면 "다른 방"으로 옮겨 가므로 놓을 수 있는 방이 5곳보다 많아야 한다
	const auto returnRooms = std::count_if(map.rooms.begin(), map.rooms.end(),
		[](const MapRoom& room) { return !room.returnCells.empty(); });
	if (static_cast<size_t>(returnRooms) <= GameData::ReturnPointCount)
		return "반납 지점을 놓을 수 있는 방이 " + std::to_string(GameData::ReturnPointCount + 1) + "곳보다 적음";
	// 의적 셋이 서로 다른 방에서 시작하고, 첫 금고는 그 방들을 빼고 고르므로 그보다 1곳 이상 많아야 한다
	if (static_cast<size_t>(returnRooms) < GameData::SafeRoomsNeeded)
		return "금고를 놓을 수 있는 방이 " + std::to_string(GameData::SafeRoomsNeeded) + "곳보다 적음";

	const NavGrid& nav = map.nav;
	if (nav.cellSize <= 0.0f || nav.rows <= 0 || nav.cols <= 0 || nav.cells.size() != static_cast<size_t>(nav.rows))
		return "nav_grid의 크기가 잘못됨";
	for (const std::string& row : nav.cells)
		if (row.size() != static_cast<size_t>(nav.cols))
			return "nav_grid의 행 길이가 cols와 다름";
	for (const MapPatrolEdge& e : map.patrolEdges)
		if (e.a >= map.patrolNodes.size() || e.b >= map.patrolNodes.size())
			return "순찰 연결이 없는 지점을 가리킴";

	// 의적이 서 있어야 하는 곳: 이동 가능한 칸이고, 첫 번째 의적 스폰에서 걸어서 닿아야 한다
	const Vec3 start = map.thiefSpawns[0].pos;
	auto check = [&](const Vec3& pos, const std::string& what) -> std::string {
		if (!nav.IsWalkable(pos))
			return what + "이(가) 이동할 수 없는 칸에 있음";
		if (!nav.Connected(start, pos))
			return what + "에 의적 스폰에서 걸어서 갈 수 없음";
		return {};
	};
	std::string error;
	for (size_t i = 0; i < map.thiefSpawns.size() && error.empty(); ++i)
		error = check(map.thiefSpawns[i].pos, "의적 스폰 " + std::to_string(i));
	if (!error.empty())
		return error;

	for (size_t i = 0; i < map.guardSpawns.size(); ++i)
		if (!nav.IsWalkable(map.guardSpawns[i].pos))
			return "경비원 스폰 " + std::to_string(i) + "이 이동할 수 없는 칸에 있음";
	return {};
}

std::vector<std::string> MapWarnings(const MapData& map)
{
	// 게임은 돌지만 맵 팀에 알려야 할 것: 경비원이 갇혀서 나오지 못하거나, 갈 수 없는 순찰 지점
	std::vector<std::string> warnings;
	const NavGrid& nav = map.nav;
	const Vec3 start = map.thiefSpawns.empty() ? Vec3{} : map.thiefSpawns[0].pos;
	for (size_t i = 0; i < map.guardSpawns.size(); ++i)
		if (!nav.Connected(start, map.guardSpawns[i].pos))
			warnings.push_back("경비원 스폰 " + std::to_string(i) + "이 의적 쪽과 이어지지 않음 (문 없는 방?) → 그 경비원은 밖으로 나오지 못함");
	for (size_t i = 0; i < map.patrolNodes.size(); ++i)
	{
		if (!nav.IsWalkable(map.patrolNodes[i]))
			warnings.push_back("순찰 지점 " + std::to_string(i) + "이 이동할 수 없는 칸에 있음 → 경비원이 건너뜀");
		else if (!nav.Connected(start, map.patrolNodes[i]))
			warnings.push_back("순찰 지점 " + std::to_string(i) + "이 의적 쪽과 이어지지 않음 → 경비원이 건너뜀");
	}
	for (const MapRoom& room : map.rooms)
		if (room.returnPoint && room.returnCells.empty())
			warnings.push_back("방 " + room.id + "에 놓을 칸이 없음 (문 없는 방?) → 반납 지점·금고·아이템이 생기지 않음");
	return warnings;
}

// ───────────────── 서버가 쓰는 맵 ─────────────────

bool LoadDefaultMap(const std::filesystem::path& path, std::string& error)
{
	// Worker가 돌기 전(서버 시작 때) 한 번만 부른다. 그 뒤로는 아무도 고치지 않으므로 여러 방이 동시에 읽어도 안전하다.
	g_loaded = LoadMapFile(path, g_map, error);
	return g_loaded;
}

const MapData& DefaultMap()
{
	if (!g_loaded)
	{
		LOG_ERROR("[Map] DefaultMap()이 LoadDefaultMap 전에 호출됨");
		std::abort();
	}
	return g_map;
}

} // namespace Content
