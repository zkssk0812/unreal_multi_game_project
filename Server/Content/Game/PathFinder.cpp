#include "ServerCore/CorePch.h"
#include "Content/Game/PathFinder.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Content {

namespace {

constexpr float Diagonal = 1.41421356f;	// 대각선 한 칸의 비용 (직선 한 칸 = 1)

// 스레드마다 하나씩 있는 A* 작업 공간
struct Scratch
{
	std::vector<float> g;				// 시작부터 이 칸까지의 비용
	std::vector<int32_t> parent;		// 어느 칸에서 왔나 (경로 되짚기)
	std::vector<uint32_t> seen;			// seen[i] == generation 이면 이번 탐색에서 g·parent가 유효
	std::vector<uint32_t> closed;		// closed[i] == generation 이면 이미 확정된 칸
	std::vector<std::pair<float, int32_t>> open;	// (f = g + h, 칸 번호) 최소 힙
	std::vector<int32_t> cells;			// 되짚은 칸 목록 (재사용)
	uint32_t generation = 0;

	void Begin(size_t cellCount)
	{
		if (g.size() != cellCount)
		{
			g.assign(cellCount, 0.0f);
			parent.assign(cellCount, -1);
			seen.assign(cellCount, 0);
			closed.assign(cellCount, 0);
			generation = 0;
		}
		if (++generation == 0)
		{
			// 40억 번에 한 번: 세대 번호가 한 바퀴 돌면 진짜로 지운다
			std::fill(seen.begin(), seen.end(), 0u);
			std::fill(closed.begin(), closed.end(), 0u);
			generation = 1;
		}
		open.clear();
	}
};

thread_local Scratch t_scratch;

bool Walkable(const NavGrid& nav, int row, int col) { return nav.At(row, col) == '.'; }

// 막힌 칸이면 반경 3칸 안에서 가장 가까운 뚫린 칸으로
bool SnapToWalkable(const NavGrid& nav, const Vec3& pos, int& row, int& col)
{
	if (!nav.ToCell(pos, row, col))
	{
		// 격자 밖이면 가장자리로 끌어온다
		row = std::clamp(row, 0, nav.rows - 1);
		col = std::clamp(col, 0, nav.cols - 1);
	}
	if (Walkable(nav, row, col))
		return true;
	for (int radius = 1; radius <= 3; ++radius)
	{
		int bestRow = -1, bestCol = -1, bestDist = 1 << 30;
		for (int dr = -radius; dr <= radius; ++dr)
			for (int dc = -radius; dc <= radius; ++dc)
			{
				if (Walkable(nav, row + dr, col + dc) && dr * dr + dc * dc < bestDist)
				{
					bestDist = dr * dr + dc * dc;
					bestRow = row + dr;
					bestCol = col + dc;
				}
			}
		if (bestRow >= 0)
		{
			row = bestRow;
			col = bestCol;
			return true;
		}
	}
	return false;
}

// 남은 거리 추정(옥타일 거리): 대각선으로 갈 수 있는 만큼 가고 나머지는 직선. 실제 비용보다 크지 않아 최단 경로가 보장된다
float Heuristic(int r0, int c0, int r1, int c1)
{
	const int dr = std::abs(r0 - r1), dc = std::abs(c0 - c1);
	return static_cast<float>(std::max(dr, dc) - std::min(dr, dc)) + Diagonal * static_cast<float>(std::min(dr, dc));
}

} // namespace

bool LineWalkable(const NavGrid& nav, const Vec3& a, const Vec3& b)
{
	// 칸 크기의 1/4 간격으로 점을 찍어 그 칸이 모두 '.'인지 본다
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float length = std::sqrt(dx * dx + dy * dy);
	const int steps = std::max(1, static_cast<int>(length / (nav.cellSize * 0.25f)));
	for (int i = 0; i <= steps; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(steps);
		if (!nav.IsWalkable({ a.x + dx * t, a.y + dy * t, 0.0f }))
			return false;
	}
	return true;
}

