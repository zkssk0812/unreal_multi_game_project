#pragma once

// 맵 데이터: 게임 시작 때 엔티티를 어디에 놓을지, 경비원이 어디로 다닐 수 있는지 (docs/DESIGN.md 7.3).
//
// 원본은 클라이언트 팀의 충돌 정보 Data/Maps/Mansion_1F_Collision.json (벽·유리·기둥·소품).
// tools/collision_to_map.py가 여기에 방·순찰 지점·스폰을 더해 Data/Maps/mansion_1f_server.json을 만들고,
// 서버는 시작할 때 이 JSON을 한 번 읽어 MapData 하나를 채운다(MapLoader.cpp). 서버가 꺼질 때까지 메모리에 둔다.
// 모든 방은 const MapData& 로 같은 것을 읽기만 하므로 복사도 락도 없다.
//   맵을 바꾸면: 충돌 파일 교체 → python tools/collision_to_map.py → 서버 재시작 (다시 빌드할 필요 없음)
//
// 금고·상자는 맵에 자리가 없다. 서버가 게임마다 방 안의 빈 칸에서 무작위로 고른다 (psd.md A-05).
// 경로 격자는 파일의 틀(원점·칸 크기·행·열)만 읽고, 칸은 서버가 충돌체로 직접 만든다(BuildNavGrid).
//
// 좌표는 cm, Unreal 월드 좌표 (X = 북, Y = 동, Z = 위, 맵 중심 = 원점). 방향(yaw)은 도, 0 = +X.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Content/Messages/Types.h"

namespace Content {

struct MapPoint					// 스폰 지점
{
	Vec3 pos;
	float yaw = 0.0f;
};

struct MapArea					// 바닥 위의 사각 구역 (맵 범위, 바닥, 방의 한 조각)
{
	float xmin = 0, xmax = 0, ymin = 0, ymax = 0;
	bool Contains(const Vec3& p) const { return p.x >= xmin && p.x <= xmax && p.y >= ymin && p.y <= ymax; }
};

struct MapRoom					// 방: 반납 지점·금고·상자·필드 아이템이 놓이는 곳 (반납 지점은 방마다 최대 1개)
{
	std::string id;				// JSON의 rooms[].id (GuestRoom1, CentralHall, Area_1 …)
	std::string name;
	std::vector<MapArea> areas;	// 방 모양 = 직사각형 여러 개 (복도처럼 꺾인 방). JSON의 rects (없으면 xmin~ymax 하나)
	bool returnPoint = true;	// JSON의 return_point: false면 반납 지점·아이템을 두지 않는다 (복도)
	// 반납 지점·아이템을 놓을 수 있는 칸의 가운데 (방 안, 걸을 수 있고, 의적 스폰에서 걸어서 닿는 칸). LoadMapJson이 채운다.
	// 비어 있으면 그 방에는 아무것도 생기지 않는다 (문 없는 방, 복도)
	std::vector<Vec3> returnCells;

	bool Contains(const Vec3& p) const
	{
		for (const MapArea& a : areas)
			if (a.Contains(p))
				return true;
		return false;
	}
};

struct MapCollider				// 벽·가구. P5 시야 검사(3D)에 쓴다
{
	enum class Shape : uint8_t { Box, Circle };
	Shape shape = Shape::Box;
	float xmin = 0, xmax = 0, ymin = 0, ymax = 0;	// Box
	float cx = 0, cy = 0, r = 0;					// Circle
	float zMin = 0, zMax = 0;						// 높이 범위 (울타리 220, 벽 300 등)
	bool blocksMovement = true;
	bool blocksSight = true;
	bool dynamic = false;							// 창고 문·탈출구처럼 열리고 닫히는 것
};

enum class PatrolNodeKind : uint8_t	// 순찰 지점이 놓인 곳 (JSON의 patrol.nodes[].room)
{
	Room,			// 방 안 (반납 지점을 둘 수 있는 방)
	Door,			// 방 사이 문 ("door")
	Corridor,		// 복도 등 그 밖
};

struct MapPatrolEdge			// 순찰 지점 a ↔ b 가 이어져 있음
{
	uint16_t a = 0;
	uint16_t b = 0;
	float distCm = 0.0f;
};

// 경로 찾기용 격자 (P5 A*). 한 칸 50cm. '.' 이동 가능, '#' 막힘(벽), '~' 이동만 막힘(낮은 장애물, 시야는 통과)
struct NavGrid
{
	// 구역 번호: 서로 걸어서 닿는 칸끼리 같은 번호(1부터), 막힌 칸은 0. 번호가 다르면 길이 없으므로
	// A*가 맵 전체를 뒤지지 않고 바로 실패한다(문 없는 방으로 가려 할 때 등).
	std::vector<uint16_t> regions;	// rows × cols
	float cellSize = 0.0f;
	int rows = 0;
	int cols = 0;
	float originX = 0.0f;		// 0행 0열 모서리의 월드 좌표
	float originY = 0.0f;
	std::vector<std::string> cells;	// rows개, 각 cols글자

