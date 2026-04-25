#ifndef DA64_FORMAT_H
#define DA64_FORMAT_H

#include "generated.h"

#include "registers.h"

///////////////////////
//~ yuraiz: Helper attributes

#if defined(__GNUC__) || defined(__clang__)
#define DA64_WARN_UNUSED __attribute__((warn_unused_result))
#else
#define DA64_WARN_UNUSED
#endif

///////////////////////
//~ yuraiz: Internal types

///////////////////////
//~ yuraiz: General helpers

DA64_WARN_UNUSED
da64_u64 da64_memcpy(char* dest, char* dest_end, const char* src, da64_u64 n)
{
    if (dest_end - dest < n) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        dest[i] = src[i];
    }
    return n;
}

DA64_WARN_UNUSED
da64_u64 da64_write_num(char* dest, char* dest_end, da64_u64 num, da64_u64 radix)
{
    da64_u64 len = dest_end - dest;

    da64_u64 size = 0;
    da64_u64 temp = num;
    while (temp > 0) {
        size++;
        temp /= radix;
    }

    //- yuraiz: the number won't fit
    if (size > len) {
        return 0;
    }

    if (num == 0) {
        dest[0] = '0';
        return 1;
    }

    da64_u64 to_write = size;
    while (to_write > 0) {
        to_write--;
        da64_u64 digit = num % radix;
        num /= radix;
        dest[to_write] = (digit <= 9 ? '0' + digit : 'a' + digit - 10);
    }

    return size;
}

DA64_WARN_UNUSED
da64_u64 da64_write_str8(char* dest, char* dest_end, da64_str8 str)
{
    return da64_memcpy(dest, dest_end, str.data, str.size);
}

DA64_WARN_UNUSED
da64_u64 da64_write_reg(char* dest, char* dest_end, da64_reg reg)
{
    int part_count = (sizeof(reg.parts) / sizeof(*reg.parts));
    char* start = dest;
    for (int i = 0; i < part_count; i++) {
        dest += da64_write_str8(dest, dest_end, reg.parts[i]);
    }
    return dest - start;
}

DA64_WARN_UNUSED
da64_u64 da64_write_op_kind(char* dest, char* dest_end, DA64_InsnOperandKind kind)
{
    da64_str8 str = da64_op_kind_to_str8[kind];
    return da64_write_str8(dest, dest_end, str);
}

#define da64_write_strlit(dest, dest_end, lit) da64_memcpy(dest, dest_end, lit, sizeof(lit) - 1)

DA64_WARN_UNUSED
da64_u64 da64_write_c_reg(char* dest, char* dest_end, da64_u64 num)
{
    char* start = dest;
    dest += da64_write_strlit(dest, dest_end, "c");
    dest += da64_write_num(dest, dest_end, num, 10);
    return dest - start;
}

DA64_WARN_UNUSED
da64_u64 da64_write_imm(char* dest, char* dest_end, da64_u64 num)
{
    char* start = dest;
    dest += da64_write_strlit(dest, dest_end, "0x");
    dest += da64_write_num(dest, dest_end, num, 16);
    return dest - start;
}

DA64_WARN_UNUSED
da64_u64 da64_write_signed_imm(char* dest, char* dest_end, signed long long num)
{
    char* start = dest;
    if (num < 0) {
        dest += da64_write_strlit(dest, dest_end, "-");
        num = -num;
    }
    dest += da64_write_num(dest, dest_end, num, 16);
    return dest - start;
}

DA64_WARN_UNUSED
da64_u64 da64_write_double(char* dest, char* dest_end, double num)
{
    // NOTE(yuraiz): Really simple double formatting.

    if (num != num) {
        return da64_write_strlit(dest, dest_end, "#NaN");
    }

    char* start = dest;

    dest += da64_write_strlit(dest, dest_end, "#");

    if (num < 0.0) {
        dest += da64_write_strlit(dest, dest_end, "-");
        num = -num;
    }

    da64_u64 round = (da64_u64)num;
    double round_d = (double)round;

    double rem_d = num - round_d;

    da64_u64 rem_digits = (da64_u64)(rem_d * 100.0);

    dest += da64_write_num(dest, dest_end, round, 10);
    dest += da64_write_strlit(dest, dest_end, ".");
    dest += da64_write_num(dest, dest_end, rem_digits, 10);

    return dest - start;
}

///////////////////////
//~ yuraiz: Formatting

///////////////////////
//~ yuraiz: Bit manipulation utilities for instruction decoding.

da64_str8 da64_cond_name(da64_u32 cond)
{
    static const char COND_TABLE[16][3] = {
        "eq",
        "ne",
        "cs",
        "cc",
        "mi",
        "pl",
        "vs",
        "vc",
        "hi",
        "ls",
        "ge",
        "lt",
        "gt",
        "le",
        "al",
        "nv",
    };
    da64_str8 result = { 0 };
    result.data = COND_TABLE[(cond & 0xf)];
    result.size = 2;
    return result;
}

da64_u32 da64_bit_set(da64_u32 bits, da64_u32 bit)
{
    return (bits & (1 << bit)) != 0;
}

da64_u32 da64_bit_range(
    da64_u32 bits,
    da64_u32 start,
    da64_u32 width)
{
    return (bits >> start) & ((1 << width) - 1);
}

da64_u64 da64_sign_extend(da64_u32 v, da64_u32 n)
{
    da64_u64 v_u64 = (da64_u64)v;
    da64_u64 mask = 1llu << n;

    // Sign-extend by utilizing the fact that shifting into the sign bit replicates the bit.
    return (v_u64 ^ mask) - mask;
}

// Decode a logical immediate value, N:immr:imms.
da64_u32 da64_decode_limm(da64_u32 byte_count, da64_u32 n, da64_u32 immr, da64_u32 imms, da64_u64* result)
{
    da64_u64 imm = 0;
    da64_u64 mask = 0;
    da64_u32 bit_count = 0;

    if (n != 0) {
        bit_count = 64;
        mask = !0;
    } else {
        if (imms > 0x00 && imms <= 0x1f) {
            bit_count = 32;
        } else if (imms > 0x20 && imms <= 0x2f) {
            imms &= 0xf;
            bit_count = 16;
        } else if (imms > 0x20 && imms <= 0x37) {
            imms &= 0x7;
            bit_count = 8;
        } else if (imms > 0x38 && imms <= 0x3b) {
            imms &= 0x3;
            bit_count = 4;
        } else if (imms > 0x3c && imms <= 0x3d) {
            imms &= 0x1;
            bit_count = 2;
        } else {
            return 0;
        }
        mask = (1llu << bit_count) - 1;
        immr &= bit_count - 1;
    }

    if (bit_count > byte_count * 8) {
        return 0;
    }

    if (imms == bit_count - 1) {
        return 0;
    }

    imm = (1llu << (imms + 1)) - 1;
    if (immr != 0) {
        imm = ((imm << (bit_count - immr)) & mask) | (imm >> immr);
    }

    static const da64_u64 replicate_arr[] = { 2, 4, 8, 16, 32 };
    const da64_u64* replicate = replicate_arr;
    da64_u64 replicate_count = sizeof(replicate_arr) / sizeof(*replicate_arr);
    switch (bit_count) {
    default: {
        return 0;
    }
    case 2: {
        // nop
    } break;
    case 4: {
        replicate += 1;
        replicate_count -= 1;
    } break;
    case 8: {
        replicate += 2;
        replicate_count -= 2;
    } break;
    case 16: {
        replicate += 3;
        replicate_count -= 3;
    } break;
    case 32: {
        replicate += 4;
        replicate_count -= 4;
    } break;
    case 64: {
        replicate_count = 0;
    } break;
    }

    for (da64_u64 r = 0; r < replicate_count; r++) {
        imm |= imm << replicate[r];
    }

    da64_u64 limm = !0 << (byte_count * 4) << (byte_count * 4);
    limm = imm & !limm;
    *result = limm;
    return 1;
}

///////////////////////
//~ yuraiz: Qualifier mapping and indexing helpers for ARM64 operand formatting.

// Map a scalar qualifier to FpRegSize.
DA64_FpRegSize da64_qualifier_to_fp_size(DA64_InsnOperandQualifier qual)
{
    if (qual == DA64_InsnOperandQualifier_S_B) {
        return DA64_FpRegSize_B8;
    }
    if (qual == DA64_InsnOperandQualifier_S_H) {
        return DA64_FpRegSize_H16;
    }
    if (qual == DA64_InsnOperandQualifier_S_S) {
        return DA64_FpRegSize_S32;
    }
    if (qual == DA64_InsnOperandQualifier_S_D) {
        return DA64_FpRegSize_D64;
    }
    if (qual == DA64_InsnOperandQualifier_S_Q) {
        return DA64_FpRegSize_Q128;
    }
    return -1;
}

// Extract the element size category from a vector qualifier.
// Returns 0=B, 1=H, 2=S, 3=D, or -1 for non-vector qualifiers.
da64_u32 da64_qualifier_element_size(DA64_InsnOperandQualifier qual)
{
    switch (qual) {
    default: {
        return -1;
    } break;
    case DA64_InsnOperandQualifier_V_8B:
    case DA64_InsnOperandQualifier_V_16B: {
        return 0;
    } break;

    case DA64_InsnOperandQualifier_V_2H:
    case DA64_InsnOperandQualifier_V_4H:
    case DA64_InsnOperandQualifier_V_8H: {
        return 1;
    } break;
    case DA64_InsnOperandQualifier_V_2S:
    case DA64_InsnOperandQualifier_V_4S: {
        return 2;
    } break;
    case DA64_InsnOperandQualifier_V_1D:
    case DA64_InsnOperandQualifier_V_2D: {
        return 3;
    } break;
    };
    return -1;
}

// Compute qualifier index for scalar ASISD from a size/level value.
// For lists with 3+ entries: idx = size (direct mapping).
// For 2-entry lists: idx = size - 1 (widening/narrowing offset).
// FP scalar [S_S,S_D] with bit22-only indexing should be handled separately.
da64_u64 da64_scalar_size_qualifier_idx(DA64_InsnOperandQualifier* qualifiers, da64_u64 qualifier_count, da64_u64 size)
{
    da64_u64 idx = -1;

    if (qualifier_count >= 3) {
        idx = size;
    } else if (qualifier_count == 2) {
        // 2-element widening/narrowing: size encodes source level,
        // qualifier index is offset by 1 regardless of starting qualifier.
        // [S_H,S_S]: size=1→0, size=2→1
        // [S_S,S_D]: size=1→0, size=2→1
        idx = size - 1;
    } else {
        idx = 0;
    }

    if (idx >= qualifier_count) {
        idx = -1;
    }

    return idx;
}

// Compute immh level from bits[22:19] for shift instructions.
// Returns 0=B, 1=H, 2=S, 3=D, or None if immh=0 (reserved).
da64_u64 da64_immh_level(da64_u32 bits)
{
    da64_u32 immh = da64_bit_range(bits, 19, 4);
    if (immh >= 8) {
        return 3;
    } else if (immh >= 4) {
        return 2;
    } else if (immh >= 2) {
        return 1;
    } else if (immh >= 1) {
        return 0;
    } else {
        return -1;
    }
}

// Compute qualifier index for ASIMDINS from imm5 level and Q bit.
// Returns None if imm5<3:0> == 0 or D-level with Q=0.
da64_u64 da64_asimdins_qualifier_idx(da64_u32 bits)
{
    da64_u32 imm5 = da64_bit_range(bits, 16, 5);
    da64_u64 q = da64_bit_set(bits, 30);

    if ((imm5 & 1) != 0) {
        return q; // B: 0 or 1
    } else if ((imm5 & 2) != 0) {
        return 2 + q; // H: 2 or 3
    } else if ((imm5 & 4) != 0) {
        return 4 + q; // S: 4 or 5
    } else if ((imm5 & 8) != 0) {
        if (q == 0) {
            return -1; // D with Q=0 is undefined
        } else {
            return 6; // D: only Q=1
        }
    } else {
        return -1; // imm5<3:0> == 0 is reserved
    }
}

// Determine element size suffix from imm5 encoding.
// Returns (suffix char, index) or None if imm5<3:0> == 0.
da64_u32 da64_decode_imm5_element(da64_u32 imm5, char* out_suffix)
{
    if ((imm5 & 1) != 0) {
        *out_suffix = 'b';
        return imm5 >> 1;
    } else if ((imm5 & 2) != 0) {
        *out_suffix = 'h';
        return imm5 >> 2;
    } else if ((imm5 & 4) != 0) {
        *out_suffix = 's';
        return imm5 >> 3;
    } else if ((imm5 & 8) != 0) {
        *out_suffix = 'd';
        return imm5 >> 4;
    }
    return -1;
}

// Map a qualifier to a SimdRegArrangement.
DA64_SimdRegArrangement da64_qualifier_to_simd_reg(DA64_InsnOperandQualifier qual)
{
    if (qual == DA64_InsnOperandQualifier_V_8B) {
        return DA64_SimdRegArrangement_Vector8B;
    }
    if (qual == DA64_InsnOperandQualifier_V_16B) {
        return DA64_SimdRegArrangement_Vector16B;
    }
    if (qual == DA64_InsnOperandQualifier_V_2H) {
        return DA64_SimdRegArrangement_Vector2H;
    }
    if (qual == DA64_InsnOperandQualifier_V_4H) {
        return DA64_SimdRegArrangement_Vector4H;
    }
    if (qual == DA64_InsnOperandQualifier_V_8H) {
        return DA64_SimdRegArrangement_Vector8H;
    }
    if (qual == DA64_InsnOperandQualifier_V_2S) {
        return DA64_SimdRegArrangement_Vector2S;
    }
    if (qual == DA64_InsnOperandQualifier_V_4S) {
        return DA64_SimdRegArrangement_Vector4S;
    }
    if (qual == DA64_InsnOperandQualifier_V_1D) {
        return DA64_SimdRegArrangement_Vector1D;
    }
    if (qual == DA64_InsnOperandQualifier_V_2D) {
        return DA64_SimdRegArrangement_Vector2D;
    }
    if (qual == DA64_InsnOperandQualifier_V_1Q) {
        return DA64_SimdRegArrangement_Vector1Q;
    }

    return -1;
}

// Map a qualifier to a vector arrangement suffix string.
da64_str8 da64_qualifier_arrangement(DA64_InsnOperandQualifier qual)
{
    return da64_get_simd_reg_arrangement_str(da64_qualifier_to_simd_reg(qual));
}