bool FindPath(const NavGrid& nav, const Vec3& from, const Vec3& to, std::vector<Vec3>& out, PathStats* stats)
{
	const auto begin = std::chrono::steady_clock::now();
	out.clear();
	uint32_t expanded = 0;

	auto finish = [&](bool found) {
		if (stats)
		{
			stats->expanded = expanded;
			stats->micros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - begin).count());
		}
		return found;
	};

	int sr = 0, sc = 0, gr = 0, gc = 0;
	if (nav.rows <= 0 || nav.cols <= 0 || !SnapToWalkable(nav, from, sr, sc) || !SnapToWalkable(nav, to, gr, gc))
		return finish(false);

	// 서로 다른 구역(걸어서 안 닿는 곳)이면 탐색하지 않고 바로 실패 (맵 전체를 뒤지는 비용을 없앤다)
	if (!nav.regions.empty() && nav.RegionOf(sr, sc) != nav.RegionOf(gr, gc))
		return finish(false);

	Scratch& s = t_scratch;
	s.Begin(static_cast<size_t>(nav.rows) * static_cast<size_t>(nav.cols));
	const uint32_t gen = s.generation;
	auto index = [&](int r, int c) { return r * nav.cols + c; };
	auto heapLess = [](const std::pair<float, int32_t>& a, const std::pair<float, int32_t>& b) { return a.first > b.first; };

	const int start = index(sr, sc), goal = index(gr, gc);
	s.g[start] = 0.0f;
	s.parent[start] = -1;
	s.seen[start] = gen;
	s.open.push_back({ Heuristic(sr, sc, gr, gc), start });

	static constexpr int DR[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
	static constexpr int DC[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };

	bool found = false;
	while (!s.open.empty())
	{
		std::pop_heap(s.open.begin(), s.open.end(), heapLess);
		const int cur = s.open.back().second;
		s.open.pop_back();
		if (s.closed[cur] == gen)
			continue;		// 같은 칸이 더 싼 비용으로 이미 확정됨 (힙에서 지우지 않고 꺼낼 때 건너뛴다)
		s.closed[cur] = gen;
		++expanded;
		if (cur == goal)
		{
			found = true;
			break;
		}

		const int r = cur / nav.cols, c = cur % nav.cols;
		for (int d = 0; d < 8; ++d)
		{
			const int nr = r + DR[d], nc = c + DC[d];
			if (!Walkable(nav, nr, nc))
				continue;
			const bool diagonal = d >= 4;
			if (diagonal && (!Walkable(nav, r + DR[d], c) || !Walkable(nav, r, c + DC[d])))
				continue;		// 모서리 끼기 금지
			const int next = index(nr, nc);
			if (s.closed[next] == gen)
				continue;
			const float cost = s.g[cur] + (diagonal ? Diagonal : 1.0f);
			if (s.seen[next] == gen && cost >= s.g[next])
				continue;
			s.seen[next] = gen;
			s.g[next] = cost;
			s.parent[next] = cur;
			s.open.push_back({ cost + Heuristic(nr, nc, gr, gc), next });
			std::push_heap(s.open.begin(), s.open.end(), heapLess);
		}
	}
	if (!found)
		return finish(false);

	// 목적지에서 부모를 따라 거꾸로 → 뒤집기
	s.cells.clear();
	for (int cur = goal; cur != -1; cur = s.parent[cur])
		s.cells.push_back(cur);
	std::reverse(s.cells.begin(), s.cells.end());

	// 경로 다듬기: 지금 점에서 직선으로 갈 수 있는 가장 먼 점까지 건너뛴다
	auto worldOf = [&](int cell) { return nav.ToWorld(cell / nav.cols, cell % nav.cols); };
	Vec3 anchor = from;
	size_t i = 1;		// cells[0]은 시작 칸
	while (i < s.cells.size())
	{
		size_t farthest = i;
		while (farthest + 1 < s.cells.size() && LineWalkable(nav, anchor, worldOf(s.cells[farthest + 1])))
			++farthest;
		anchor = worldOf(s.cells[farthest]);
		out.push_back(anchor);
		i = farthest + 1;
	}
	// 마지막 점은 칸 가운데 대신 실제 목적지로 (목적지가 뚫린 칸이고 직선으로 닿으면)
	if (!out.empty() && nav.IsWalkable(to) && LineWalkable(nav, out.size() >= 2 ? out[out.size() - 2] : from, to))
		out.back() = { to.x, to.y, 0.0f };
	if (out.empty())
		out.push_back(nav.IsWalkable(to) ? Vec3{ to.x, to.y, 0.0f } : worldOf(goal));	// 같은 칸 안
	for (Vec3& p : out)
		p.z = from.z;		// 높이는 출발점 그대로 (1층 맵)
	return finish(true);
}

} // namespace Content
