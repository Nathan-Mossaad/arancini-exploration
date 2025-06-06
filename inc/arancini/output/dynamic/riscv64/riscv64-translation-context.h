#pragma once

#include <arancini/ir/node.h>
#include <arancini/output/dynamic/riscv64/encoder/riscv64-assembler.h>
#include <arancini/output/dynamic/riscv64/instruction-builder/builder.h>
#include <arancini/output/dynamic/riscv64/register.h>
#include <arancini/output/dynamic/translation-context.h>
#include <arancini/input/registers.h>
#include <arancini/runtime/exec/x86/x86-cpu-state.h>
#include <arancini/util/ordering.h>

#include <array>
#include <bitset>
#include <forward_list>
#include <memory>
#include <optional>
#include <stack>
#include <unordered_map>
#include <variant>

namespace arancini::output::dynamic::riscv64 {

using builder::AddressOperand;
using builder::InstructionBuilder;
using arancini::input::x86::reg_idx;
using arancini::input::x86::reg_offsets;

class riscv64_translation_context : public translation_context {
public:
	riscv64_translation_context(machine_code_writer &writer)
		: translation_context(writer)
		, assembler_(&writer, true, RV_GC)
	{
	}

	virtual void begin_block() override;
	virtual void begin_instruction(off_t address, const std::string &disasm) override;
	virtual void end_instruction() override;
	virtual void end_block() override;
	virtual void lower(const std::shared_ptr<ir::action_node> &n) override;

	virtual void chain(uint64_t chain_address, void *chain_target) override;

private:
	InstructionBuilder builder_;
	Assembler assembler_;

	off_t current_address_;
	std::vector<std::shared_ptr<action_node>> nodes_;

	intptr_t ret_val_;

	std::unordered_map<const ir::label_node *, std::pair<Label *, bool>> labels_;
	std::stack<decltype(builder_.next_register().encoding()), std::vector<decltype(builder_.next_register().encoding())>> idxs_;
	std::forward_list<decltype(builder_.next_register().encoding())> live_across_iteration_;

	std::unordered_map<const ir::port *, TypedRegister> treg_for_port_;
	std::forward_list<TypedRegister> temporaries;
	std::unordered_map<const ir::local_var *, std::reference_wrapper<TypedRegister>> locals_;
	std::bitset<16> reg_loaded_ {};
	std::bitset<16> reg_written_ {};
	std::bitset<8> flag_loaded_ {};
	std::bitset<8> flag_written_ {};

	std::pair<TypedRegister &, bool> allocate_register(
		const ir::port *p = nullptr, std::optional<RegisterOperand> reg1 = std::nullopt, std::optional<RegisterOperand> reg2 = std::nullopt);

	RegisterOperand get_or_assign_mapped_register(uint32_t idx);
	RegisterOperand get_or_load_mapped_register(uint32_t idx);
	void write_back_registers();

	std::optional<std::reference_wrapper<TypedRegister>> materialise(const ir::node *n);
	std::optional<int64_t> get_as_int(const node *n);

	TypedRegister &materialise_read_reg(const ir::read_reg_node &n);
	void materialise_write_reg(const ir::write_reg_node &n);
	TypedRegister &materialise_read_mem(const ir::read_mem_node &n);
	void materialise_write_mem(const ir::write_mem_node &n);
	TypedRegister &materialise_read_pc(const ir::read_pc_node &n);
	void materialise_write_pc(const ir::write_pc_node &n);
	void materialise_label(const ir::label_node &n);
	void materialise_br(const ir::br_node &n);
	void materialise_cond_br(const ir::cond_br_node &n);
	TypedRegister &materialise_constant(int64_t imm);
	TypedRegister &materialise_unary_arith(const ir::unary_arith_node &n);
	TypedRegister &materialise_binary_arith(const ir::binary_arith_node &n);
	TypedRegister &materialise_ternary_arith(const ir::ternary_arith_node &n);
	TypedRegister &materialise_bit_shift(const ir::bit_shift_node &n);
	TypedRegister &materialise_bit_extract(const ir::bit_extract_node &n);
	TypedRegister &materialise_bit_insert(const ir::bit_insert_node &n);
	TypedRegister &materialise_cast(const ir::cast_node &n);
	TypedRegister & materialise_binary_atomic(const ir::binary_atomic_node &n);
	TypedRegister &materialise_ternary_atomic(const ir::ternary_atomic_node &n);
	TypedRegister &materialise_csel(const ir::csel_node &n);
	void materialise_internal_call(const ir::internal_call_node &n);
	TypedRegister &materialise_vector_insert(const ir::vector_insert_node &node);
	TypedRegister &materialise_vector_extract(const ir::vector_extract_node &n);

	void add_marker(int payload);

	template <reg_idx... idx> std::tuple<to_type_t<reg_idx, idx, RegisterOperand>...> allocate_in_order(to_type_t<reg_idx, idx, const port *>... args);
	template <const size_t order[], typename Tuple, size_t... idxs> auto reorder(Tuple tuple, std::index_sequence<idxs...>);
	void bw_branch_vreg_helper(bool bw);
};
} // namespace arancini::output::dynamic::riscv64
