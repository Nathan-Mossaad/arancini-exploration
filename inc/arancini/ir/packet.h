#pragma once

#include <string>
#include <vector>

#include <arancini/ir/node.h>
#include <arancini/ir/visitor.h>

extern "C" {
#include <xed/xed-interface.h>
}

namespace arancini::ir {
class packet {
public:
	packet(off_t address, const std::string &disassembly = "")
		: address_(address)
		, disasm_(disassembly)
	{
	}

	local_var *alloc_local(const value_type &type)
	{
		auto lcl = new local_var(type);
		locals_.push_back(lcl);

		return lcl;
	}

	void append_action(const std::shared_ptr<action_node>& node) { actions_.push_back(node); }

	const std::vector<std::shared_ptr<action_node>> &actions() const { return actions_; }
	std::vector<std::shared_ptr<action_node>> &actions()  { return actions_; }

	void set_actions(std::vector<std::shared_ptr<action_node>> actions) { actions_ = std::move(actions);}

	void accept(visitor &v) { v.visit_packet(*this); }

	off_t address() const { return address_; }

	br_type updates_pc() const
	{
		br_type max = br_type::none;
		for (auto a : actions_) {
			if ((unsigned)a->updates_pc() > (unsigned)max) {
				max = a->updates_pc();
			}
		}

		return max;
	}

	const std::string &disassembly() const { return disasm_; }

private:
	off_t address_;
	std::string disasm_;
	std::vector<local_var *> locals_;
	std::vector<std::shared_ptr<action_node>> actions_;
};
} // namespace arancini::ir