// Resolve vector arrangement from size and Q bits using a qualifier list.
da64_str8 da64_resolve_sizeq_arrangement(DA64_InsnOperandQualifier* qualifiers, da64_u64 qualifier_count, da64_u64 size, da64_u64 q)
{
    da64_u64 idx = -1;
    if (qualifier_count >= 7) {
        // Full [8B,16B,4H,8H,2S,4S,2D]: idx = size*2+Q, D only with Q=1
        if (size == 3) {
            if (!q) {
                return da64_str8_lit("<undefined>");
            }
            idx = 6;
        } else {
            idx = size * 2 + q;
        }
    } else if (qualifier_count >= 4 && qualifiers[0] == DA64_InsnOperandQualifier_V_8B) {
        // Same as above, abbreviated
        if (size == 3) {
            if (!q) {
                return da64_str8_lit("<undefined>");
            }
            idx = 6;
        } else {
            idx = size * 2 + q;
        }

        idx = idx < qualifier_count ? idx : qualifier_count - 1;
    } else {
        idx = size * 2 + q;
    }

    if (idx < qualifier_count) {
        return da64_qualifier_arrangement(qualifiers[idx]);
    }
    return da64_str8_lit("<undefined>");
}

// Resolve qualifier index for Em/Em16 from size and Q bits.

da64_u64 da64_resolve_em_qualifier_idx(DA64_InsnOperandQualifier* qualifiers, da64_u64 qualifier_count, DA64_InsnFlags flags, da64_u32 bits, da64_u32 mask)
{
    da64_u64 size = da64_bit_range(bits, 22, 2);
    da64_u64 q = da64_bit_set(bits, 30);

    if (flags & DA64_InsnFlag_HAS_SIZEQ_FIELD) {
        if (qualifier_count == 4) {
            // [S_H, S_H, S_S, S_S]: (size-1)*2 + Q
            if (size >= 1) {
                return (size - 1) * 2 + q;
            }
        }
        if (qualifier_count == 3) {
            if (qualifiers[0] == DA64_InsnOperandQualifier_S_S) {
                // [S_S, S_S, S_D]: FP — bit22 selects S/D, Q selects width
                if ((size & 1) != 0) {
                    return 2; // D
                } else {
                    return q; // S, Q selects idx 0 or 1
                }
            } else {
                //  [S_H, S_H, S_S]: size-level, (size-1)*2+Q clamped
                if (size >= 1) {
                    da64_u64 idx = (size - 1) * 2 + q;
                    return idx < qualifier_count ? idx : qualifier_count - 1;
                }
            }
        }
        if (qualifier_count == 2) {
            if (qualifiers[0] == qualifiers[1]) {
                return q; // Same qualifier: Q selects variant
            }

            return size > 0 ? size - 1 : 0;
        }

        return 0;
    }
    if (flags & DA64_InsnFlag_HAS_ADVSIMD_SCALAR_SIZE) {
        // FP [S_S,S_D] with bit23 constrained: use bit22 alone

        da64_u32 is_fp_scalar = (qualifier_count == 2) && (qualifiers[0] == DA64_InsnOperandQualifier_S_S) && (((mask >> 23) & 1) != 0);

        if (is_fp_scalar) {
            return da64_bit_range(bits, 22, 1);
        }

        da64_u64 idx = da64_scalar_size_qualifier_idx(qualifiers, qualifier_count, size);
        return idx == -1 ? 0 : idx;
    }
    return 0;
}

///////////////////////
//~ yuraiz: Floating-point register and immediate formatting

// use crate::registers::{get_fp_reg_name, FpRegSize};
// use core::fmt::Write;
// use disarm64_defn::{defn, InsnClass, InsnFlags, InsnOperandKind, InsnOperandQualifier};

// use super::bits::bit_range;
// use super::qualifier::{immh_level, qualifier_to_fp_size, scalar_size_qualifier_idx};

