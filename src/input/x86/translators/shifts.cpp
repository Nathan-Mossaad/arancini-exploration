#include "arancini/ir/value-type.h"
#include <arancini/input/x86/translators/translators.h>
#include <arancini/ir/ir-builder.h>
#include <arancini/ir/node.h>

using namespace arancini::ir;
using namespace arancini::input::x86::translators;

void shifts_translator::do_translate() {
    auto src = read_operand(0);
    auto amt = read_operand(1);

    auto inst = xed_decoded_inst_get_iclass(xed_inst());
    switch (inst) {
    case XED_ICLASS_SAR:
    case XED_ICLASS_SHR:
    case XED_ICLASS_SHL: {
        if (src->val().type().width() == 64) {
            amt = builder().insert_and(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 0x3F)->val());
        } else {
            amt = builder().insert_and(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 0x1F)->val());
        }
        if (is_immediate_operand(1)) { // shift amount is an immediate
            auto rot_val = ((constant_node *)amt)->const_val_i();

            if (rot_val != 0) {
                value_node *shift;
                if (inst == XED_ICLASS_SAR) {
                    shift = builder().insert_asr(src->val(), amt->val());
                } else if (inst == XED_ICLASS_SHR) {
                    shift = builder().insert_lsr(src->val(), amt->val());
                } else {
                    shift = builder().insert_lsl(src->val(), amt->val());
                }
                write_operand(0, shift->val());

                // CF flag receives the last shifted bit
                value_node *last_shift_bit;
                if (inst == XED_ICLASS_SHL) {
                    last_shift_bit = builder().insert_bit_extract(
                        src->val(), src->val().type().width() - rot_val, 1);
                } else { // SHR/SAR
                    last_shift_bit = builder().insert_bit_extract(
                        src->val(), rot_val - 1, 1);
                }
                write_reg(reg_offsets::CF, last_shift_bit->val());

                // OF is set if shifting by 1 only
                if (rot_val == 1) {
                    if (inst == XED_ICLASS_SHL) {
                        // for left shifts, OF is set to 0 iff most significant
                        // bit of the result equals CF
                        auto msb = builder().insert_bit_extract(
                            shift->val(), shift->val().type().width() - 1, 1);
                        auto of = builder().insert_xor(last_shift_bit->val(),
                                                       msb->val());
                        write_reg(reg_offsets::OF, of->val());
                    } else if (inst == XED_ICLASS_SAR) {
                        // for SAR, OF is cleared
                        write_reg(reg_offsets::OF,
                                  builder().insert_constant_u1(0)->val());
                    } else {
                        // for SHR, OF is set to most significant bit of
                        // original operand
                        auto msb = builder().insert_bit_extract(
                            src->val(), src->val().type().width() - 1, 1);
                        write_reg(reg_offsets::OF, msb->val());
                    }
                }
                write_flags(shift, flag_op::update, flag_op::ignore,
                            flag_op::ignore, flag_op::update, flag_op::update,
                            flag_op::ignore);
            }
        } else { // shift amount is a register
            auto zero_shift = builder().insert_cmpeq(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 0)->val());
            cond_br_node *zero_br = (cond_br_node *)builder().insert_cond_br(
                zero_shift->val(), nullptr);

            // shift amount is not 0
            value_node *shift;
            if (inst == XED_ICLASS_SAR) {
                shift = builder().insert_asr(src->val(), amt->val());
            } else if (inst == XED_ICLASS_SHR) {
                shift = builder().insert_lsr(src->val(), amt->val());
            } else {
                shift = builder().insert_lsl(src->val(), amt->val());
            }
            write_operand(0, shift->val());

            write_flags(shift, flag_op::update, flag_op::ignore,
                        flag_op::ignore, flag_op::update, flag_op::update,
                        flag_op::ignore);

            // CF flag receives the last shifted bit
            value_node *last_shift_bit;
            auto amt_min_1 = builder().insert_sub(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 1)->val());
            if (inst == XED_ICLASS_SHL) {
                auto tmp_shift =
                    builder().insert_lsl(src->val(), amt_min_1->val());
                last_shift_bit = builder().insert_bit_extract(
                    tmp_shift->val(), src->val().type().width() - 1, 1);
            } else { // SHR/SAR
                auto tmp_shift =
                    builder().insert_lsr(src->val(), amt_min_1->val());
                last_shift_bit =
                    builder().insert_bit_extract(tmp_shift->val(), 0, 1);
            }
            write_reg(reg_offsets::CF, last_shift_bit->val());

            // if shifting by 1, OF needs to be set as with immediate shift
            auto one_shift = builder().insert_cmpne(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 1)->val());
            cond_br_node *one_br = (cond_br_node *)builder().insert_cond_br(
                one_shift->val(), nullptr);

            if (inst == XED_ICLASS_SHL) {
                // for left shifts, OF is set to 0 iff most significant bit of
                // the result equals CF
                auto msb = builder().insert_bit_extract(
                    shift->val(), shift->val().type().width() - 1, 1);
                auto of =
                    builder().insert_xor(last_shift_bit->val(), msb->val());
                write_reg(reg_offsets::OF, of->val());
            } else if (inst == XED_ICLASS_SAR) {
                // for SAR, OF is cleared
                write_reg(reg_offsets::OF,
                          builder().insert_constant_u1(0)->val());
            } else {
                // for SHR, OF is set to most significant bit of original
                // operand
                auto msb = builder().insert_bit_extract(
                    src->val(), src->val().type().width() - 1, 1);
                write_reg(reg_offsets::OF, msb->val());
            }

            auto end_label = builder().insert_label("end");
            auto end_label1 = builder().insert_label("end");
            zero_br->add_br_target(end_label);
            one_br->add_br_target(end_label1);
        }
        break;
    }
    case XED_ICLASS_SHRD:
    case XED_ICLASS_SHLD: {
        auto in = amt;
        amt = read_operand(2);

        if (src->val().type().width() == 64) {
            amt = builder().insert_and(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 0x3F)->val());
        } else {
            amt = builder().insert_and(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 0x1F)->val());
        }
        if (is_immediate_operand(1)) { // shift amount is an immediate
            auto shift_val = ((constant_node *)amt)->const_val_i();

            if (shift_val != 0) {
                value_node *shift;
                if (inst == XED_ICLASS_SHRD) {
                    shift = builder().insert_lsr(src->val(), amt->val());
                    shift = builder().insert_bit_insert(
                        shift->val(),
                        builder()
                            .insert_bit_extract(
                                in->val(), in->val().type().width() - shift_val,
                                shift_val)
                            ->val(),
                        in->val().type().width() - shift_val, shift_val);
                } else {
                    shift = builder().insert_lsl(src->val(), amt->val());
                    shift = builder().insert_bit_insert(
                        shift->val(),
                        builder()
                            .insert_bit_extract(in->val(), 0, shift_val)
                            ->val(),
                        0, shift_val);
                }

                write_operand(0, shift->val());

                // CF flag receives the last shifted bit
                value_node *last_shift_bit;
                if (inst == XED_ICLASS_SHRD) {
                    last_shift_bit = builder().insert_bit_extract(
                        src->val(), shift_val - 1, 1);
                } else {
                    last_shift_bit = builder().insert_bit_extract(
                        src->val(), src->val().type().width() - shift_val, 1);
                }

                write_reg(reg_offsets::CF, last_shift_bit->val());

                // OF is set if shifting by 1 only
                if (shift_val == 1) {
                    value_node *of;
                    if (inst == XED_ICLASS_SHRD) {
                        auto msb = builder().insert_bit_extract(
                            src->val(), src->val().type().width() - 1, 1);
                        auto new_msb = builder().insert_bit_extract(
                            shift->val(), shift->val().type().width() - 1, 1);
                        of = builder().insert_xor(new_msb->val(), msb->val());
                    } else {
                        auto msb = builder().insert_bit_extract(
                            shift->val(), shift->val().type().width() - 1, 1);
                        of = builder().insert_xor(last_shift_bit->val(),
                                                  msb->val());
                    }
                    write_reg(reg_offsets::OF, of->val());
                }
                write_flags(shift, flag_op::update, flag_op::ignore,
                            flag_op::ignore, flag_op::update, flag_op::update,
                            flag_op::ignore);
            }
        } else { // shift amount is cl (8bit)
            auto zero_shift = builder().insert_cmpeq(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 0)->val());
            cond_br_node *zero_br = (cond_br_node *)builder().insert_cond_br(
                zero_shift->val(), nullptr);

            // shift amount is not 0
            value_node *shift;
            auto to_clear = builder().insert_sub(
                builder()
                    .insert_constant_i(in->val().type(),
                                       in->val().type().width())
                    ->val(),
                amt->val());
            if (inst == XED_ICLASS_SHRD) {
                shift = builder().insert_lsr(src->val(), amt->val());

                // clear lower <width-amt> bits in in
                in = builder().insert_lsr(in->val(), to_clear->val());
                in = builder().insert_lsl(in->val(), to_clear->val());

            } else {
                shift = builder().insert_lsl(src->val(), amt->val());

                // clear upper <width-amt> bits in in
                in = builder().insert_lsl(in->val(), to_clear->val());
                in = builder().insert_lsr(in->val(), to_clear->val());
            }
            shift = builder().insert_or(shift->val(), in->val());
            write_operand(0, shift->val());

            write_flags(shift, flag_op::update, flag_op::ignore,
                        flag_op::ignore, flag_op::update, flag_op::update,
                        flag_op::ignore);

            // CF flag receives the last shifted bit
            value_node *last_shift_bit;
            auto amt_min_1 = builder().insert_sub(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 1)->val());
            if (inst == XED_ICLASS_SHRD) {
                auto tmp_shift =
                    builder().insert_lsr(src->val(), amt_min_1->val());
                last_shift_bit =
                    builder().insert_bit_extract(tmp_shift->val(), 0, 1);
            } else {
                auto tmp_shift =
                    builder().insert_lsl(src->val(), amt_min_1->val());
                last_shift_bit = builder().insert_bit_extract(
                    tmp_shift->val(), src->val().type().width() - 1, 1);
            }
            write_reg(reg_offsets::CF, last_shift_bit->val());

            // if shifting by 1, OF needs to be set as with immediate shift
            auto one_shift = builder().insert_cmpne(
                amt->val(),
                builder().insert_constant_i(amt->val().type(), 1)->val());
            cond_br_node *one_br = (cond_br_node *)builder().insert_cond_br(
                one_shift->val(), nullptr);

            value_node *of;
            auto msb = builder().insert_bit_extract(
                shift->val(), shift->val().type().width() - 1, 1);

            if (inst == XED_ICLASS_SHRD) {
                auto old_msb = builder().insert_bit_extract(
                    src->val(), shift->val().type().width() - 1, 1);
                of = builder().insert_xor(old_msb->val(), msb->val());
            } else {
                of = builder().insert_xor(last_shift_bit->val(), msb->val());
            }
            write_reg(reg_offsets::OF, of->val());

            auto end_label = builder().insert_label("end");
            auto end_label1 = builder().insert_label("end");
            zero_br->add_br_target(end_label);
            one_br->add_br_target(end_label1);
        }
        break;
    }

    case XED_ICLASS_PSRLW:
    case XED_ICLASS_PSRLD:
    case XED_ICLASS_PSRLQ: {
        auto nr_splits = 0;
        auto typ = (inst == XED_ICLASS_PSRLW)   ? value_type::u16()
                   : (inst == XED_ICLASS_PSRLD) ? value_type::u32()
                                                : value_type::u64();

        if (src->val().type().width() == 64) {
            nr_splits = (inst == XED_ICLASS_PSRLW)   ? 4
                        : (inst == XED_ICLASS_PSRLD) ? 2
                                                     : 1;
        } else { // 128-bit xmm register
            nr_splits = (inst == XED_ICLASS_PSRLW)   ? 8
                        : (inst == XED_ICLASS_PSRLD) ? 4
                                                     : 2;
        }

        auto src_vec = builder().insert_bitcast(
            value_type::vector(typ, nr_splits), src->val());

        for (int i = 0; i < nr_splits; i++) {
            auto v = builder().insert_vector_extract(src_vec->val(), i);
            // TODO: if amt > val width, zero out
            auto shift = builder().insert_lsr(v->val(), amt->val());
            src_vec =
                builder().insert_vector_insert(src_vec->val(), i, shift->val());
        }

        write_operand(0, src_vec->val());
        break;
    }

    case XED_ICLASS_PSLLDQ: {
        auto amt_val = ((constant_node *)amt)->const_val_i();

        if (amt_val > 15) {
            write_operand(0, builder().insert_constant_u128(0)->val());
        } else {
            write_operand(
                0, builder()
                       .insert_lsl(
                           src->val(),
                           builder().insert_constant_u32(amt_val * 8)->val())
                       ->val());
        }
        break;
    }

    case XED_ICLASS_PSRLDQ: {
        auto amt_val = ((constant_node *)amt)->const_val_i();

        if (amt_val > 15) {
            write_operand(0, builder().insert_constant_u128(0)->val());
        } else {
            write_operand(
                0, builder()
                       .insert_lsr(
                           src->val(),
                           builder().insert_constant_u32(amt_val * 8)->val())
                       ->val());
        }
        break;
    }

    case XED_ICLASS_ROR: {
        // xed encoding: ror reg, imm
        auto value = read_operand(0);
        constant_node *rot = (constant_node *)read_operand(
            1); // TODO might need to mask this to 5 or 6 bits
        auto rot_val = rot->const_val_i();

        if (rot_val != 0) {
            auto low_bits =
                builder().insert_bit_extract(value->val(), 0, rot_val);
            auto shift = builder().insert_lsr(value->val(), rot->val());
            auto res = builder().insert_bit_insert(
                shift->val(), low_bits->val(),
                value->val().type().width() - rot_val, rot_val);

            write_operand(0, res->val());

            // CF flag receives a copy of the last bit that was shifted from one
            // end to the other
            auto top_bit0 = builder().insert_bit_extract(
                res->val(), res->val().type().width() - 1, 1);
            write_reg(reg_offsets::CF, top_bit0->val());

            if (rot_val == 1) {
                // OF is set to the XOR of 2 most significant bits iff rot == 1
                auto top_bit1 = builder().insert_bit_extract(
                    res->val(), res->val().type().width() - 2, 1);
                write_reg(reg_offsets::OF,
                          builder()
                              .insert_xor(top_bit0->val(), top_bit1->val())
                              ->val());
            }
        }
        break;
    }

    case XED_ICLASS_ROL: {
        // xed encoding: rol reg, imm
        auto value = read_operand(0);
        constant_node *rot = (constant_node *)read_operand(
            1); // TODO might need to mask this to 5 or 6 bits
        auto rot_val = rot->const_val_i();

        if (rot_val != 0) {
            auto high_bits = builder().insert_bit_extract(
                value->val(), value->val().type().width() - rot_val, rot_val);
            auto shift = builder().insert_lsl(value->val(), rot->val());
            auto res = builder().insert_bit_insert(
                shift->val(), high_bits->val(), 0, rot_val);

            write_operand(0, res->val());

            // CF flag receives a copy of the last bit that was shifted from one
            // end to the other
            auto low_bit = builder().insert_bit_extract(res->val(), 0, 1);
            write_reg(reg_offsets::CF, low_bit->val());

            if (rot_val == 1) {
                // OF is set to the XOR of CF (after rotate) and the most
                // significant bits iff rot == 1
                auto top_bit = builder().insert_bit_extract(
                    res->val(), res->val().type().width() - 1, 1);
                write_reg(reg_offsets::OF,
                          builder()
                              .insert_xor(top_bit->val(), low_bit->val())
                              ->val());
            }
        }
        break;
    }
    case XED_ICLASS_PSLLD: {
        auto value = read_operand(0);
        auto by = read_operand(1);

        value = builder().insert_bitcast(
            value_type::vector(value_type::u32(), 4), value->val());
        for (unsigned i = 0; i < 4; i++) {
            auto dword = builder().insert_vector_extract(value->val(), i);
            auto res = builder().insert_lsl(dword->val(), by->val());
            value = builder().insert_vector_insert(value->val(), i, res->val());
        }
        break;
    }
    default:
        throw std::runtime_error("unsupported shift instruction");
    }
}
