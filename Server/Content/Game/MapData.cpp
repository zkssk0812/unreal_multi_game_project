#include "ServerCore/CorePch.h"
#include "Content/Game/MapData.h"

#include <cmath>

// 격자 계산 도우미. JSON 읽기는 MapLoader.cpp.

namespace Content {

// 격자 좌표 규칙 (맵 파일의 nav_grid.cell_center):
//   X = originX - (row + 0.5) * cellSize   ← 행이 늘수록 남쪽(-X)
//   Y = originY + (col + 0.5) * cellSize   ← 열이 늘수록 동쪽(+Y)

bool NavGrid::ToCell(const Vec3& pos, int& row, int& col) const
{
	if (cellSize <= 0.0f)
		return false;
	row = static_cast<int>(std::floor((originX - pos.x) / cellSize));
	col = static_cast<int>(std::floor((pos.y - originY) / cellSize));
	return row >= 0 && row < rows && col >= 0 && col < cols;
}

Vec3 NavGrid::ToWorld(int row, int col) const
{
	return { originX - (row + 0.5f) * cellSize, originY + (col + 0.5f) * cellSize, 0.0f };
}

char NavGrid::At(int row, int col) const
{
	if (row < 0 || row >= rows || col < 0 || col >= cols)
		return '#';
	return cells[static_cast<size_t>(row)][static_cast<size_t>(col)];
}

bool NavGrid::IsWalkable(const Vec3& pos) const
{
	int row = 0, col = 0;
	return ToCell(pos, row, col) && At(row, col) == '.';
}

void NavGrid::LabelRegions()
{
	// 뚫린 칸에서 시작해 상하좌우로 번져 가며 같은 번호를 붙인다(flood fill).
	// A*는 대각선도 쓰지만 양옆이 뚫려야만 가므로, 상하좌우로 이어진 칸끼리와 갈 수 있는 곳이 같다.
	regions.assign(static_cast<size_t>(rows) * static_cast<size_t>(cols), 0);
	std::vector<int> queue;
	uint16_t next = 0;
	for (int start = 0; start < rows * cols; ++start)
	{
		if (regions[start] != 0 || At(start / cols, start % cols) != '.')
			continue;
		++next;
		regions[start] = next;
		queue.assign(1, start);
		for (size_t head = 0; head < queue.size(); ++head)
		{
			const int r = queue[head] / cols, c = queue[head] % cols;
			static constexpr int DR[4] = { -1, 1, 0, 0 };
			static constexpr int DC[4] = { 0, 0, -1, 1 };
			for (int d = 0; d < 4; ++d)
			{
				const int nr = r + DR[d], nc = c + DC[d];
				if (At(nr, nc) != '.' || regions[nr * cols + nc] != 0)
					continue;
				regions[nr * cols + nc] = next;
				queue.push_back(nr * cols + nc);
			}
		}
	}
}

uint16_t NavGrid::RegionOf(int row, int col) const
{
	if (row < 0 || row >= rows || col < 0 || col >= cols || regions.empty())
		return 0;
	return regions[static_cast<size_t>(row) * cols + col];
}

uint16_t NavGrid::RegionOf(const Vec3& pos) const
{
	int row = 0, col = 0;
	return ToCell(pos, row, col) ? RegionOf(row, col) : 0;
}

bool NavGrid::Connected(const Vec3& a, const Vec3& b) const
{
	const uint16_t region = RegionOf(a);
	return region != 0 && region == RegionOf(b);
}

} // namespace Content
