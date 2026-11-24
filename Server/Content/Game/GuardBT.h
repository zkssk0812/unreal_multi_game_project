#pragma once

// 작은 행동 트리(Behavior Tree) 틀 (docs/경비원AI_행동트리.md).
//
// - 노드는 Tick마다 성공·실패·실행 중 하나를 돌려준다.
//   선택(Selector): 자식을 앞에서부터, 처음으로 실패가 아닌 결과를 돌려줌 ("우선순위 목록")
//   순서(Sequence): 자식을 차례로, 실패나 실행 중이 나오면 거기서 멈춤 ("조건 → 행동")
//   조건(Condition): 참이면 성공, 거짓이면 실패
//   행동(Action): 함수가 돌려주는 결과 그대로
// - 기억하는 노드가 없다(reactive): 매 틱 뿌리부터 다시 평가하고, 하던 일은 블랙보드(GuardBrain)에 남긴다.
//   그래서 우선순위가 높은 가지의 조건이 참이 되면 그 틱에 바로 그쪽으로 넘어간다.
// - 트리는 한 번 만들어 모든 경비원·모든 방이 같이 쓴다(읽기 전용). 경비원마다 다른 것은 Ctx에만 있다.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Content::Bt {

enum class Status : uint8_t { Success, Failure, Running };

template <class Ctx>
class Node
{
public:
	enum class Kind : uint8_t { Selector, Sequence, Condition, Action };

	Node(Kind kind, std::string name) : _kind(kind), _name(std::move(name)) {}

	Status Tick(Ctx& ctx) const
	{
		switch (_kind)
		{
		case Kind::Selector:
			for (const auto& child : _children)
				if (const Status s = child->Tick(ctx); s != Status::Failure)
					return s;
			return Status::Failure;
		case Kind::Sequence:
			for (const auto& child : _children)
				if (const Status s = child->Tick(ctx); s != Status::Success)
					return s;
			return Status::Success;
		case Kind::Condition:
			return _test(ctx) ? Status::Success : Status::Failure;
		case Kind::Action:
			return _run(ctx);
		}
		return Status::Failure;
	}

	Kind GetKind() const { return _kind; }
	const std::string& Name() const { return _name; }
	const std::vector<std::unique_ptr<Node>>& Children() const { return _children; }

	// 만들기 도우미: N::Select("이름", 자식1, 자식2, ...)
	template <class... Kids>
	static std::unique_ptr<Node> Select(std::string name, Kids&&... kids)
	{
		auto n = std::make_unique<Node>(Kind::Selector, std::move(name));
		(n->_children.push_back(std::forward<Kids>(kids)), ...);
		return n;
	}
	template <class... Kids>
	static std::unique_ptr<Node> Sequence(std::string name, Kids&&... kids)
	{
		auto n = std::make_unique<Node>(Kind::Sequence, std::move(name));
		(n->_children.push_back(std::forward<Kids>(kids)), ...);
		return n;
	}
	static std::unique_ptr<Node> If(std::string name, std::function<bool(Ctx&)> test)
	{
		auto n = std::make_unique<Node>(Kind::Condition, std::move(name));
		n->_test = std::move(test);
		return n;
	}
	static std::unique_ptr<Node> Do(std::string name, std::function<Status(Ctx&)> run)
	{
		auto n = std::make_unique<Node>(Kind::Action, std::move(name));
		n->_run = std::move(run);
		return n;
	}

private:
	Kind _kind;
	std::string _name;
	std::vector<std::unique_ptr<Node>> _children;
	std::function<bool(Ctx&)> _test;
	std::function<Status(Ctx&)> _run;
};

} // namespace Content::Bt