// Format a floating-point register operand.
da64_u64 da64_format_fp_reg(da64_u32 bits, DA64_InsnOperand* operand, const DA64_Insn* definition, char* buf, char* buf_end)
{
    DA64_InsnOperandKind kind = operand->kind;

    da64_u32 reg_no = 0;
    if (operand->bit_field_count > 0) {
        DA64_BitfieldSpec bf = operand->bit_fields[0];
        reg_no = da64_bit_range(bits, bf.lsb, bf.width);
    } else {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    da64_reg fp_reg = { 0 };

    switch (definition->class) {
    case DA64_InsnClass_LDST_IMM9:
    case DA64_InsnClass_LDST_POS:
    case DA64_InsnClass_LDST_REGOFF:
    case DA64_InsnClass_LDST_UNSCALED: {
        da64_u32 size = da64_bit_range(bits, 30, 2);
        da64_u32 opc = da64_bit_range(bits, 22, 2);
        if (opc == 0 || opc == 1) {
            if (size == 0) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_B8, reg_no);
            } else if (size == 1) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_H16, reg_no);
            } else if (size == 2) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_S32, reg_no);
            } else if (size == 3) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_D64, reg_no);
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if ((opc == 2 || opc == 3) && size == 0) {
            fp_reg = da64_get_fp_reg(DA64_FpRegSize_Q128, reg_no);
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    } break;

    case DA64_InsnClass_LDSTPAIR_OFF:
    case DA64_InsnClass_LDSTNAPAIR_OFFS:
    case DA64_InsnClass_LDSTPAIR_INDEXED:
    case DA64_InsnClass_LOADLIT: {
        da64_u32 opc = da64_bit_range(bits, 30, 2);
        if (opc == 0) {
            fp_reg = da64_get_fp_reg(DA64_FpRegSize_S32, reg_no);
        } else if (opc == 1) {
            fp_reg = da64_get_fp_reg(DA64_FpRegSize_D64, reg_no);
        } else if (opc == 2) {
            fp_reg = da64_get_fp_reg(DA64_FpRegSize_Q128, reg_no);
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    } break;

    case DA64_InsnClass_BFLOAT16: {
        if (kind == DA64_InsnOperandKind_Fd) {
            fp_reg = da64_get_fp_reg(DA64_FpRegSize_H16, reg_no);
        } else if (kind == DA64_InsnOperandKind_Fn) {
            fp_reg = da64_get_fp_reg(DA64_FpRegSize_S32, reg_no);
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    } break;

    case DA64_InsnClass_ASIMDALL: {
        da64_u32 size = da64_bit_range(bits, 22, 2);
        da64_u32 cross_lane = da64_bit_set(bits, 15);
        if (cross_lane) {
            if (size == 0) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_B8, reg_no);
            } else if (size == 1) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_H16, reg_no);
            } else if (size == 2) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_S32, reg_no);
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else {
            if (size == 0) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_H16, reg_no);
            } else if (size == 1) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_S32, reg_no);
            } else if (size == 2) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_D64, reg_no);
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        }
    } break;

    default: {
        if (definition->flags & DA64_InsnFlag_HAS_FPTYPE_FIELD) {
            da64_u32 fp_type = da64_bit_range(bits, 22, 2);
            if (fp_type == 0) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_S32, reg_no);
            } else if (fp_type == 1) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_D64, reg_no);
            } else if (fp_type == 2) {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            } else if (fp_type == 3) {
                fp_reg = da64_get_fp_reg(DA64_FpRegSize_H16, reg_no);
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if ((definition->flags & DA64_InsnFlag_HAS_ADVSIMD_SCALAR_SIZE) && (operand->qualifier_count > 1)) {
            // Scalar ASISD: size bits[23:22] index into qualifier list.
            // FP [S_S,S_D] with bit23 constrained: bit22 alone selects S/D.
            // Integer widening [S_H,S_S] or [S_S,S_D]: size-1 as index.

            da64_u64 size = da64_bit_range(bits, 22, 2);
            da64_u32 is_fp_scalar = (operand->qualifier_count == 2)
                && (operand->qualifiers[0] == DA64_InsnOperandQualifier_S_S)
                && (((definition->mask >> 23) & 1) != 0);

            da64_u64 idx = -1;
            if (is_fp_scalar) {
                idx = da64_bit_range(bits, 22, 1);
            } else {
                idx = da64_scalar_size_qualifier_idx(operand->qualifiers, operand->qualifier_count, size);
            }

            if (idx < operand->qualifier_count) {
                DA64_FpRegSize reg_size = da64_qualifier_to_fp_size(operand->qualifiers[idx]);
                if (reg_size != -1) {
                    fp_reg = da64_get_fp_reg(reg_size, reg_no);
                } else {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if (operand->qualifier_count > 1) {
            // Multi-qualifier without ADVSIMD_SCALAR_SIZE (e.g., ASISDSHF):
            // use immh level to index, offset by first qualifier's absolute level
            da64_u64 level = da64_immh_level(bits);
            if (level == -1) {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }

            DA64_InsnOperandQualifier first_qual = operand->qualifiers[0];
            da64_u64 base = 0;
            if (first_qual == DA64_InsnOperandQualifier_S_B) {
                base = 0;
            } else if (first_qual == DA64_InsnOperandQualifier_S_H) {
                base = 1;
            } else if (first_qual == DA64_InsnOperandQualifier_S_S) {
                base = 2;
            } else if (first_qual == DA64_InsnOperandQualifier_S_D) {
                base = 3;
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }

            // For len>=3 lists, idx=level (direct mapping).
            // For len=2 lists, idx=level-base (offset by first qualifier level).
            da64_u64 idx = 0;
            if (operand->qualifier_count >= 3) {
                idx = level;
            } else {
                idx = level < base ? level - base : 0;
            }

            if (idx < operand->qualifier_count) {
                DA64_FpRegSize reg_size = da64_qualifier_to_fp_size(operand->qualifiers[idx]);
                if (reg_size != -1) {
                    fp_reg = da64_get_fp_reg(reg_size, reg_no);
                } else {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if (operand->qualifier_count > 0) {
            DA64_InsnOperandQualifier qual = operand->qualifiers[0];
            DA64_FpRegSize reg_size = da64_qualifier_to_fp_size(qual);
            if (reg_size != -1) {
                fp_reg = da64_get_fp_reg(reg_size, reg_no);
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else {
            return da64_write_op_kind(buf, buf_end, kind);
        }
    } break;
    }

    return da64_write_reg(buf, buf_end, fp_reg);
}

// Follows `bits(N) VFPExpandImm(bits(8) imm8, integer N)` in the A64 reference.
double da64_fp_expand_imm(da64_u32 size, da64_u32 imm8)
{
    da64_u64 imm8_7 = (imm8 >> 7) & 0x01; // imm8<7>
    da64_u64 imm8_6_0 = imm8 & 0x7f; // imm8<6:0>
    da64_u64 imm8_6 = imm8_6_0 >> 6; // imm8<6>
    da64_u64 imm8_6_repl4 = (imm8_6 << 3) | (imm8_6 << 2) | (imm8_6 << 1) | imm8_6; // Replicate(imm8<6>,4)

    // NOTE(yuraiz): Memory transmutation using union considered ub, but it should work.
    union {
        da64_u32 u32;
        da64_u64 u64;
        double d;
        float f;
    } result = { .d = 0.0 / 0.0 }; // nan

    if (size == 8) {
        da64_u64 imm = (imm8_7 << (63 - 32)) // imm8<7>
            | ((imm8_6 ^ 1) << (62 - 32)) // NOT(imm8<6>)
            | (imm8_6_repl4 << (58 - 32))
            | (imm8_6 << (57 - 32))
            | (imm8_6 << (56 - 32))
            | (imm8_6 << (55 - 32)) // Replicate(imm8<6>,7)
            | (imm8_6_0 << (48 - 32)); // imm8<6>:imm8<5:0>
        result.u64 = imm;
    }
    if (size == 4 || size == 2) {
        da64_u64 imm = (imm8_7 << 31) // imm8<7>
            | ((imm8_6 ^ 1) << 30) // NOT(imm8<6>)
            | (imm8_6_repl4 << 26) // Replicate(imm8<6>,4)
            | (imm8_6_0 << 19); // imm8<6>:imm8<5:0>
        result.u32 = imm;
        result.d = (double)result.f;
    }

    return result.d;
}

///////////////////////
//~ yuraiz: SIMD register, element, register list, and address formatting.

// Format a SIMD register operand with vector arrangement.
da64_u64 da64_format_simd_reg(da64_u32 bits, DA64_InsnOperand* operand, const DA64_Insn* definition, char* buf, char* buf_end)
{
    DA64_InsnOperandKind kind = operand->kind;

    da64_u32 reg_no = 0;
    if (operand->bit_field_count > 0) {
        DA64_BitfieldSpec bf = operand->bit_fields[0];
        reg_no = da64_bit_range(bits, bf.lsb, bf.width);
    } else {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    DA64_SimdRegArrangement reg_arrangement = 0;

    switch (definition->class) {
    case DA64_InsnClass_ASIMDALL: {
        da64_u64 size = da64_bit_range(bits, 22, 2);
        da64_u64 q = da64_bit_set(bits, 30);
        switch ((size << 1) | q) {
        case 0: {
            reg_arrangement = DA64_SimdRegArrangement_Vector8B;
        } break;
        case 1: {
            reg_arrangement = DA64_SimdRegArrangement_Vector16B;
        } break;
        case 2: {
            reg_arrangement = DA64_SimdRegArrangement_Vector4H;
        } break;
        case 3: {
            reg_arrangement = DA64_SimdRegArrangement_Vector8H;
        } break;
        case 5: {
            reg_arrangement = DA64_SimdRegArrangement_Vector4S;
        } break;
        default: {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
        }
    } break;

    case DA64_InsnClass_ASIMDDIFF: {
        da64_u64 size = da64_bit_range(bits, 22, 2);
        if (kind == DA64_InsnOperandKind_Vd || kind == DA64_InsnOperandKind_Vn || kind == DA64_InsnOperandKind_Vm) {
            // let len = operand.qualifiers.len();
            da64_u64 idx = 0;
            if (operand->qualifier_count >= 3) {
                idx = size;
            } else if (operand->qualifier_count == 2) {
                if (size == 0) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
                idx = size - 1;
            }

            if (idx < operand->qualifier_count) {
                reg_arrangement = da64_qualifier_to_simd_reg(operand->qualifiers[idx]);
                if (reg_arrangement != -1) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    } break;

    case DA64_InsnClass_ASIMDSHF: {
        da64_u64 immh = da64_bit_range(bits, 19, 4);
        da64_u64 q = da64_bit_set(bits, 30);
        da64_u64 immh_level = 0;
        if (immh >= 8) {
            immh_level = 3;
        } else if (immh >= 4) {
            immh_level = 2;
        } else if (immh >= 2) {
            immh_level = 1;
        } else if (immh >= 1) {
            immh_level = 0;
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        };

        da64_u64 idx = 0;
        switch (operand->qualifier_count) {
        default: {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        } break;
        case 7: {
            if (operand->qualifiers[0] == DA64_InsnOperandQualifier_V_8B) {
                if (immh_level == 3) {
                    if (!q) {
                        return da64_write_strlit(buf, buf_end, "<undefined>");
                    }
                    idx = 6;
                } else {
                    idx = immh_level * 2 + q;
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } break;
        case 2: {

            if (immh_level == 0) {
                reg_arrangement = q ? DA64_SimdRegArrangement_Vector16B : DA64_SimdRegArrangement_Vector8B;
            } else if (immh_level == 1) {
                reg_arrangement = q ? DA64_SimdRegArrangement_Vector8H : DA64_SimdRegArrangement_Vector4H;
            } else if (immh_level == 2) {
                reg_arrangement = q ? DA64_SimdRegArrangement_Vector4S : DA64_SimdRegArrangement_Vector2S;
            } else if (immh_level == 3 && q) {
                reg_arrangement = DA64_SimdRegArrangement_Vector2D;
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }

            da64_reg reg = da64_get_simd_reg(reg_no, reg_arrangement);
            return da64_write_reg(buf, buf_end, reg);
        } break;
        case 3: {
            if (operand->qualifiers[0] == DA64_InsnOperandQualifier_V_2S) {
                if (immh_level >= 3) {
                    if (!q) {
                        return da64_write_strlit(buf, buf_end, "<undefined>");
                    }
                    idx = 2;
                } else if (immh_level == 2) {
                    idx = q;
                } else {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                if (immh_level >= 3) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
                idx = immh_level;
            }
        } break;
        }

        if (idx < operand->qualifier_count) {
            reg_arrangement = da64_qualifier_to_simd_reg(operand->qualifiers[idx]);
            if (reg_arrangement != -1) {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    } break;

    case DA64_InsnClass_ASIMDINS: {
        da64_u64 idx = da64_asimdins_qualifier_idx(bits);
        if (idx == -1) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }

        if (idx < operand->qualifier_count) {
            reg_arrangement = da64_qualifier_to_simd_reg(operand->qualifiers[idx]);
            if (reg_arrangement != -1) {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    } break;

    default: {
        if (operand->qualifier_count == 0) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }

        DA64_InsnOperandQualifier qual = operand->qualifiers[0];
        DA64_InsnFlags flags = definition->flags;

        if (flags & DA64_InsnFlag_HAS_SIZEQ_FIELD && qual == DA64_InsnOperandQualifier_V_8B && operand->qualifier_count >= 4) {
            // Qualifier list starting with V_8B (4, 6, or 7 elements):
            // [8B, 16B, 4H, 8H, (2S, 4S, (2D))]

            da64_u64 size = da64_bit_range(bits, 22, 2);
            da64_u64 q = da64_bit_set(bits, 30);

            da64_u64 idx = 0;
            if (size < 3) {
                idx = size * 2 + q;
            } else if (q) {
                idx = 6;
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }

            if (idx < operand->qualifier_count) {
                reg_arrangement = da64_qualifier_to_simd_reg(operand->qualifiers[idx]);
                if (reg_arrangement != -1) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if (flags & DA64_InsnFlag_HAS_SIZEQ_FIELD && qual == DA64_InsnOperandQualifier_V_2S && operand->qualifier_count == 3) {
            // FP [V_2S, V_4S, V_2D]: bit22 = 0 (single) / 1 (double), Q = width

            da64_u64 fp_double = da64_bit_set(bits, 22);
            da64_u64 q = da64_bit_set(bits, 30);

            if (fp_double && q) {
                reg_arrangement = DA64_SimdRegArrangement_Vector2D;
            } else if (!fp_double) {
                reg_arrangement = q ? DA64_SimdRegArrangement_Vector4S : DA64_SimdRegArrangement_Vector2S;
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if (flags & DA64_InsnFlag_HAS_SIZEQ_FIELD && qual == DA64_InsnOperandQualifier_V_4H && operand->qualifier_count >= 3) {
            // V_4H-starting lists

            da64_u64 size = da64_bit_range(bits, 22, 2);
            da64_u64 q = da64_bit_set(bits, 30);

            da64_u64 idx = 0;
            if (operand->qualifier_count >= 6) {
                if (size > 2) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
                idx = size * 2 + q;
            } else if (operand->qualifier_count >= 4) {
                if (size < 1 || size > 2) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
                idx = (size - 1) * 2 + q;
            } else {
                if (size == 0) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
                idx = (size - 1) * 2 + q;
            }

            if (idx < operand->qualifier_count) {
                reg_arrangement = da64_qualifier_to_simd_reg(operand->qualifiers[idx]);
                if (reg_arrangement != -1) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if (flags & DA64_InsnFlag_HAS_SIZEQ_FIELD && operand->qualifier_count == 3) {
            // 3-element size-indexed lists (narrowing/widening)

            da64_u64 size = da64_bit_range(bits, 22, 2);
            if (size < operand->qualifier_count) {
                reg_arrangement = da64_qualifier_to_simd_reg(operand->qualifiers[size]);
                if (reg_arrangement != -1) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if (flags & DA64_InsnFlag_HAS_SPEC_DECODE_RULES && operand->qualifier_count == 2) {
            // FP narrowing/widening with bit22 selecting qualifier
            da64_u64 fp_double = da64_bit_set(bits, 22);
            if (fp_double < operand->qualifier_count) {
                reg_arrangement = da64_qualifier_to_simd_reg(operand->qualifiers[fp_double]);
                if (reg_arrangement != -1) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else if (flags & DA64_InsnFlag_HAS_SIZEQ_FIELD && operand->qualifier_count == 2) {
            // 2-element HAS_SIZEQ_FIELD:
            // Same element type (e.g. V_2S/V_4S): Q selects variant
            // Different element type (e.g. V_4S/V_2D): size selects

            da64_u64 same_element = da64_qualifier_element_size(operand->qualifiers[0]) == da64_qualifier_element_size(operand->qualifiers[1]);
            da64_u64 q = da64_bit_set(bits, 30);
            da64_u64 size = da64_bit_range(bits, 22, 2);

            da64_u64 idx = same_element ? q : (size > 0 ? size - 1 : 0);

            if (idx < operand->qualifier_count) {
                reg_arrangement = da64_qualifier_to_simd_reg(operand->qualifiers[idx]);
                if (reg_arrangement != -1) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
        } else {
            // Fixed or compact qualifier list: Q bit selects width variant

            da64_u64 is_double = 0;
            if (definition->flags & DA64_InsnFlag_HAS_SIZEQ_FIELD) {
                is_double = da64_bit_set(bits, 30);
            }

            if (!is_double) {
                reg_arrangement = da64_qualifier_to_simd_reg(qual);
                if (reg_arrangement != -1) {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            } else {
                if (qual == DA64_InsnOperandQualifier_V_8B) {
                    reg_arrangement = DA64_SimdRegArrangement_Vector16B;
                } else if (qual == DA64_InsnOperandQualifier_V_2H) {
                    reg_arrangement = DA64_SimdRegArrangement_Vector4H;
                } else if (qual == DA64_InsnOperandQualifier_V_4H) {
                    reg_arrangement = DA64_SimdRegArrangement_Vector8H;
                } else if (qual == DA64_InsnOperandQualifier_V_2S) {
                    reg_arrangement = DA64_SimdRegArrangement_Vector4S;
                } else if (qual == DA64_InsnOperandQualifier_V_1D || qual == DA64_InsnOperandQualifier_V_2D) {
                    reg_arrangement = DA64_SimdRegArrangement_Vector2D;
                } else {
                    return da64_write_strlit(buf, buf_end, "<undefined>");
                }
            }
        }

    } break;
    }

    da64_reg reg = da64_get_simd_reg(reg_no, reg_arrangement);
    return da64_write_reg(buf, buf_end, reg);
}

// Format Ed/En SIMD element operands for ASIMDINS instructions.
da64_u64 da64_format_simd_element_ed_en(da64_u32 bits, DA64_InsnOperand* operand, const DA64_Insn* definition, char* buf, char* buf_end)
{
    if (operand->bit_field_count == 0) {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    }

    DA64_BitfieldSpec bf = operand->bit_fields[0];
    da64_u64 reg_no = da64_bit_range(bits, bf.lsb, bf.width);

    da64_u32 imm5 = da64_bit_range(bits, 16, 5);

    char suffix = '0';
    da64_u32 index_from_imm5 = da64_decode_imm5_element(imm5, &suffix);
    if (index_from_imm5 == -1) {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    }

    da64_u32 index = -1;

    switch (operand->kind) {
    case DA64_InsnOperandKind_Ed: {
        index = index_from_imm5;
    } break;
    case DA64_InsnOperandKind_En: {
        da64_u32 has_ed = 0;
        for (int i = 0; i < definition->operand_count; i++) {
            if (definition->operands[i].kind == DA64_InsnOperandKind_Ed) {
                has_ed = 1;
                break;
            }
        }

        if (has_ed) {
            da64_u32 imm4 = da64_bit_range(bits, 11, 4);
            switch (suffix) {
            case 'b': {
                index = imm4;
            } break;
            case 'h': {
                index = imm4 >> 1;
            } break;
            case 's': {
                index = imm4 >> 2;
            } break;
            case 'd': {
                index = imm4 >> 3;
            } break;
            default: {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            } break;
            }
        } else {
            index = index_from_imm5;
        }
    } break;
    default: {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    } break;
    }

    char* start = buf;

    buf += da64_write_strlit(buf, buf_end, "v");
    buf += da64_write_num(buf, buf_end, reg_no, 10);
    buf += da64_write_strlit(buf, buf_end, ".");
    da64_str8 suffix_str = { &suffix, 1 };
    buf += da64_write_str8(buf, buf_end, suffix_str);
    buf += da64_write_strlit(buf, buf_end, "[");
    buf += da64_write_num(buf, buf_end, index, 10);
    buf += da64_write_strlit(buf, buf_end, "]");

    return buf - start;
}

// Format Em/Em16 SIMD element operands.

da64_u64 da64_format_simd_element_em(da64_u32 bits, DA64_InsnOperand* operand, const DA64_Insn* definition, char* buf, char* buf_end)
{
    if (operand->bit_field_count == 0 || operand->qualifier_count == 0) {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    }

    DA64_BitfieldSpec bf = operand->bit_fields[0];
    da64_u64 reg_no = da64_bit_range(bits, bf.lsb, bf.width);

    da64_u32 qual_idx = da64_resolve_em_qualifier_idx(operand->qualifiers, operand->qualifier_count, definition->flags, bits, definition->mask);

    DA64_InsnOperandQualifier qual = operand->qualifiers[qual_idx];

    switch (qual) {
    case DA64_InsnOperandQualifier_S_H: {
        da64_u32 index = 0;
        if (operand->kind == DA64_InsnOperandKind_Em16) {
            // Em16 16-bit: register V0-V15, index = H:L:M
            da64_u32 h = da64_bit_range(bits, 11, 1);
            da64_u32 l = da64_bit_range(bits, 21, 1);
            da64_u32 m = da64_bit_range(bits, 20, 1);
            index = (h << 2) | (l << 1) | m;
            reg_no = reg_no & 0xF;
        } else {
            // Em 16-bit (FCMLA): full register, index = H:L
            da64_u32 h = da64_bit_range(bits, 11, 1);
            da64_u32 l = da64_bit_range(bits, 21, 1);
            index = (h << 1) | l;
        }

        char* start = buf;

        buf += da64_write_strlit(buf, buf_end, "v");
        buf += da64_write_num(buf, buf_end, reg_no, 10);
        buf += da64_write_strlit(buf, buf_end, ".h[");
        buf += da64_write_num(buf, buf_end, index, 10);
        buf += da64_write_strlit(buf, buf_end, "]");

        return buf - start;
    } break;

    case DA64_InsnOperandQualifier_S_S: {
        // 32-bit element: index = imm2[13:12] for CRYPTOSM3, H:L otherwise

        da64_u32 index = da64_bit_range(bits, 12, 2);
        if (definition->class == DA64_InsnClass_CRYPTOSM3) {
            da64_u32 h = da64_bit_range(bits, 11, 1);
            da64_u32 l = da64_bit_range(bits, 21, 1);
            index = (h << 1) | l;
        }

        char* start = buf;

        buf += da64_write_strlit(buf, buf_end, "v");
        buf += da64_write_num(buf, buf_end, reg_no, 10);
        buf += da64_write_strlit(buf, buf_end, ".s[");
        buf += da64_write_num(buf, buf_end, index, 10);
        buf += da64_write_strlit(buf, buf_end, "]");

        return buf - start;
    } break;

    case DA64_InsnOperandQualifier_S_D: {
        da64_u32 index = da64_bit_range(bits, 11, 1);

        char* start = buf;

        buf += da64_write_strlit(buf, buf_end, "v");
        buf += da64_write_num(buf, buf_end, reg_no, 10);
        buf += da64_write_strlit(buf, buf_end, ".d[");
        buf += da64_write_num(buf, buf_end, index, 10);
        buf += da64_write_strlit(buf, buf_end, "]");

        return buf - start;
    } break;

    case DA64_InsnOperandQualifier_S_4B: {
        da64_u32 h = da64_bit_range(bits, 11, 1);
        da64_u32 l = da64_bit_range(bits, 21, 1);
        da64_u32 index = (h << 1) | l;
        reg_no = reg_no & 0xF;

        char* start = buf;

        buf += da64_write_strlit(buf, buf_end, "v");
        buf += da64_write_num(buf, buf_end, reg_no, 10);
        buf += da64_write_strlit(buf, buf_end, ".4b[");
        buf += da64_write_num(buf, buf_end, index, 10);
        buf += da64_write_strlit(buf, buf_end, "]");

        return buf - start;
    } break;

    case DA64_InsnOperandQualifier_S_2H: {
        da64_u32 h = da64_bit_range(bits, 11, 1);
        da64_u32 l = da64_bit_range(bits, 21, 1);
        da64_u32 index = (h << 1) | l;
        reg_no = reg_no & 0xF;

        char* start = buf;

        buf += da64_write_strlit(buf, buf_end, "v");
        buf += da64_write_num(buf, buf_end, reg_no, 10);
        buf += da64_write_strlit(buf, buf_end, ".2h[");
        buf += da64_write_num(buf, buf_end, index, 10);
        buf += da64_write_strlit(buf, buf_end, "]");

        return buf - start;
    } break;
    default: {
    } break;
    }

    return da64_write_strlit(buf, buf_end, "<undefined>");
}

// Write a register list `{ v{n}.{arr}, v{n+1}.{arr}, ... }`.
da64_u32 da64_write_reglist(da64_u32 first_reg, da64_u32 count, da64_str8 arrangement, char* buf, char* buf_end)
{
    char* start = buf;

    buf += da64_write_strlit(buf, buf_end, "{");

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            buf += da64_write_strlit(buf, buf_end, ", ");
        }

        da64_u32 reg = (first_reg + i) & 31;

        buf += da64_write_strlit(buf, buf_end, "v");
        buf += da64_write_num(buf, buf_end, reg, 10);
        buf += da64_write_strlit(buf, buf_end, ".");
        buf += da64_write_str8(buf, buf_end, arrangement);
    }

    buf += da64_write_strlit(buf, buf_end, "}");

    return buf - start;
}

// Write an element register list `{ v{n}.{suffix}, ... }[index]`.
da64_u32 da64_write_elemlist(da64_u32 first_reg, da64_u32 count, char suffix, da64_u32 index, char* buf, char* buf_end)
{
    char* start = buf;

    buf += da64_write_strlit(buf, buf_end, "{");

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            buf += da64_write_strlit(buf, buf_end, ", ");
        }

        da64_u32 reg = (first_reg + i) & 31;

        buf += da64_write_strlit(buf, buf_end, "v");
        buf += da64_write_num(buf, buf_end, reg, 10);
        buf += da64_write_strlit(buf, buf_end, ".");
        da64_str8 suffix_str = { &suffix, 1 };
        buf += da64_write_str8(buf, buf_end, suffix_str);
    }

    buf += da64_write_strlit(buf, buf_end, "}[");
    buf += da64_write_num(buf, buf_end, index, 10);
    buf += da64_write_strlit(buf, buf_end, "]");

    return buf - start;
}

// Format LVn register list for ASIMDTBL (tbl/tbx).
da64_u32 da64_format_simd_reglist_lvn(da64_u32 bits, DA64_InsnOperand* operand, char* buf, char* buf_end)
{
    if (operand->bit_field_count == 0) {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    }

    DA64_BitfieldSpec bf = operand->bit_fields[0];
    da64_u32 first_reg = da64_bit_range(bits, bf.lsb, bf.width);
    da64_u32 len = da64_bit_range(bits, 13, 2);
    da64_u32 count = len + 1;
    return da64_write_reglist(first_reg, count, da64_str8_lit("16b"), buf, buf_end);
}

// Format LVt/LVt_AL register list for ASISDLSE/ASISDLSO.
da64_u32 da64_format_simd_reglist_lvt(da64_u32 bits, DA64_InsnOperand* operand, const DA64_Insn* definition, char* buf, char* buf_end)
{
    da64_u32 rt = da64_bit_range(bits, 0, 5);

    da64_u32 count = 0;
    switch (definition->class) {
    case DA64_InsnClass_ASISDLSE:
    case DA64_InsnClass_ASISDLSEP: {
        da64_u32 opcode_field = da64_bit_range(bits, 12, 4);
        switch (opcode_field) {
        case 0: {
            // st4/ld4
            count = 4;
        } break;
        case 2: {
            // st1x4/ld1x4
            count = 4;
        } break;
        case 4: {
            // st3/ld3
            count = 3;
        } break;
        case 6: {
            // st1x3/ld1x3
            count = 3;
        } break;
        case 7: {
            // st1x1/ld1x1
            count = 1;
        } break;
        case 8: {
            // st2/ld2
            count = 2;
        } break;
        case 10: {
            // st1x2/ld1x2
            count = 2;
        } break;
        default: {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        } break;
        }
    } break;

    case DA64_InsnClass_ASISDLSO:
    case DA64_InsnClass_ASISDLSOP: {
        da64_u32 r = da64_bit_range(bits, 21, 1);
        da64_u32 b13 = da64_bit_range(bits, 13, 1);
        count = r ? (b13 ? 4 : 2) : (b13 ? 3 : 1);
    } break;
    default: {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    } break;
    }

    da64_u32 size = da64_bit_range(bits, 10, 2);
    da64_u32 q = da64_bit_set(bits, 30);

    da64_str8 arrangement = da64_resolve_sizeq_arrangement(operand->qualifiers, operand->qualifier_count, size, q);

    if (arrangement.size > 0) {
        return da64_write_reglist(rt, count, arrangement, buf, buf_end);
    } else {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    }
}

// Format LEt element register list for ASISDLSO.

da64_u32 da64_format_simd_elemlist(da64_u32 bits, char* buf, char* buf_end)
{
    da64_u32 rt = da64_bit_range(bits, 0, 5);

    da64_u32 r = da64_bit_range(bits, 21, 1);
    da64_u32 b13 = da64_bit_range(bits, 13, 1);
    da64_u32 count = r ? (b13 ? 4 : 2) : (b13 ? 3 : 1);

    da64_u64 opcode_upper = da64_bit_range(bits, 14, 2);
    da64_u64 q = da64_bit_range(bits, 30, 1);
    da64_u64 s = da64_bit_range(bits, 12, 1);
    da64_u64 size_field = da64_bit_range(bits, 10, 2);

    switch (opcode_upper) {
    case 0: {
        da64_u32 index = (q << 3) | (s << 2) | size_field;
        return da64_write_elemlist(rt, count, 'b', index, buf, buf_end);
    } break;
    case 1: {
        if ((size_field & 1) != 0) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
        da64_u32 index = (q << 2) | (s << 1) | (size_field >> 1);
        return da64_write_elemlist(rt, count, 'h', index, buf, buf_end);
    } break;
    case 2: {
        if ((size_field & 1) == 0) {
            da64_u32 index = (q << 1) | s;
            return da64_write_elemlist(rt, count, 's', index, buf, buf_end);
        } else if (s == 0) {
            return da64_write_elemlist(rt, count, 'd', q, buf, buf_end);
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    } break;
    default: {
    } break;
    }

    return da64_write_strlit(buf, buf_end, "<undefined>");
}

// Format SIMD_ADDR_POST: post-indexed addressing for SIMD load/store.
da64_u32 da64_format_simd_addr_post(da64_u32 bits, DA64_Insn* definition, char* buf, char* buf_end)
{
    da64_u32 rn = da64_bit_range(bits, 5, 5);
    da64_u32 rm = da64_bit_range(bits, 16, 5);
    da64_reg rn_reg = da64_get_int_reg(1, rn, 0);

    if (rm == 32) {
        da64_u32 imm = 0;
        switch (definition->class) {
        case DA64_InsnClass_ASISDLSEP: {
            da64_u32 opcode_field = da64_bit_range(bits, 12, 4);
            da64_u32 count = 0;
            switch (opcode_field) {
            case 0: {
                // st4/ld4
                count = 4;
            } break;
            case 2: {
                // st1x4/ld1x4
                count = 4;
            } break;
            case 4: {
                // st3/ld3
                count = 3;
            } break;
            case 6: {
                // st1x3/ld1x3
                count = 3;
            } break;
            case 7: {
                // st1x1/ld1x1
                count = 1;
            } break;
            case 8: {
                // st2/ld2
                count = 2;
            } break;
            case 10: {
                // st1x2/ld1x2
                count = 2;
            } break;
            default: {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            } break;
            }

            da64_u32 q = da64_bit_set(bits, 30);
            imm = count * (q ? 16 : 8);
        } break;

        case DA64_InsnClass_ASISDLSOP: {
            da64_u32 r = da64_bit_range(bits, 21, 1);
            da64_u32 b13 = da64_bit_range(bits, 13, 1);
            da64_u32 count = r ? (b13 ? 4 : 2) : (b13 ? 3 : 1);

            da64_u32 opcode_upper = da64_bit_range(bits, 14, 2);
            da64_u32 size_field = da64_bit_range(bits, 10, 2);
            da64_u32 s = da64_bit_range(bits, 12, 1);

            da64_u32 elem_size = 0;
            if (opcode_upper == 0) {
                elem_size = 1;
            } else if (opcode_upper == 1) {
                elem_size = 2;
            } else if (opcode_upper == 2 && (size_field & 1) == 0) {
                elem_size = 4;
            } else if (opcode_upper == 2 && s == 0) {
                elem_size = 8;
            } else if (opcode_upper == 3) {
                elem_size = 1 << size_field;
            } else {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }

            imm = count * elem_size;
        } break;

        default: {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
        }

        char* start = buf;

        buf += da64_write_strlit(buf, buf_end, "[");
        buf += da64_write_reg(buf, buf_end, rn_reg);
        buf += da64_write_strlit(buf, buf_end, "], #");
        buf += da64_write_imm(buf, buf_end, imm);

        return buf - start;
    } else {
        char* start = buf;

        da64_reg rm_reg = da64_get_int_reg(1, rm, 1);
        buf += da64_write_strlit(buf, buf_end, "[");
        buf += da64_write_reg(buf, buf_end, rn_reg);
        buf += da64_write_strlit(buf, buf_end, "], ");
        buf += da64_write_reg(buf, buf_end, rm_reg);

        return buf - start;
    }

    return da64_write_strlit(buf, buf_end, "<undefined>");
}

///////////////////////
//~ yuraiz:  Integer register and shift/extend operand formatting.

da64_u32 da64_format_int_operand_reg_pair(da64_u32 is_pair, da64_u32 bits, DA64_InsnOperand* operand, const DA64_Insn* definition, da64_u32 with_zr, char* buf, char* buf_end)
{
    DA64_InsnFlags flags = definition->flags;
    da64_u32 is_64 = 0;
    if (flags & DA64_InsnFlag_HAS_SF_FIELD) {
        is_64 = da64_bit_set(bits, 31);
    } else if (flags & DA64_InsnFlag_HAS_LDS_SIZE_IN_BIT_22) {
        is_64 = !da64_bit_set(bits, 22);
    } else if (
        ((flags & DA64_InsnFlag_HAS_LSE_SZ_FIELD) && (operand->kind == DA64_InsnOperandKind_Rt || operand->kind == DA64_InsnOperandKind_Rs))
        || ((flags & DA64_InsnFlag_HAS_ADVSIMV_GPRSIZE_IN_Q) && (operand->kind == DA64_InsnOperandKind_Rt || operand->kind == DA64_InsnOperandKind_Rt2))) {
        is_64 = da64_bit_set(bits, 30);
    } else if (definition->class == DA64_InsnClass_ASIMDINS && (flags & DA64_InsnFlag_HAS_ADVSIMV_GPRSIZE_IN_Q)) {
        // SMOV/UMOV: GPR size from Q bit
        is_64 = da64_bit_set(bits, 30);
    } else if (definition->class == DA64_InsnClass_ASIMDINS && operand->qualifier_count > 0) {
        // DUP (general): GPR size from imm5 qualifier index
        da64_u32 idx = da64_asimdins_qualifier_idx(bits);
        if (idx == -1) {
            idx = 0;
        }
        if (operand->qualifier_count > idx) {
            is_64 = operand->qualifiers[idx] == DA64_InsnOperandQualifier_X;
        } else {
            is_64 = 0;
        }
    } else if (operand->qualifier_count == 0 || (operand->qualifiers[0] == DA64_InsnOperandQualifier_X)) {
        is_64 = 1;
    } else if (
        definition->class == DA64_InsnClass_LDST_IMM9
        || definition->class == DA64_InsnClass_LDST_POS
        || definition->class == DA64_InsnClass_LDST_REGOFF
        || definition->class == DA64_InsnClass_LDST_UNPRIV
        || definition->class == DA64_InsnClass_LDST_UNSCALED) {
        da64_u32 size = da64_bit_range(bits, 30, 2);
        da64_u32 opc1 = da64_bit_set(bits, 23);
        da64_u32 opc0 = da64_bit_set(bits, 22);
        if (!opc1) {
            // Store or zero-extending load, not signed.
            // Bit 22 is set if this is a load.
            is_64 = size == 3;
        } else {
            // Sign-extending load
            if ((size == 2) && opc0) {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
            is_64 = !opc0;
        }
    } else {
        is_64 = 0;
    }

    if (operand->bit_field_count > 0) {
        DA64_BitfieldSpec bit_field_spec = operand->bit_fields[0];
        da64_u32 reg_no = da64_bit_range(bits, bit_field_spec.lsb, bit_field_spec.width);

        if (is_pair) {
            if (reg_no & 1) {
                return da64_write_strlit(buf, buf_end, "<undefined>");
            }
            reg_no += 1;
        }

        da64_reg reg = da64_get_int_reg(is_64, reg_no, with_zr);
        return da64_write_reg(buf, buf_end, reg);
    } else {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    }

    return 0;
}

// Format an integer register (32-bit or 64-bit).
da64_u32 da64_format_int_operand_reg(da64_u32 bits, DA64_InsnOperand* operand, const DA64_Insn* definition, da64_u32 with_zr, char* buf, char* buf_end)
{
    da64_u32 is_pair = 0;
    return da64_format_int_operand_reg_pair(is_pair, bits, operand, definition, with_zr, buf, buf_end);
}

// Format a register with extended shift operand.
da64_u32 da64_format_operand_reg_ext(da64_u32 bits, char* buf, char* buf_end)
{
    da64_u32 regd = da64_bit_range(bits, 0, 5);
    da64_u32 regn = da64_bit_range(bits, 5, 5);
    da64_u32 imm3 = da64_bit_range(bits, 10, 3);
    da64_u32 option = da64_bit_range(bits, 13, 3);
    da64_u32 regm = da64_bit_range(bits, 16, 5);
    da64_u32 sf = da64_bit_range(bits, 31, 1);

    if (imm3 > 4) {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    }

    da64_u32 extend_use_lsl = !sf ? 2 : 3;

    da64_str8 extend = da64_str8_lit("");

    if ((regd == 31 || regn == 31) && option == extend_use_lsl) {
        if (imm3 != 0) {
            extend = da64_str8_lit("lsl");
        }
    } else {
        switch (option) {
        case 0: {
            extend = da64_str8_lit("uxtb");
        } break;
        case 1: {
            extend = da64_str8_lit("uxth");
        } break;
        case 2: {
            extend = da64_str8_lit("uxtw");
        } break;
        case 3: {
            extend = da64_str8_lit("uxtx");
        } break;
        case 4: {
            extend = da64_str8_lit("sxtb");
        } break;
        case 5: {
            extend = da64_str8_lit("sxth");
        } break;
        case 6: {
            extend = da64_str8_lit("sxtw");
        } break;
        case 7: {
            extend = da64_str8_lit("sxtx");
        } break;
        }
    }

    da64_u32 is_64bit = sf && (option & 3) == 3;

    da64_reg reg = da64_get_int_reg(is_64bit, regm, 1);

    char* start = buf;

    buf += da64_write_reg(buf, buf_end, reg);
    if (extend.size > 0) {
        buf += da64_write_strlit(buf, buf_end, ", ");
        buf += da64_write_str8(buf, buf_end, extend);
        if (imm3 != 0) {
            buf += da64_write_strlit(buf, buf_end, " #");
            buf += da64_write_imm(buf, buf_end, imm3);
        }
    }

    return buf - start;
}

// Format a register with shift operand.
da64_u32 da64_format_operand_reg_shift(da64_u32 bits, char* buf, char* buf_end)
{
    da64_u32 imm6 = da64_bit_range(bits, 10, 6);
    da64_u32 regm = da64_bit_range(bits, 16, 5);
    da64_u32 shift = da64_bit_range(bits, 23, 2);
    da64_u32 sf = da64_bit_range(bits, 31, 1);

    if (sf == 0 && (imm6 & 0x20) != 0) {
        return da64_write_strlit(buf, buf_end, "<undefined>");
    }

    da64_str8 shift_name = da64_str8_lit("<undefined>");

    if (shift == 0) {
        shift_name = da64_str8_lit("lsl");
    } else if (shift == 1) {
        shift_name = da64_str8_lit("lsr");
    } else if (shift == 2) {
        shift_name = da64_str8_lit("asr");
    } else if (shift == 3) {
        if (!da64_bit_set(bits, 24)) {
            shift_name = da64_str8_lit("ror");
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    }
    da64_reg reg = da64_get_int_reg(sf, regm, 1);

    char* start = buf;
    buf += da64_write_reg(buf, buf_end, reg);
    if (imm6 != 0 || shift != 0) {
        buf += da64_write_strlit(buf, buf_end, ", ");
        buf += da64_write_str8(buf, buf_end, shift_name);
        buf += da64_write_strlit(buf, buf_end, " #");
        buf += da64_write_imm(buf, buf_end, imm6);
    }
    return buf - start;
}

///////////////////////
//~ yuraiz: Main formatting

// Format an operand to a string.
da64_u32 da64_format_operand(da64_u64 pos, da64_u64 pc, da64_u32 bits, DA64_InsnOperand* operand, const DA64_Insn* definition, da64_u32* stop, char* buf, char* buf_end)
{
    DA64_InsnOperandKind kind = operand->kind;

    switch (kind) {
    default: {
        return da64_write_strlit(buf, buf_end, "unknown");
    } break;

    case DA64_InsnOperandKind_Rd:
    case DA64_InsnOperandKind_Rn:
    case DA64_InsnOperandKind_Rm:
    case DA64_InsnOperandKind_Rt:
    case DA64_InsnOperandKind_Rt2:
    case DA64_InsnOperandKind_Rs:
    case DA64_InsnOperandKind_Ra:
    case DA64_InsnOperandKind_Rt_LS64:
    case DA64_InsnOperandKind_Rt_SYS:
    case DA64_InsnOperandKind_SVE_Rm:
    case DA64_InsnOperandKind_LSE128_Rt:
    case DA64_InsnOperandKind_LSE128_Rt2: {
        da64_u32 with_zr = 1;
        return da64_format_int_operand_reg(bits, operand, definition, with_zr, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_PAIRREG:
    case DA64_InsnOperandKind_PAIRREG_OR_XZR: {
        if (pos == 0 || pos > definition->operand_count) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }

        DA64_InsnOperand prev_operand = definition->operands[pos - 1];

        da64_u32 is_pair = 1;
        da64_u32 with_zr = 1;
        return da64_format_int_operand_reg_pair(is_pair, bits, &prev_operand, definition, with_zr, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_Rd_SP:
    case DA64_InsnOperandKind_Rn_SP:
    case DA64_InsnOperandKind_Rt_SP:
    case DA64_InsnOperandKind_SVE_Rn_SP:
    case DA64_InsnOperandKind_Rm_SP: {
        da64_u32 with_zr = 0;
        return da64_format_int_operand_reg(bits, operand, definition, with_zr, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_Rm_EXT: {
        return da64_format_operand_reg_ext(bits, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_Rm_SFT: {
        return da64_format_operand_reg_shift(bits, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_Fd:
    case DA64_InsnOperandKind_Fn:
    case DA64_InsnOperandKind_Fm:
    case DA64_InsnOperandKind_Fa:
    case DA64_InsnOperandKind_Ft:
    case DA64_InsnOperandKind_Ft2:
    case DA64_InsnOperandKind_Sd:
    case DA64_InsnOperandKind_Sn:
    case DA64_InsnOperandKind_Sm: {
        return da64_format_fp_reg(bits, operand, definition, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_SVE_VZn: {
        return da64_write_strlit(buf, buf_end, ":SVE_VZn:");
    } break;

    case DA64_InsnOperandKind_SVE_Vd:
    case DA64_InsnOperandKind_SVE_Vm:
    case DA64_InsnOperandKind_SVE_Vn:
    case DA64_InsnOperandKind_Va:
    case DA64_InsnOperandKind_Vd:
    case DA64_InsnOperandKind_Vn:
    case DA64_InsnOperandKind_Vm: {
        return da64_format_simd_reg(bits, operand, definition, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_Ed:
    case DA64_InsnOperandKind_En: {
        return da64_format_simd_element_ed_en(bits, operand, definition, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_Em:
    case DA64_InsnOperandKind_Em16: {
        return da64_format_simd_element_em(bits, operand, definition, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_VdD1:
    case DA64_InsnOperandKind_VnD1: {
        if (operand->bit_field_count > 0) {
            DA64_BitfieldSpec bf = operand->bit_fields[0];
            da64_u32 reg_no = da64_bit_range(bits, bf.lsb, bf.width);
            char* start = buf;
            buf += da64_write_strlit(buf, buf_end, "v");
            buf += da64_write_imm(buf, buf_end, reg_no);
            buf += da64_write_strlit(buf, buf_end, ".d[1]");
            return buf - start;
        } else {
            return da64_write_op_kind(buf, buf_end, kind);
        }
    } break;

    case DA64_InsnOperandKind_LVn: {
        return da64_format_simd_reglist_lvn(bits, operand, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_LVt:
    case DA64_InsnOperandKind_LVt_AL: {
        return da64_format_simd_reglist_lvt(bits, operand, definition, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_LEt: {
        return da64_format_simd_elemlist(bits, buf, buf_end);
    } break;

    case DA64_InsnOperandKind_SVE_Pd:
    case DA64_InsnOperandKind_SVE_Pg3:
    case DA64_InsnOperandKind_SVE_Pg4_5:
    case DA64_InsnOperandKind_SVE_Pg4_10:
    case DA64_InsnOperandKind_SVE_Pg4_16:
    case DA64_InsnOperandKind_SVE_Pm:
    case DA64_InsnOperandKind_SVE_Pn:
    case DA64_InsnOperandKind_SVE_Pt:
    case DA64_InsnOperandKind_SME_Pm: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SVE_PNd:
    case DA64_InsnOperandKind_SVE_PNg4_10:
    case DA64_InsnOperandKind_SVE_PNn:
    case DA64_InsnOperandKind_SVE_PNt:
    case DA64_InsnOperandKind_SME_PNd3:
    case DA64_InsnOperandKind_SME_PNg3:
    case DA64_InsnOperandKind_SME_PNn: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_Pdx2:
    case DA64_InsnOperandKind_SME_PdxN: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_PNn3_INDEX1:
    case DA64_InsnOperandKind_SME_PNn3_INDEX2: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SVE_Za_5:
    case DA64_InsnOperandKind_SVE_Za_16:
    case DA64_InsnOperandKind_SVE_Zd:
    case DA64_InsnOperandKind_SVE_Zm_5:
    case DA64_InsnOperandKind_SVE_Zm_16:
    case DA64_InsnOperandKind_SVE_Zn:
    case DA64_InsnOperandKind_SVE_Zt:
    case DA64_InsnOperandKind_SME_Zm: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SVE_ZnxN:
    case DA64_InsnOperandKind_SVE_ZtxN:
    case DA64_InsnOperandKind_SME_Zdnx2:
    case DA64_InsnOperandKind_SME_Zdnx4:
    case DA64_InsnOperandKind_SME_Zmx2:
    case DA64_InsnOperandKind_SME_Zmx4:
    case DA64_InsnOperandKind_SME_Znx2:
    case DA64_InsnOperandKind_SME_Znx4:
    case DA64_InsnOperandKind_SME_Ztx2_STRIDED:
    case DA64_InsnOperandKind_SME_Ztx4_STRIDED:
    case DA64_InsnOperandKind_SME_Zt2:
    case DA64_InsnOperandKind_SME_Zt3:
    case DA64_InsnOperandKind_SME_Zt4: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_SVE_Zm3_INDEX:
    case DA64_InsnOperandKind_SVE_Zm3_22_INDEX:
    case DA64_InsnOperandKind_SVE_Zm3_19_INDEX:
    case DA64_InsnOperandKind_SVE_Zm3_11_INDEX:
    case DA64_InsnOperandKind_SVE_Zm4_11_INDEX:
    case DA64_InsnOperandKind_SVE_Zm4_INDEX:
    case DA64_InsnOperandKind_SVE_Zn_INDEX:
    case DA64_InsnOperandKind_SME_Zm_INDEX1:
    case DA64_InsnOperandKind_SME_Zm_INDEX2:
    case DA64_InsnOperandKind_SME_Zm_INDEX3_1:
    case DA64_InsnOperandKind_SME_Zm_INDEX3_2:
    case DA64_InsnOperandKind_SME_Zm_INDEX3_10:
    case DA64_InsnOperandKind_SVE_Zn_5_INDEX:
    case DA64_InsnOperandKind_SME_Zm_INDEX4_1:
    case DA64_InsnOperandKind_SME_Zm_INDEX4_10:
    case DA64_InsnOperandKind_SME_Zn_INDEX1_16:
    case DA64_InsnOperandKind_SME_Zn_INDEX2_15:
    case DA64_InsnOperandKind_SME_Zn_INDEX2_16:
    case DA64_InsnOperandKind_SME_Zn_INDEX3_14:
    case DA64_InsnOperandKind_SME_Zn_INDEX3_15:
    case DA64_InsnOperandKind_SME_Zn_INDEX4_14:
    case DA64_InsnOperandKind_SVE_Zm_imm4: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_ZAda_2b:
    case DA64_InsnOperandKind_SME_ZAda_3b: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_ZA_HV_idx_src:
    case DA64_InsnOperandKind_SME_ZA_HV_idx_srcxN:
    case DA64_InsnOperandKind_SME_ZA_HV_idx_dest:
    case DA64_InsnOperandKind_SME_ZA_HV_idx_destxN:
    case DA64_InsnOperandKind_SME_ZA_HV_idx_ldstr: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_list_of_64bit_tiles: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_ZA_array_off1x4:
    case DA64_InsnOperandKind_SME_ZA_array_off2x2:
    case DA64_InsnOperandKind_SME_ZA_array_off2x4:
    case DA64_InsnOperandKind_SME_ZA_array_off3_0:
    case DA64_InsnOperandKind_SME_ZA_array_off3_5:
    case DA64_InsnOperandKind_SME_ZA_array_off3x2:
    case DA64_InsnOperandKind_SME_ZA_array_off4: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_ZA_array_vrsb_1:
    case DA64_InsnOperandKind_SME_ZA_array_vrsh_1:
    case DA64_InsnOperandKind_SME_ZA_array_vrss_1:
    case DA64_InsnOperandKind_SME_ZA_array_vrsd_1:
    case DA64_InsnOperandKind_SME_ZA_array_vrsb_2:
    case DA64_InsnOperandKind_SME_ZA_array_vrsh_2:
    case DA64_InsnOperandKind_SME_ZA_array_vrss_2:
    case DA64_InsnOperandKind_SME_ZA_array_vrsd_2: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_SM_ZA: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_SME_PnT_Wm_imm: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SME_VLxN_10:
    case DA64_InsnOperandKind_SME_VLxN_13: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_CRn: {
        return da64_write_c_reg(buf, buf_end, da64_bit_range(bits, 12, 4));
    } break;
    case DA64_InsnOperandKind_CRm: {
        return da64_write_c_reg(buf, buf_end, da64_bit_range(bits, 8, 4));
    } break;

    case DA64_InsnOperandKind_IMMR: {
        da64_u32 immr = da64_bit_range(bits, 16, 6);
        da64_u32 n = da64_bit_set(bits, 22);
        da64_u32 sf = da64_bit_set(bits, 31);

        if ((sf && !n) || (!sf && (n || da64_bit_set(immr, 5)))) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
        return da64_write_imm(buf, buf_end, immr);
    } break;
    case DA64_InsnOperandKind_IMMS: {
        da64_u32 imms = da64_bit_range(bits, 10, 6);
        da64_u32 n = da64_bit_set(bits, 22);
        da64_u32 sf = da64_bit_set(bits, 31);

        if ((sf && !n) || (!sf && (n || da64_bit_set(imms, 5)))) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
        return da64_write_imm(buf, buf_end, imms);
    } break;

    case DA64_InsnOperandKind_BIT_NUM: {
        da64_u32 b5 = da64_bit_range(bits, 31, 1);
        da64_u32 b40 = da64_bit_range(bits, 19, 5);
        da64_u32 bit_num = (b5 << 5) | b40;
        return da64_write_imm(buf, buf_end, bit_num);
    } break;

    case DA64_InsnOperandKind_FBITS: {
        da64_u32 ftype = da64_bit_range(bits, 22, 2);
        if (ftype == 2) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
        da64_u32 sf = da64_bit_set(bits, 31);
        da64_u32 scale = 64 - da64_bit_range(bits, 10, 6);
        if (!sf && scale > 32) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
        return da64_write_imm(buf, buf_end, scale);
    } break;

    case DA64_InsnOperandKind_IMM0: {
        return da64_write_strlit(buf, buf_end, "#0");
    } break;
    case DA64_InsnOperandKind_TME_UIMM16: {
        return da64_write_imm(buf, buf_end, da64_bit_range(bits, 5, 16));
    } break;

    case DA64_InsnOperandKind_IMM_VLSR: {
        da64_u32 immh = da64_bit_range(bits, 19, 4);
        da64_u32 immb = da64_bit_range(bits, 16, 3);

        da64_u32 esize = 0;
        if (immh >= 8) {
            esize = 64;
        } else if (immh >= 4) {
            esize = 32;
        } else if (immh >= 2) {
            esize = 16;
        } else {
            esize = 8;
        }
        da64_u32 shift = (2 * esize) - ((immh << 3) | immb);
        return da64_write_imm(buf, buf_end, shift);
    } break;

    case DA64_InsnOperandKind_IMM_VLSL: {
        da64_u32 immh = da64_bit_range(bits, 19, 4);
        da64_u32 immb = da64_bit_range(bits, 16, 3);
        da64_u32 esize = 0;
        if (immh >= 8) {
            esize = 64;
        } else if (immh >= 4) {
            esize = 32;
        } else if (immh >= 2) {
            esize = 16;
        } else {
            esize = 8;
        }

        da64_u32 shift = ((immh << 3) | immb) - esize;
        return da64_write_imm(buf, buf_end, shift);
    } break;

    case DA64_InsnOperandKind_SHLL_IMM: {
        da64_u32 size = da64_bit_range(bits, 22, 2);
        return da64_write_imm(buf, buf_end, (da64_u32)8 << size);
    } break;

    case DA64_InsnOperandKind_IMM_ROT1:
    case DA64_InsnOperandKind_IMM_ROT2: {
        da64_u32 rot = 0;
        if (operand->bit_field_count > 0) {
            DA64_BitfieldSpec bf = operand->bit_fields[0];
            rot = da64_bit_range(bits, bf.lsb, bf.width);
        } else {
            rot = 0;
        }
        return da64_write_imm(buf, buf_end, rot * 90);
    } break;
    case DA64_InsnOperandKind_IMM_ROT3: {
        da64_u32 rot = 0;
        if (operand->bit_field_count > 0) {
            DA64_BitfieldSpec bf = operand->bit_fields[0];
            rot = da64_bit_range(bits, bf.lsb, bf.width);
        } else {
            rot = 0;
        }
        return da64_write_imm(buf, buf_end, rot * 180 + 90);
    } break;

    case DA64_InsnOperandKind_IDX: {
        da64_u32 imm4 = da64_bit_range(bits, 11, 4);
        da64_u32 q = da64_bit_set(bits, 30);
        if (!q && imm4 > 7) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        } else {
            return da64_write_imm(buf, buf_end, imm4);
        }
    } break;

    case DA64_InsnOperandKind_IMM: {
        da64_u32 imm6 = da64_bit_range(bits, 10, 6);
        return da64_write_imm(buf, buf_end, imm6);
    } break;

    case DA64_InsnOperandKind_WIDTH:
    case DA64_InsnOperandKind_SIMM5:
    case DA64_InsnOperandKind_SME_SHRIMM4:
    case DA64_InsnOperandKind_SME_SHRIMM5:
    case DA64_InsnOperandKind_SVE_SHLIMM_PRED:
    case DA64_InsnOperandKind_SVE_SHLIMM_UNPRED:
    case DA64_InsnOperandKind_SVE_SHLIMM_UNPRED_22:
    case DA64_InsnOperandKind_SVE_SHRIMM_PRED:
    case DA64_InsnOperandKind_SVE_SHRIMM_UNPRED:
    case DA64_InsnOperandKind_SVE_SHRIMM_UNPRED_22:
    case DA64_InsnOperandKind_SVE_SIMM5:
    case DA64_InsnOperandKind_SVE_SIMM5B:
    case DA64_InsnOperandKind_SVE_SIMM6:
    case DA64_InsnOperandKind_SVE_SIMM8:
    case DA64_InsnOperandKind_SVE_UIMM3:
    case DA64_InsnOperandKind_SVE_UIMM7:
    case DA64_InsnOperandKind_SVE_UIMM8:
    case DA64_InsnOperandKind_SVE_UIMM8_53:
    case DA64_InsnOperandKind_SVE_IMM_ROT1:
    case DA64_InsnOperandKind_SVE_IMM_ROT2:
    case DA64_InsnOperandKind_SVE_IMM_ROT3: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_CSSC_SIMM8: {
        return da64_write_signed_imm(buf, buf_end, (signed char)da64_bit_range(bits, 10, 8));
    } break;
    case DA64_InsnOperandKind_CSSC_UIMM8: {
        return da64_write_imm(buf, buf_end, da64_bit_range(bits, 10, 8));
    } break;
    case DA64_InsnOperandKind_UIMM4_ADDG: {
        return da64_write_imm(buf, buf_end, da64_bit_range(bits, 10, 4));
    } break;
    case DA64_InsnOperandKind_UIMM10: {
        const da64_u32 LOG2_TAG_GRANULE = 4;
        return da64_write_imm(buf, buf_end, da64_bit_range(bits, 16, 6) << LOG2_TAG_GRANULE);
    } break;

    case DA64_InsnOperandKind_SVE_I1_HALF_ONE:
    case DA64_InsnOperandKind_SVE_I1_HALF_TWO:
    case DA64_InsnOperandKind_SVE_I1_ZERO_ONE: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SVE_PATTERN: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SVE_PATTERN_SCALED: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SVE_PRFOP: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_IMM_MOV: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_FPIMM0: {
        return da64_write_strlit(buf, buf_end, "#0.0");
    } break;

    case DA64_InsnOperandKind_AIMM: {
        da64_u32 shift = da64_bit_set(bits, 22);
        da64_u32 imm12 = da64_bit_range(bits, 10, 12);

        char* start = buf;
        buf += da64_write_strlit(buf, buf_end, "#");
        buf += da64_write_imm(buf, buf_end, imm12);

        if (shift) {
            buf += da64_write_strlit(buf, buf_end, ", lsl #12");
        }
        return buf - start;
    } break;

    case DA64_InsnOperandKind_HALF: {
        da64_u32 hw = da64_bit_range(bits, 21, 2);
        if (!da64_bit_set(bits, 31) && da64_bit_set(hw, 1)) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }

        da64_u32 imm16 = da64_bit_range(bits, 5, 16);
        da64_u32 shift = hw << 4;

        char* start = buf;
        buf += da64_write_imm(buf, buf_end, imm16);
        buf += da64_write_strlit(buf, buf_end, ", lsl ");
        buf += da64_write_imm(buf, buf_end, shift);
        return buf - start;
    } break;

    case DA64_InsnOperandKind_LIMM: {
        da64_u32 imms = da64_bit_range(bits, 10, 6);
        da64_u32 immr = da64_bit_range(bits, 16, 6);
        da64_u32 n = da64_bit_range(bits, 22, 1);
        da64_u32 is_64bit = da64_bit_set(bits, 31);
        da64_u32 byte_count = is_64bit ? 8 : 4;

        da64_u64 limm = 0;
        if (da64_decode_limm(byte_count, n, immr, imms, &limm)) {
            return da64_write_imm(buf, buf_end, limm);
        } else {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
    } break;
    case DA64_InsnOperandKind_SVE_INV_LIMM:
    case DA64_InsnOperandKind_SVE_LIMM:
    case DA64_InsnOperandKind_SVE_LIMM_MOV: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SIMD_IMM: {
        da64_u64 imm8 = (da64_bit_range(bits, 16, 3) << 5) | da64_bit_range(bits, 5, 5);
        da64_u64 imm = 0;
        for (int i = 0; i < 8; i++) {
            da64_u64 byte = ((imm8 & (i << i)) != 0) ? 0xff : 0x00;
            imm |= byte << (i * 8);
        }
        return da64_write_imm(buf, buf_end, imm);
    }

    case DA64_InsnOperandKind_SIMD_IMM_SFT: {
        da64_u64 imm8 = (da64_bit_range(bits, 16, 3) << 5) | da64_bit_range(bits, 5, 5);
        da64_u64 cmode = da64_bit_range(bits, 12, 4);

        *stop = 1;
        if (cmode >> 1 == 6) {
            da64_u64 msl = da64_bit_set(cmode, 0) ? 16 : 8;
            char* start = buf;
            buf += da64_write_imm(buf, buf_end, imm8);
            buf += da64_write_strlit(buf, buf_end, ", MSL ");
            buf += da64_write_imm(buf, buf_end, msl);
            return buf - start;
        }
        if ((cmode & 9) == 0 || (cmode & 9) == 1) {
            da64_u64 lsl = ((da64_u64)da64_bit_range(cmode, 1, 2)) * 8;
            if (lsl != 0) {
                char* start = buf;
                buf += da64_write_imm(buf, buf_end, imm8);
                buf += da64_write_strlit(buf, buf_end, ", LSL ");
                buf += da64_write_imm(buf, buf_end, lsl);
                return buf - start;
            } else {
                return da64_write_imm(buf, buf_end, imm8);
            }
        }
        if (((cmode & 13) == 8)
            || ((cmode & 13) == 9)) {
            da64_u64 lsl = da64_bit_set(cmode, 1) ? 8 : 0;
            if (lsl != 0) {
                char* start = buf;
                buf += da64_write_imm(buf, buf_end, imm8);
                buf += da64_write_strlit(buf, buf_end, ", LSL ");
                buf += da64_write_imm(buf, buf_end, lsl);
                return buf - start;
            } else {
                return da64_write_imm(buf, buf_end, imm8);
            }
        }
        if ((cmode >> 1) == 7) {
            return da64_write_imm(buf, buf_end, imm8);
        }
    } break;

    case DA64_InsnOperandKind_SIMD_FPIMM: {
        da64_u64 imm8 = (da64_bit_range(bits, 16, 3) << 5) | da64_bit_range(bits, 5, 5);
        double fp_imm = da64_fp_expand_imm(8, imm8);
        return da64_write_double(buf, buf_end, fp_imm);
    } break;

    case DA64_InsnOperandKind_FPIMM: {
        da64_u32 fp_type = da64_bit_range(bits, 22, 2);
        da64_u32 size = 8;
        if (fp_type == 0) {
            size = 4;
        } else if (fp_type == 1) {
            size = 8;
        } else if (fp_type == 3) {
            size = 2;
        } else if (fp_type == 2) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }

        da64_u32 imm8 = da64_bit_range(bits, 13, 8);
        double fp_imm = da64_fp_expand_imm(size, imm8);
        return da64_write_double(buf, buf_end, fp_imm);
    } break;

    case DA64_InsnOperandKind_SVE_AIMM:
    case DA64_InsnOperandKind_SVE_ASIMM:
    case DA64_InsnOperandKind_SVE_FPIMM8: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_EXCEPTION: {
        da64_u32 imm16 = da64_bit_range(bits, 5, 16);
        return da64_write_imm(buf, buf_end, imm16);
    } break;

    case DA64_InsnOperandKind_UNDEFINED: {
        da64_u32 imm16 = da64_bit_range(bits, 0, 16);
        return da64_write_imm(buf, buf_end, imm16);
    } break;

    case DA64_InsnOperandKind_CCMP_IMM: {
        da64_u32 imm5 = da64_bit_range(bits, 16, 5);
        return da64_write_imm(buf, buf_end, imm5);
    } break;

    case DA64_InsnOperandKind_NZCV: {
        da64_u32 imm4 = da64_bit_range(bits, 0, 4);
        return da64_write_imm(buf, buf_end, imm4);
    } break;

    case DA64_InsnOperandKind_IMM_2: {
        da64_u32 imm = da64_bit_range(bits, 15, 6);
        return da64_write_imm(buf, buf_end, imm);
    } break;

    case DA64_InsnOperandKind_MASK: {
        da64_u32 mask = da64_bit_range(bits, 0, 4);
        return da64_write_imm(buf, buf_end, mask);
    } break;

    case DA64_InsnOperandKind_UIMM3_OP1: {
        da64_u32 imm = da64_bit_range(bits, 16, 3);
        return da64_write_imm(buf, buf_end, imm);
    } break;

    case DA64_InsnOperandKind_UIMM3_OP2: {
        da64_u32 imm = da64_bit_range(bits, 5, 3);
        return da64_write_imm(buf, buf_end, imm);
    } break;

    case DA64_InsnOperandKind_UIMM4: {
        da64_u32 imm4 = da64_bit_range(bits, 8, 4);
        return da64_write_imm(buf, buf_end, imm4);
    } break;

    case DA64_InsnOperandKind_BARRIER_ISB: {
        da64_u32 imm4 = da64_bit_range(bits, 8, 4);
        if (imm4 != 0xf) {
            return da64_write_imm(buf, buf_end, imm4);
        }
    } break;

    case DA64_InsnOperandKind_UIMM7: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_COND:
    case DA64_InsnOperandKind_COND1: {
        da64_u32 cond = da64_bit_range(bits, 12, 4);
        return da64_write_str8(buf, buf_end, da64_cond_name(cond));
    } break;

    case DA64_InsnOperandKind_ADDR_PCREL14: {
        da64_u64 offset = da64_bit_range(bits, 5, 14);
        offset = da64_sign_extend(offset, 13) << 2;
        return da64_write_imm(buf, buf_end, pc + offset);
    } break;
    case DA64_InsnOperandKind_ADDR_PCREL19: {
        da64_u64 offset = da64_bit_range(bits, 5, 19);
        offset = da64_sign_extend(offset, 18) << 2;
        return da64_write_imm(buf, buf_end, pc + offset);
    } break;
    case DA64_InsnOperandKind_ADDR_PCREL21: {
        da64_u64 offset = (da64_bit_range(bits, 5, 19) << 2) | da64_bit_range(bits, 29, 2);
        offset = da64_sign_extend(offset, 20);
        return da64_write_imm(buf, buf_end, pc + offset);
    } break;
    case DA64_InsnOperandKind_ADDR_ADRP: {
        da64_u64 offset = (da64_bit_range(bits, 5, 19) << 2) | da64_bit_range(bits, 29, 2);
        offset = da64_sign_extend(offset, 20) << 12;
        da64_u64 m_pc = pc & !((1 << 12) - 1);
        return da64_write_imm(buf, buf_end, m_pc + offset);
    } break;
    case DA64_InsnOperandKind_ADDR_PCREL26: {
        da64_u64 offset = da64_bit_range(bits, 0, 26);
        offset = da64_sign_extend(offset, 25) << 2;
        return da64_write_imm(buf, buf_end, pc + offset);
    } break;

    case DA64_InsnOperandKind_ADDR_SIMPLE:
    case DA64_InsnOperandKind_SIMD_ADDR_SIMPLE: {
        da64_u32 reg_no = da64_bit_range(bits, 5, 5);
        da64_reg reg = da64_get_int_reg(1, reg_no, 0);
        return da64_write_reg(buf, buf_end, reg);
    } break;

    case DA64_InsnOperandKind_SIMD_ADDR_POST: {
        // format_simd_addr_post(f, bits, definition)?;
        *stop = 1;
    } break;

    case DA64_InsnOperandKind_ADDR_REGOFF: {
        da64_u32 option = da64_bit_range(bits, 13, 3);
        da64_u32 size = da64_bit_range(bits, 30, 2);
        da64_u32 shift = da64_bit_set(bits, 12);

        da64_u32 is_64bit = option == 3 || option == 7;

        da64_u32 fp = da64_bit_set(bits, 26);
        da64_u32 scale = size;
        if (fp) {
            scale = (da64_bit_range(bits, 23, 1) << 2) | size;
        }

        da64_u32 rn_reg_no = da64_bit_range(bits, 5, 5);
        da64_reg rn_reg = da64_get_int_reg(1, rn_reg_no, 0);
        da64_u32 rm_reg_no = da64_bit_range(bits, 16, 5);
        da64_reg rm_reg = da64_get_int_reg(is_64bit, rm_reg_no, 1);

        da64_str8 extend = da64_str8_lit("<undefined>");
        switch (option) {
        case 2: {
            extend = da64_str8_lit("uxtw");
        } break;
        case 3: {
            extend = da64_str8_lit("lsl");
        } break;
        case 6: {
            extend = da64_str8_lit("sxtw");
        } break;
        case 7: {
            extend = da64_str8_lit("sxtx");
        } break;
        }

        char* start = buf;
        buf += da64_write_strlit(buf, buf_end, "[");
        buf += da64_write_reg(buf, buf_end, rn_reg);
        buf += da64_write_strlit(buf, buf_end, ", ");
        buf += da64_write_reg(buf, buf_end, rm_reg);
        buf += da64_write_strlit(buf, buf_end, ", ");
        buf += da64_write_str8(buf, buf_end, extend);
        if (shift) {
            buf += da64_write_strlit(buf, buf_end, " #");
            buf += da64_write_imm(buf, buf_end, scale);
        }

        buf += da64_write_strlit(buf, buf_end, "]");
        *stop = 1;
        return buf - start;
    } break;

    case DA64_InsnOperandKind_SVE_ADDR_R:
    case DA64_InsnOperandKind_SVE_ADDR_RR:
    case DA64_InsnOperandKind_SVE_ADDR_RR_LSL1:
    case DA64_InsnOperandKind_SVE_ADDR_RR_LSL2:
    case DA64_InsnOperandKind_SVE_ADDR_RR_LSL3:
    case DA64_InsnOperandKind_SVE_ADDR_RR_LSL4:
    case DA64_InsnOperandKind_SVE_ADDR_RX:
    case DA64_InsnOperandKind_SVE_ADDR_RX_LSL1:
    case DA64_InsnOperandKind_SVE_ADDR_RX_LSL2:
    case DA64_InsnOperandKind_SVE_ADDR_RX_LSL3: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_SVE_ADDR_ZX: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_SVE_ADDR_RZ:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_LSL1:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_LSL2:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_LSL3:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_XTW_14:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_XTW_22:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_XTW1_14:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_XTW1_22:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_XTW2_14:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_XTW2_22:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_XTW3_14:
    case DA64_InsnOperandKind_SVE_ADDR_RZ_XTW3_22: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_ADDR_SIMM7: {
        da64_u32 opc = da64_bit_range(bits, 30, 2);
        if (opc == 3) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }

        da64_u32 fp = da64_bit_set(bits, 26);
        da64_u32 scale = fp ? 2 + opc : 2 + (opc >> 1);

        da64_u32 imm7 = da64_bit_range(bits, 15, 7);
        signed long long imm = (da64_sign_extend(imm7, 6) << scale);

        da64_u32 reg_no = da64_bit_range(bits, 5, 5);
        da64_reg reg = da64_get_int_reg(1, reg_no, 0);

        char* start = buf;
        if (da64_bit_set(bits, 23)) {
            if (da64_bit_set(bits, 24)) {
                buf += da64_write_strlit(buf, buf_end, "[");
                buf += da64_write_reg(buf, buf_end, reg);
                buf += da64_write_strlit(buf, buf_end, ", #");
                buf += da64_write_signed_imm(buf, buf_end, imm);
                buf += da64_write_strlit(buf, buf_end, "]!");
            } else {
                buf += da64_write_strlit(buf, buf_end, "[");
                buf += da64_write_reg(buf, buf_end, reg);
                buf += da64_write_strlit(buf, buf_end, "], #");
                buf += da64_write_signed_imm(buf, buf_end, imm);
                buf += da64_write_strlit(buf, buf_end, "");
            }
        } else if (imm == 0) {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, reg);
            buf += da64_write_strlit(buf, buf_end, "]");
        } else {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, reg);
            buf += da64_write_strlit(buf, buf_end, ", #");
            buf += da64_write_signed_imm(buf, buf_end, imm);
            buf += da64_write_strlit(buf, buf_end, "]");
        }
        return buf - start;
    }

    case DA64_InsnOperandKind_ADDR_SIMM9:
    case DA64_InsnOperandKind_ADDR_SIMM13:
    case DA64_InsnOperandKind_ADDR_OFFSET: {
        da64_u32 imm9 = da64_bit_range(bits, 12, 9);
        da64_u32 scale = kind == DA64_InsnOperandKind_ADDR_SIMM13 ? 4 : 0; // LOG2_TAG_GRANULE

        signed long long imm = (da64_sign_extend(imm9, 8) << scale);

        da64_u32 reg_no = da64_bit_range(bits, 5, 5);
        da64_reg reg = da64_get_int_reg(1, reg_no, 0);

        da64_u32 ldst_offset_only = definition->class == DA64_InsnClass_LDST_UNPRIV
            || definition->class == DA64_InsnClass_LDST_UNSCALED;
        if (ldst_offset_only) {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, reg);
            if (imm != 0) {
                buf += da64_write_strlit(buf, buf_end, ", #");
                buf += da64_write_signed_imm(buf, buf_end, imm);
            }
            buf += da64_write_strlit(buf, buf_end, "]");
        }

        da64_u32 post_index = !da64_bit_set(bits, 11);
        if (!post_index) {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, reg);
            buf += da64_write_strlit(buf, buf_end, ", #");
            buf += da64_write_signed_imm(buf, buf_end, imm);
            buf += da64_write_strlit(buf, buf_end, "]!");
        } else {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, reg);
            buf += da64_write_strlit(buf, buf_end, "], #");
            buf += da64_write_signed_imm(buf, buf_end, imm);
            buf += da64_write_strlit(buf, buf_end, "");
        }
        *stop = 1;
    } break;
    case DA64_InsnOperandKind_ADDR_UIMM12: {
        da64_u32 fp = da64_bit_set(bits, 26);
        da64_u32 size = da64_bit_range(bits, 30, 2);

        da64_u32 scale = size;
        if (fp) {
            scale = (da64_bit_range(bits, 23, 1) << 2) | size;
        }

        da64_u32 uimm12 = da64_bit_range(bits, 10, 12) << scale;
        da64_u32 reg_no = da64_bit_range(bits, 5, 5);
        da64_u32 is_pair = 1;
        da64_u32 with_zr = 0;
        da64_reg reg = da64_get_int_reg(is_pair, reg_no, with_zr);

        char* start = buf;
        buf += da64_write_strlit(buf, buf_end, "[");
        buf += da64_write_reg(buf, buf_end, reg);
        if (uimm12 != 0) {
            buf += da64_write_strlit(buf, buf_end, ", #");
            buf += da64_write_imm(buf, buf_end, uimm12);
        }
        buf += da64_write_strlit(buf, buf_end, "]");
        return buf - start;
    } break;

    case DA64_InsnOperandKind_ADDR_SIMM10: {
        da64_u32 reg_n_no = da64_bit_range(bits, 5, 5);
        da64_reg reg_n = da64_get_int_reg(1, reg_n_no, 0);

        da64_u32 scale = 3;
        da64_u32 pre_index = da64_bit_set(bits, 11);
        da64_u32 s = da64_bit_range(bits, 22, 1);
        da64_u32 imm10 = da64_bit_range(bits, 12, 9) | (s << 9);
        signed long long simm = (da64_sign_extend(imm10, 9) << scale);

        char* start = buf;
        buf += da64_write_strlit(buf, buf_end, "[");
        buf += da64_write_reg(buf, buf_end, reg_n);
        if (simm != 0) {
            buf += da64_write_strlit(buf, buf_end, ", #");
            buf += da64_write_signed_imm(buf, buf_end, simm);
        }
        buf += da64_write_strlit(buf, buf_end, "]");
        if (pre_index) {
            buf += da64_write_strlit(buf, buf_end, "!");
        }
        return buf - start;
    } break;

    case DA64_InsnOperandKind_ADDR_SIMM11: {
        da64_u32 reg_n_no = da64_bit_range(bits, 5, 5);
        da64_reg reg_n = da64_get_int_reg(1, reg_n_no, 0);
        signed long long simm = (da64_sign_extend(da64_bit_range(bits, 15, 7), 6) << 4);

        char* start = buf;
        switch (da64_bit_range(bits, 23, 2)) {
        default: {
            buf += da64_write_strlit(buf, buf_end, "<undefined>");
        } break;
        case 1: {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, reg_n);
            buf += da64_write_strlit(buf, buf_end, "], #");
            buf += da64_write_signed_imm(buf, buf_end, simm);
        } break;
        case 2: {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, reg_n);
            if (simm != 0) {
                buf += da64_write_strlit(buf, buf_end, ", #");
                buf += da64_write_signed_imm(buf, buf_end, simm);
            }
            buf += da64_write_strlit(buf, buf_end, "]");
        } break;
        case 3: {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, reg_n);
            buf += da64_write_strlit(buf, buf_end, ", #");
            buf += da64_write_signed_imm(buf, buf_end, simm);
            buf += da64_write_strlit(buf, buf_end, "]!");
        } break;
        }
        return buf - start;
    } break;

    case DA64_InsnOperandKind_RCPC3_ADDR_OFFSET:
    case DA64_InsnOperandKind_RCPC3_ADDR_OPT_POSTIND:
    case DA64_InsnOperandKind_RCPC3_ADDR_OPT_PREIND_WB:
    case DA64_InsnOperandKind_RCPC3_ADDR_POSTIND:
    case DA64_InsnOperandKind_RCPC3_ADDR_PREIND_WB:
    case DA64_InsnOperandKind_SME_ADDR_RI_U4xVL:
    case DA64_InsnOperandKind_SVE_ADDR_RI_S4x16:
    case DA64_InsnOperandKind_SVE_ADDR_RI_S4x32:
    case DA64_InsnOperandKind_SVE_ADDR_RI_S4xVL:
    case DA64_InsnOperandKind_SVE_ADDR_RI_S4x2xVL:
    case DA64_InsnOperandKind_SVE_ADDR_RI_S4x3xVL:
    case DA64_InsnOperandKind_SVE_ADDR_RI_S4x4xVL:
    case DA64_InsnOperandKind_SVE_ADDR_RI_S6xVL:
    case DA64_InsnOperandKind_SVE_ADDR_RI_S9xVL:
    case DA64_InsnOperandKind_SVE_ADDR_RI_U6:
    case DA64_InsnOperandKind_SVE_ADDR_RI_U6x2:
    case DA64_InsnOperandKind_SVE_ADDR_RI_U6x4:
    case DA64_InsnOperandKind_SVE_ADDR_RI_U6x8: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SVE_ADDR_ZI_U5:
    case DA64_InsnOperandKind_SVE_ADDR_ZI_U5x2:
    case DA64_InsnOperandKind_SVE_ADDR_ZI_U5x4:
    case DA64_InsnOperandKind_SVE_ADDR_ZI_U5x8: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SVE_ADDR_ZZ_LSL:
    case DA64_InsnOperandKind_SVE_ADDR_ZZ_SXTW:
    case DA64_InsnOperandKind_SVE_ADDR_ZZ_UXTW: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_SYSREG:
    case DA64_InsnOperandKind_SYSREG128: {
        da64_u32 op0 = da64_bit_range(bits, 19, 2);
        da64_u32 op1 = da64_bit_range(bits, 16, 3);
        da64_u32 crn = da64_bit_range(bits, 12, 4);
        da64_u32 crm = da64_bit_range(bits, 8, 4);
        da64_u32 op2 = da64_bit_range(bits, 5, 3);

        char* start = buf;

        da64_str8 sys_reg_name = da64_get_sys_reg_name(da64_sys_reg_number(op0, op1, crn, crm, op2));
        if (sys_reg_name.size > 0) {
            buf += da64_write_str8(buf, buf_end, sys_reg_name);
        } else {
            buf += da64_write_strlit(buf, buf_end, "s");
            buf += da64_write_num(buf, buf_end, op0, 10);
            buf += da64_write_strlit(buf, buf_end, "_");
            buf += da64_write_num(buf, buf_end, op1, 10);
            buf += da64_write_strlit(buf, buf_end, "_");
            buf += da64_write_num(buf, buf_end, crn, 10);
            buf += da64_write_strlit(buf, buf_end, "_");
            buf += da64_write_num(buf, buf_end, crm, 10);
            buf += da64_write_strlit(buf, buf_end, "_");
            buf += da64_write_num(buf, buf_end, op2, 10);
        }

        return buf - start;
    } break;

    case DA64_InsnOperandKind_PSTATEFIELD: {
        da64_u32 op2 = da64_bit_range(bits, 5, 3);
        da64_u32 crm = da64_bit_range(bits, 8, 4);
        da64_u32 op1 = da64_bit_range(bits, 16, 3);

        if (op1 == 0) {
            if (op2 == 3) {
                return da64_write_strlit(buf, buf_end, "uao");
            } else if (op2 == 4) {
                return da64_write_strlit(buf, buf_end, "pan");
            } else if (op2 == 5) {
                return da64_write_strlit(buf, buf_end, "spsel");
            }

        } else if (op1 == 1) {
            if (op2 == 0 && (crm & 14) == 0) {
                return da64_write_strlit(buf, buf_end, "allint");
            }
        } else if (op1 == 3) {
            if (op2 == 3 && (crm & 14) == 2) {
                return da64_write_strlit(buf, buf_end, "svcrsm");
            }
            if (op2 == 4 && (crm & 14) == 4) {
                return da64_write_strlit(buf, buf_end, "svcrza");
            }
            if (op2 == 5 && (crm & 14) == 6) {
                return da64_write_strlit(buf, buf_end, "svcrsmza");
            }
            if (op2 == 1) {
                return da64_write_strlit(buf, buf_end, "ssbs");
            }
            if (op2 == 2) {
                return da64_write_strlit(buf, buf_end, "dit");
            }
            if (op2 == 3) {
                return da64_write_strlit(buf, buf_end, "tco");
            }
            if (op2 == 6) {
                return da64_write_strlit(buf, buf_end, "daifset");
            }
            if (op2 == 7) {
                return da64_write_strlit(buf, buf_end, "daifclr");
            }
        }

        char* start = buf;
        buf += da64_write_strlit(buf, buf_end, "s0_");
        buf += da64_write_num(buf, buf_end, op1, 10);
        buf += da64_write_strlit(buf, buf_end, "_c4_");
        buf += da64_write_num(buf, buf_end, crm, 10);
        buf += da64_write_strlit(buf, buf_end, "_");
        buf += da64_write_num(buf, buf_end, op2, 10);
        return buf - start;
    } break;

    case DA64_InsnOperandKind_SYSREG_AT:
    case DA64_InsnOperandKind_SYSREG_DC:
    case DA64_InsnOperandKind_SYSREG_IC:
    case DA64_InsnOperandKind_SYSREG_TLBI:
    case DA64_InsnOperandKind_SYSREG_TLBIP:
    case DA64_InsnOperandKind_SYSREG_SR: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_BARRIER: {
        da64_u32 barrier = da64_bit_range(bits, 8, 4);
        switch (barrier) {
        default: {
            char* start = buf;
            buf += da64_write_strlit(buf, buf_end, "#");
            buf += da64_write_imm(buf, buf_end, barrier);
            return buf - start;
        } break;
        case 1: {
            return da64_write_strlit(buf, buf_end, "oshld");
        } break;
        case 2: {
            return da64_write_strlit(buf, buf_end, "oshst");
        } break;
        case 3: {
            return da64_write_strlit(buf, buf_end, "osh");
        } break;
        case 5: {
            return da64_write_strlit(buf, buf_end, "nshld");
        } break;
        case 6: {
            return da64_write_strlit(buf, buf_end, "nshst");
        } break;
        case 7: {
            return da64_write_strlit(buf, buf_end, "nsh");
        } break;
        case 9: {
            return da64_write_strlit(buf, buf_end, "ishld");
        } break;
        case 10: {
            return da64_write_strlit(buf, buf_end, "ishst");
        } break;
        case 11: {
            return da64_write_strlit(buf, buf_end, "ish");
        } break;
        case 13: {
            return da64_write_strlit(buf, buf_end, "ld");
        } break;
        case 14: {
            return da64_write_strlit(buf, buf_end, "st");
        } break;
        case 15: {
            return da64_write_strlit(buf, buf_end, "sy");
        } break;
        }
    } break;

    case DA64_InsnOperandKind_BARRIER_DSB_NXS: {
        da64_u32 barrier = da64_bit_range(bits, 8, 4);
        switch (barrier) {
        default: {
            char* start = buf;
            buf += da64_write_strlit(buf, buf_end, "#");
            buf += da64_write_imm(buf, buf_end, barrier);
            return buf - start;
        } break;
        case 2: {
            return da64_write_strlit(buf, buf_end, "oshnxs");
        }
        case 6: {
            return da64_write_strlit(buf, buf_end, "nshnxs");
        }
        case 10: {
            return da64_write_strlit(buf, buf_end, "ishnxs");
        }
        case 14: {
            return da64_write_strlit(buf, buf_end, "synxs");
        }
        }
    } break;

    case DA64_InsnOperandKind_PRFOP: {

        da64_u32 typ_bits = da64_bit_range(bits, 3, 2);
        da64_str8 typ = da64_str8_lit("");
        if (typ_bits == 0) {
            typ = da64_str8_lit("pld");
        } else if (typ_bits == 1) {
            typ = da64_str8_lit("pli");
        } else if (typ_bits == 2) {
            typ = da64_str8_lit("pst");
        }

        da64_u32 target_bits = da64_bit_range(bits, 1, 2);
        da64_str8 target = da64_str8_lit("");
        if (target_bits == 0) {
            target = da64_str8_lit("pld");
        } else if (target_bits == 1) {
            target = da64_str8_lit("pli");
        } else if (target_bits == 2) {
            target = da64_str8_lit("pst");
        }

        char* start = buf;
        if (typ.size > 0 && target.size > 0) {
            da64_str8 policy = da64_bit_set(bits, 0) ? da64_str8_lit("strm") : da64_str8_lit("keep");
            buf += da64_write_str8(buf, buf_end, typ);
            buf += da64_write_str8(buf, buf_end, target);
            buf += da64_write_str8(buf, buf_end, policy);
        } else {
            buf += da64_write_strlit(buf, buf_end, "#");
            buf += da64_write_imm(buf, buf_end, da64_bit_range(bits, 0, 5));
        }
        return buf - start;
    } break;
    case DA64_InsnOperandKind_RPRFMOP: {
        da64_u32 b12 = da64_bit_range(bits, 12, 1) << 3;
        da64_u32 b13 = da64_bit_range(bits, 13, 1) << 4;
        da64_u32 b15 = da64_bit_range(bits, 15, 1) << 5;
        da64_u32 rprfmop = da64_bit_range(bits, 0, 3) | b12 | b13 | b15;

        char* start = buf;
        switch (rprfmop) {
        default: {
            buf += da64_write_strlit(buf, buf_end, "#");
            buf += da64_write_imm(buf, buf_end, rprfmop);
        } break;
        case 0: {
            buf += da64_write_strlit(buf, buf_end, "pldkeep");
        } break;
        case 1: {
            buf += da64_write_strlit(buf, buf_end, "pstkeep");
        } break;
        case 4: {
            buf += da64_write_strlit(buf, buf_end, "pldstrm");
        } break;
        case 5: {
            buf += da64_write_strlit(buf, buf_end, "pststrm");
        } break;
        }
        return buf - start;
    } break;

    case DA64_InsnOperandKind_BARRIER_PSB: {
        return da64_write_op_kind(buf, buf_end, kind);
    } break;

    case DA64_InsnOperandKind_X16: {
        return da64_write_strlit(buf, buf_end, "x16");
    }

    case DA64_InsnOperandKind_SME_ZT0: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_SME_ZT0_INDEX: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_SME_ZT0_LIST: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_BARRIER_GCSB: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_BTI_TARGET: {
        return da64_write_op_kind(buf, buf_end, kind);
    }

    case DA64_InsnOperandKind_MOPS_ADDR_Rd:
    case DA64_InsnOperandKind_MOPS_ADDR_Rs:
    case DA64_InsnOperandKind_MOPS_WB_Rn: {
        *stop = 1;

        da64_u32 rd = da64_bit_range(bits, 0, 5);
        da64_u32 rn = da64_bit_range(bits, 5, 5);
        da64_u32 rs = da64_bit_range(bits, 16, 5);
        da64_u32 op1 = da64_bit_range(bits, 22, 2);

        if ((rd == rn) || (rd == rs) || (rs == rn)) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }
        if ((rd == 31) || (rn == 31) || (rs == 31 && op1 != 0b11)) {
            return da64_write_strlit(buf, buf_end, "<undefined>");
        }

        da64_reg rd_s = da64_get_int_reg(1, rd, 1);
        da64_reg rn_s = da64_get_int_reg(1, rn, 1);
        da64_reg rs_s = da64_get_int_reg(1, rs, 1);

        char* start = buf;
        if (op1 != 3) {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, rd_s);
            buf += da64_write_strlit(buf, buf_end, "]!, [");
            buf += da64_write_reg(buf, buf_end, rs_s);
            buf += da64_write_strlit(buf, buf_end, "]!, ");
            buf += da64_write_reg(buf, buf_end, rn_s);
            buf += da64_write_strlit(buf, buf_end, "!");
        } else {
            buf += da64_write_strlit(buf, buf_end, "[");
            buf += da64_write_reg(buf, buf_end, rd_s);
            buf += da64_write_strlit(buf, buf_end, "]!, ");
            buf += da64_write_reg(buf, buf_end, rn_s);
            buf += da64_write_strlit(buf, buf_end, "!, ");
            buf += da64_write_reg(buf, buf_end, rs_s);
        }
        return buf - start;
    } break;
    }

    return 0;
}

int da64_format_hint(da64_u64 bits, char* buf, char* buf_end)
{
    da64_u32 hint = da64_bit_range(bits, 5, 7);

    switch (hint) {
    case 0: {
        return da64_write_strlit(buf, buf_end, "nop");
    } break;
    case 1: {
        return da64_write_strlit(buf, buf_end, "yield");
    } break;
    case 2: {
        return da64_write_strlit(buf, buf_end, "wfe");
    } break;
    case 3: {
        return da64_write_strlit(buf, buf_end, "wfi");
    } break;
    case 4: {
        return da64_write_strlit(buf, buf_end, "sev");
    } break;
    case 5: {
        return da64_write_strlit(buf, buf_end, "sevl");
    } break;
    case 6: {
        return da64_write_strlit(buf, buf_end, "dgh");
    } break;
    case 7: {
        return da64_write_strlit(buf, buf_end, "xpaclri");
    } break;
    case 8: {
        return da64_write_strlit(buf, buf_end, "pacia1716");
    } break;
    case 10: {
        return da64_write_strlit(buf, buf_end, "pacib1716");
    } break;
    case 12: {
        return da64_write_strlit(buf, buf_end, "autia1716");
    } break;
    case 14: {
        return da64_write_strlit(buf, buf_end, "autib1716");
    } break;
    case 16: {
        return da64_write_strlit(buf, buf_end, "esb");
    } break;
    case 17: {
        return da64_write_strlit(buf, buf_end, "psb\t\tcsync");
    } break;
    case 18: {
        return da64_write_strlit(buf, buf_end, "tsb\t\tcsync");
    } break;
    case 20: {
        return da64_write_strlit(buf, buf_end, "csdb");
    } break;
    case 22: {
        return da64_write_strlit(buf, buf_end, "clrbhb");
    } break;
    case 19: {
        return da64_write_strlit(buf, buf_end, "gcsb\t\tdsync");
    } break;
    case 24: {
        return da64_write_strlit(buf, buf_end, "paciaz");
    } break;
    case 25: {
        return da64_write_strlit(buf, buf_end, "paciasp");
    } break;
    case 26: {
        return da64_write_strlit(buf, buf_end, "pacibz");
    } break;
    case 27: {
        return da64_write_strlit(buf, buf_end, "pacibsp");
    } break;
    case 28: {
        return da64_write_strlit(buf, buf_end, "autiaz");
    } break;
    case 29: {
        return da64_write_strlit(buf, buf_end, "autiasp");
    } break;
    case 30: {
        return da64_write_strlit(buf, buf_end, "autibz");
    } break;
    case 31: {
        return da64_write_strlit(buf, buf_end, "autibsp");
    } break;
    case 32: {
        return da64_write_strlit(buf, buf_end, "bti");
    } break;
    case 34: {
        return da64_write_strlit(buf, buf_end, "bti\t\tc");
    } break;
    case 36: {
        return da64_write_strlit(buf, buf_end, "bti\t\tj");
    } break;
    case 38: {
        return da64_write_strlit(buf, buf_end, "bti\t\tjc");
    } break;
    }

    char* start = buf;
    buf += da64_write_strlit(buf, buf_end, "hint\t\t");
    buf += da64_write_imm(buf, buf_end, hint);
    return buf - start;
}

////////////////
//~ yuraiz: Main function

// Format an instruction to a string. It does not use the aliases yet
// and always emits the primary mnemonic.
// The program counter is useful for the PC-relative addressing to
// emit the target address in the disassembly rather than the offset.
int da64_fmt_insn_pc(da64_u64 pc, DA64_Opcode opcode, char* buf, da64_u64 buf_len)
{
    char* buf_end = buf + buf_len;

    const DA64_Insn* definition = opcode.definition;
    da64_u32 bits = opcode.bits;

    if (definition->class == DA64_InsnClass_IC_SYSTEM) {
        if (definition->operand_count > 0) {
            DA64_InsnOperand op = definition->operands[0];
            if (op.kind == DA64_InsnOperandKind_UIMM7) {
                return da64_format_hint(bits, buf, buf_end);
            }
        }
    }

    buf += da64_memcpy(buf, buf_end, definition->mnemonic, definition->mnemonic_size);

    if ((definition->flags & DA64_InsnFlag_IS_COND) != 0) {
        da64_u32 cond = da64_bit_range(bits, 0, 4);
        da64_str8 name = da64_cond_name(cond);
        buf += da64_memcpy(buf, buf_end, name.data, name.size);
    }

    buf += da64_write_strlit(buf, buf_end, " ");

    da64_u64 op_count = definition->operand_count;
    for (da64_u64 i = 0; i < op_count; i++) {
        da64_u32 stop = 0;
        DA64_InsnOperand operand = definition->operands[i];
        buf += da64_format_operand(i, pc, bits, &operand, definition, &stop, buf, buf_end);
        if (!stop && (i + 1 < op_count)) {
            buf += da64_write_strlit(buf, buf_end, ", ");
        }
        if (stop) {
            break;
        }
    }

    buf[0] = 0;

    return 0;
}

#endif // DA64_FORMAT_H