	// 월드 좌표 → 칸. 격자 밖이면 false
	bool ToCell(const Vec3& pos, int& row, int& col) const;
	// 칸 → 칸 가운데의 월드 좌표 (z = 0)
	Vec3 ToWorld(int row, int col) const;
	char At(int row, int col) const;				// 격자 밖은 '#'
	bool IsWalkable(const Vec3& pos) const;			// 그 위치의 칸이 '.' 인가

	void LabelRegions();							// cells로 regions를 채운다
	uint16_t RegionOf(int row, int col) const;		// 격자 밖·막힌 칸·번호 없음은 0
	uint16_t RegionOf(const Vec3& pos) const;
	bool Connected(const Vec3& a, const Vec3& b) const;	// 두 위치가 걸어서 닿나
};

struct MapData
{
	uint32_t mapId = 0;					// 프로토콜의 map_id (SC_GAME_LOAD)
	std::string key;					// JSON의 meta.map_id
	std::string name;					// 표시 이름
	uint32_t version = 0;				// JSON의 meta.version
	MapArea bounds;

	std::vector<MapPoint> thiefSpawns;		// 기준 지점: [0]에서 걸어서 닿는 칸만 쓴다. 의적 시작·리스폰은 방 안 무작위(A-26)이고, 놓을 방이 없을 때만 이 자리
	std::vector<MapPoint> guardSpawns;		// 경비원 시작 위치 (3곳)
	std::vector<MapRoom> rooms;				// 방 (반납 지점·금고·아이템 후보). 엔티티 typeId = 이 배열의 위치

	std::vector<Vec3> patrolNodes;			// 경비원 순찰 지점
	std::vector<PatrolNodeKind> patrolKinds;	// patrolNodes와 같은 순서. 코드로 만든 맵은 비어 있을 수 있다
	std::vector<int16_t> patrolRooms;		// 지점이 놓인 방 (rooms의 위치, 없으면 -1). patrolKinds와 같음
	std::vector<MapPatrolEdge> patrolEdges;
	std::vector<MapCollider> colliders;
	std::vector<MapArea> floors;			// 바닥 (FBX의 바닥판). 있으면 이 위의 칸만 걸을 수 있다 (건물 밖은 막힘)
	NavGrid nav;

	std::vector<std::string> warnings;		// 게임은 돌지만 맵 팀에 알려야 할 문제 (서버가 시작할 때 경고 로그)
};

// ── 맵 읽기 (MapLoader.cpp) ──
// JSON 파일/문자열 → MapData. 실패하면 false와 이유(error). 읽은 뒤 규칙 검사(ValidateMap)까지 한다.
bool LoadMapFile(const std::filesystem::path& path, MapData& out, std::string& error);
bool LoadMapJson(const std::string& jsonText, MapData& out, std::string& error);

// 바닥(floors)과 충돌체(이동을 막는 정적 충돌체)로 경로 격자의 칸과 구역 번호를 만든다.
// 바닥이 있으면 칸 가운데가 바닥 위인 칸만 걸을 수 있고, 벽은 NavWallPaddingCm만큼 넓혀서 막는다
void BuildNavGrid(MapData& map);

// 방마다 반납 지점·아이템·의적 시작 위치를 놓을 수 있는 칸(returnCells)을 모은다. BuildNavGrid 뒤에 부른다
void BuildReturnCells(MapData& map);

// 게임 규칙에 맞는지 (반납 지점·금고를 놓을 방 수, 스폰이 이동 가능한 칸이고 의적이 걸어서 닿는지 등).
// 문제가 없으면 빈 문자열. 게임이 깨지는 문제만 여기서 거절하고, 덜 심각한 것은 MapWarnings로
std::string ValidateMap(const MapData& map);
std::vector<std::string> MapWarnings(const MapData& map);	// 경비원 스폰·순찰 지점이 의적 쪽과 이어지지 않음 등

// 서버가 쓰는 맵. 서버 시작 때 LoadDefaultMap으로 한 번 읽고, 이후 DefaultMap()으로 읽기만 한다.
bool LoadDefaultMap(const std::filesystem::path& path, std::string& error);
const MapData& DefaultMap();

} // namespace Content
