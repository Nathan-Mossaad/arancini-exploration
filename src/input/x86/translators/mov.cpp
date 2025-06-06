#include <arancini/input/x86/translators/translators.h>
#include <arancini/ir/node.h>
#include <arancini/ir/ir-builder.h>

using namespace arancini::ir;
using namespace arancini::input::x86::translators;

void mov_translator::do_translate()
{
	switch (xed_decoded_inst_get_iclass(xed_inst())) {
	case XED_ICLASS_LEA:
		write_operand(0, compute_address(0)->val());
		break;

	case XED_ICLASS_MOV: {
		const xed_inst_t *insn = xed_decoded_inst_inst(xed_inst());
		auto operand = xed_inst_operand(insn, 0);
		auto opname = xed_operand_name(operand);

		auto tt = opname == XED_OPERAND_MEM0 ? type_of_operand(1) : type_of_operand(0);
		auto op1 = auto_cast(tt, read_operand(1));
		//TODO: temporary hack for MOVQ with immediate
		if (is_immediate_operand(1) && get_operand_width(1) < get_operand_width(0)) {
			op1 = builder().insert_zx(value_type::u64(), op1->val());
		}
		write_operand(0, op1->val());
		break;
	}

	case XED_ICLASS_CWD: {
		auto ax = read_reg(value_type::s16(), xedreg_to_offset(XED_REG_AX));
		auto sx = builder().insert_sx(value_type::s32(), ax->val());
		auto hi = builder().insert_bit_extract(sx->val(), 16, 16);

		write_reg(xedreg_to_offset(XED_REG_DX), hi->val());
		break;
	}

	case XED_ICLASS_CQO: {
		auto rax = read_reg(value_type::s64(), reg_offsets::RAX);
		auto sx = builder().insert_sx(value_type::s128(), rax->val());
		auto hi = builder().insert_bit_extract(sx->val(), 64, 64);

		write_reg(reg_offsets::RDX, hi->val());
		break;
	}

	case XED_ICLASS_CDQ: {
    // xed encoding: cdq edx eax
		auto eax = builder().insert_bitcast(value_type::s32(), read_operand(1)->val());
		auto sx = builder().insert_sx(value_type::s64(), eax->val());
		auto hi = builder().insert_bit_extract(sx->val(), 32, 32);

    write_operand(0, hi->val());
		break;
	}

	case XED_ICLASS_CDQE: {
    // xed encoding: cdqe rax eax
		/* Only for 64-bit, other sizes are with CBW/CWDE instructions */
		auto eax = builder().insert_bitcast(value_type::s32(), read_operand(1)->val());
		auto rax = builder().insert_sx(value_type::s64(), eax->val());
		write_operand(0, rax->val());
		break;
	}

	case XED_ICLASS_MOVQ: {
		// up to SSE2
		auto src = read_operand(1);

		if (src->val().type().width() == 128) {
			src = builder().insert_bit_extract(src->val(), 0, 64);
		}

		if (get_operand_width(0) == 128) {
			src = builder().insert_zx(value_type::u128(), src->val());
		}

		write_operand(0, src->val());
		break;
	}

	case XED_ICLASS_MOVSXD: {
		// TODO: INCORRECT FOR SOME SIZES
		auto input = read_operand(1);
		input = builder().insert_bitcast(value_type(value_type_class::signed_integer, input->val().type().width()), input->val());

		auto cast = builder().insert_sx(value_type::s64(), input->val());

		write_operand(0, cast->val());
		break;
	}

	case XED_ICLASS_MOVZX: {
		// TODO: Incorrect operand sizes
		auto input = read_operand(1);
		auto cast = builder().insert_zx(value_type::u64(), input->val());

		write_operand(0, cast->val());
		break;
	}

	case XED_ICLASS_MOVSX: {
		auto input = read_operand(1);
		auto dest = read_operand(0);
		auto cast = builder().insert_sx(dest->val().type(), input->val());

		write_operand(0, cast->val());
		break;
	}

	case XED_ICLASS_MOVD: {
		auto input = read_operand(1);
		auto dst_width = get_operand_width(0);
		auto src_width = input->val().type().width();

		if (dst_width == 32) { // movd r/m32, xmm
			input = builder().insert_bit_extract(input->val(), 0, 32);
		} else if (src_width == 32) { // movd xmm, r/m32
			input = builder().insert_zx(value_type::u128(), input->val());
		}
		write_operand(0, input->val());
		break;
	}

  case XED_ICLASS_MOVSD_XMM: {
    auto src = read_operand(1);
    auto dst_width = get_operand_width(0);
    auto src_width = src->val().type().width();

    if (dst_width == 64) { // movsd m64, xmm1
      src = builder().insert_bit_extract(src->val(), 0, 64);
    } else if (src_width == 64) { // movsd xmm1, m64
      src = builder().insert_zx(value_type::u128(), src->val());
    } else { // movsd xmm1, xmm2
      auto dst = read_operand(0);
      src = builder().insert_bit_extract(src->val(), 0, 64);
      dst = builder().insert_bit_insert(dst->val(), src->val(), 0, 64);
    }
    write_operand(0, src->val());
    break;
  }

  case XED_ICLASS_MOVSB:
  case XED_ICLASS_MOVSD:
  case XED_ICLASS_MOVSW:
  case XED_ICLASS_MOVSQ: {
	int addr_align;

	switch (xed_decoded_inst_get_iclass(xed_inst())) {
	case XED_ICLASS_MOVSQ:
	  addr_align = 8;
	  break;
	case XED_ICLASS_MOVSD:
	  addr_align = 4;
	  break;
	case XED_ICLASS_MOVSW:
	  addr_align = 2;
	  break;
	case XED_ICLASS_MOVSB:
	  addr_align = 1;
	  break;
	default:
	  throw std::runtime_error("unsupported rep movs size");
	}
	auto cst_align = builder().insert_constant_u64(addr_align);

	// move byte/word/dword/qword from [rsi] to [rdi], then inc/dec rsi and rdi depending on DF flag
	auto rsi = read_reg(value_type::u64(), reg_offsets::RSI);
	auto rdi = read_reg(value_type::u64(), reg_offsets::RDI);
	const value_type &vt = addr_align == 8 ? value_type::u64() : addr_align == 4 ? value_type::u32() : addr_align == 2 ? value_type::u16() : value_type::u8();
	auto rsi_val = builder().insert_read_mem(vt, rsi->val());

	builder().insert_write_mem(rdi->val(), rsi_val->val());

	// update rdi and rsi according to DF register (0: inc, 1: dec)
	auto df = read_reg(value_type::u1(), reg_offsets::DF);
	auto df_test = builder().insert_cmpne(df->val(), builder().insert_constant_i(value_type::u1(), 0)->val());
	cond_br_node *br_df = (cond_br_node *)builder().insert_cond_br(df_test->val(), nullptr);

	write_reg(reg_offsets::RDI, builder().insert_add(rdi->val(), cst_align->val())->val());
	write_reg(reg_offsets::RSI, builder().insert_add(rsi->val(), cst_align->val())->val());
	br_node *br_then = (br_node *)builder().insert_br(nullptr);

	auto else_label = builder().insert_label("else");
	br_df->add_br_target(else_label);

	write_reg(reg_offsets::RDI, builder().insert_sub(rdi->val(), cst_align->val())->val());
	write_reg(reg_offsets::RSI, builder().insert_sub(rsi->val(), cst_align->val())->val());

	auto endif_label = builder().insert_label("endif");
	br_then->add_br_target(endif_label);

	break;
  }

	case XED_ICLASS_MOVUPS:
	case XED_ICLASS_MOVAPS:
	case XED_ICLASS_MOVDQA:
  case XED_ICLASS_MOVDQU:
	case XED_ICLASS_MOVAPD: {
		// TODO: INCORRECT FOR SOME SIZES

		auto src = read_operand(1);
		// auto dst = read_operand(pkt, xed_inst, 0);
		// auto result = pkt->insert_or(dst->val(), )
		write_operand(0, src->val());
		break;
	}

  case XED_ICLASS_MOVLPS:
  case XED_ICLASS_MOVLPD: {
    auto src = read_operand(1);
    if (src->val().type().width() == 128) {
      src = builder().insert_bit_extract(src->val(), 0, 64);
    }

    if (get_operand_width(0) == 64) {
      write_operand(0, src->val());
    } else { // 128 bits destination
      auto dst = read_operand(0);
      dst = builder().insert_bit_insert(dst->val(), src->val(), 0, 64);
      write_operand(0, dst->val());
    }
    break;
  }
  case XED_ICLASS_MOVHPS:
  case XED_ICLASS_MOVHPD: {
    auto src = read_operand(1);
    if (src->val().type().width() == 128) {
      src = builder().insert_bit_extract(src->val(), 0, 64);
    }

    if (get_operand_width(0) == 64) {
      write_operand(0, src->val());
    } else { // 128 bits destination
      auto dst = read_operand(0);
      dst = builder().insert_bit_insert(dst->val(), src->val(), 64, 64);
      write_operand(0, dst->val());
    }
    break;
  }

	case XED_ICLASS_MOVSS: {
		// movss xmm1, xmm2: dst[31..0] = src[31..0], dst[MAXVAL..32] are unmodified
		// movss xmm1, m32: dst[31..0] = src[31..0], dst[127..32] = 0, dst[MAXVAL..128] are unmodified
		auto dst = read_operand(0);
		auto src = read_operand(1);
		value_node *val, *rslt;

		if (src->val().type().element_width() == 128) {
			src = builder().insert_bitcast(value_type::vector(value_type::f32(), 4), src->val());
			val = builder().insert_vector_extract(src->val(), 0);
		} else {
			src = builder().insert_bitcast(value_type::f32(), src->val());
			val = src;
		}

		if (dst->val().type().element_width() == 128) {
			dst = builder().insert_bitcast(value_type::vector(value_type::f32(), 4), dst->val());
			if (src->val().type().element_width() == 32) {
				// dst[127..32] = 0 iff src is mem and dst is xmm reg
				auto zero = builder().insert_constant_f(value_type::f32(), 0.0f);
				dst = builder().insert_vector_insert(dst->val(), 1, zero->val());
				dst = builder().insert_vector_insert(dst->val(), 2, zero->val());
				dst = builder().insert_vector_insert(dst->val(), 3, zero->val());
			}

			rslt = builder().insert_vector_insert(dst->val(), 0, val->val());
		} else {
			rslt = val;
		}
		write_operand(0, rslt->val());
		break;
	}

	default:
		throw std::runtime_error("unsupported mov operation");
	}
}
