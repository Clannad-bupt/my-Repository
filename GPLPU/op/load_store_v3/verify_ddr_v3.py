#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verify load_store_v3 DDR writeback against golden and an independent reference."""

import json
import os
import struct
import sys

import numpy as np


def read_hex_lines(path):
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return [ln.strip().lower() for ln in f if ln.strip()]


def line_to_mem(line):
    # write_hex_file emits byte[15]..byte[0], so reverse to memory order.
    return bytes.fromhex(line)[::-1]


def mem_to_line(mem):
    return bytes(mem)[::-1].hex()


def lanes_u32(line):
    return struct.unpack("<4I", line_to_mem(line))


def lanes_f32(line):
    return struct.unpack("<4f", line_to_mem(line))


def lanes_f16(line):
    return np.frombuffer(line_to_mem(line), dtype="<f2").copy()


def lanes_u16(line):
    return np.frombuffer(line_to_mem(line), dtype="<u2").copy()


def line_from_u32(l0, l1, l2, l3):
    return mem_to_line(struct.pack("<4I", l0, l1, l2, l3))


def line_from_f32(vals):
    return mem_to_line(struct.pack("<4f", *vals))


def line_from_f16(vals):
    return mem_to_line(np.asarray(vals, dtype="<f2").tobytes())


def ftz_f32(arr):
    bits = arr.astype(np.float32).view(np.uint32).copy()
    denorm = (bits & np.uint32(0x7F800000)) == 0
    bits[denorm] &= np.uint32(0x80000000)
    return bits.view(np.float32)


def ftz_f16_bits(bits):
    out = np.asarray(bits, dtype="<u2").copy()
    denorm = (out & np.uint16(0x7C00)) == 0
    out[denorm] &= np.uint16(0x8000)
    return out


def ftz_f16(arr):
    bits = np.asarray(arr, dtype="<f2").view("<u2")
    return ftz_f16_bits(bits).view("<f2")


def fp16_ordered(bits):
    bits = int(bits) & 0xFFFF
    return (0x8000 - bits) if (bits & 0x8000) else (bits + 0x8000)


def fp16_lane_match(a_bits, b_bits, ulp_tol):
    a_bits = int(a_bits) & 0xFFFF
    b_bits = int(b_bits) & 0xFFFF
    if a_bits == b_bits:
        return True

    exp_a, exp_b = a_bits & 0x7C00, b_bits & 0x7C00
    frac_a, frac_b = a_bits & 0x03FF, b_bits & 0x03FF
    sign_a, sign_b = a_bits & 0x8000, b_bits & 0x8000

    nan_a = exp_a == 0x7C00 and frac_a != 0
    nan_b = exp_b == 0x7C00 and frac_b != 0
    if nan_a or nan_b:
        return nan_a and nan_b

    inf_a = exp_a == 0x7C00 and frac_a == 0
    inf_b = exp_b == 0x7C00 and frac_b == 0
    if inf_a or inf_b:
        return inf_a and inf_b and sign_a == sign_b

    zero_a = (a_bits & 0x7FFF) == 0
    zero_b = (b_bits & 0x7FFF) == 0
    if zero_a or zero_b:
        return zero_a and zero_b

    return abs(fp16_ordered(a_bits) - fp16_ordered(b_bits)) <= int(ulp_tol)


def line_match(got, exp, op, dtype, fp16_ulp_tol):
    if got == exp:
        return True
    if op.lower() == "sub" and dtype.lower() in ("fp16", "float16"):
        try:
            got_lanes = lanes_u16(got)
            exp_lanes = lanes_u16(exp)
        except ValueError:
            return False
        return all(fp16_lane_match(g, e, fp16_ulp_tol)
                   for g, e in zip(got_lanes, exp_lanes))
    return False


def first_mismatch(got_lines, exp_lines, op, dtype, fp16_ulp_tol):
    for i, (got, exp) in enumerate(zip(got_lines, exp_lines)):
        if not line_match(got, exp, op, dtype, fp16_ulp_tol):
            return i
    return -1


def numpy_reference(op, inputs_lines, out_words, dtype):
    name = op.lower()
    dtype = dtype.lower()

    if name == "mul":
        inp = inputs_lines["input"]
        oth = inputs_lines["other"]
        ref = []
        for w in range(out_words):
            a = np.array(lanes_f32(inp[w]), dtype=np.float32)
            b = np.array(lanes_f32(oth[w]), dtype=np.float32)
            r = ftz_f32((a * b).astype(np.float32))
            bits = r.view(np.uint32)
            ref.append(line_from_u32(int(bits[0]), int(bits[1]), int(bits[2]), int(bits[3])))
        return ref

    if name in ("bitwise_and", "bitwiseand"):
        inp = inputs_lines["input"]
        oth = inputs_lines["other"]
        scalar = lanes_u32(oth[0])
        ref = []
        for w in range(out_words):
            il = lanes_u32(inp[w])
            ref.append(line_from_u32(*[il[j] & scalar[j] for j in range(4)]))
        return ref

    if name == "sub":
        inp = inputs_lines["input"]
        oth = inputs_lines["other"]
        alp = inputs_lines["alpha"]
        ref = []
        if dtype in ("fp16", "float16"):
            alpha = lanes_f16(alp[0])
            for w in range(out_words):
                a = lanes_f16(inp[w])
                b = lanes_f16(oth[w])
                mul = (alpha * b).astype(np.float16)
                r = (a - mul).astype(np.float16)
                ref.append(line_from_f16(r))
            return ref

        alpha = np.array(lanes_f32(alp[0]), dtype=np.float32)
        for w in range(out_words):
            a = np.array(lanes_f32(inp[w]), dtype=np.float32)
            b = np.array(lanes_f32(oth[w]), dtype=np.float32)
            mul = ftz_f32((alpha * b).astype(np.float32))
            r = (a - mul).astype(np.float32)
            ref.append(line_from_f32(r))
        return ref

    return None


def main():
    if len(sys.argv) < 2:
        print("Usage: verify_ddr_v3.py <case_abs_dir>")
        return 2
    case = sys.argv[1]

    meta_path = os.path.join(case, "meta_v3.json")
    if not os.path.exists(meta_path):
        print("ERROR: missing meta_v3.json: %s" % meta_path)
        return 2
    with open(meta_path) as f:
        meta = json.load(f)

    op = meta["op"]
    dtype = meta.get("dtype", "fp32")
    num_smc = meta["num_smc"]
    out_bank = meta["out_bank"]
    out_words = meta["out_words"]
    word_bytes = meta["word_bytes"]
    store_line = meta["store_byte_base"] // word_bytes
    meta_inputs = meta.get("inputs", [])
    use_external_golden = os.environ.get("SUB_USE_LLAMA_RETURN_GOLDEN", "") not in ("", "0")
    has_ref = (not use_external_golden) and op.lower() in ("mul", "bitwise_and", "bitwiseand", "sub") and meta_inputs
    fp16_ulp_tol = int(os.environ.get("V3_FP16_ULP_TOL", "0"))

    npass, nfail = 0, 0
    ref_pass, ref_fail = 0, 0
    for smc in range(num_smc):
        smc_dir = os.path.join(case, "smc_%d" % smc)
        golden = read_hex_lines(os.path.join(smc_dir, "output_bank", "bank%d_final.hex" % out_bank))
        hbm = read_hex_lines(os.path.join(smc_dir, "hbm_result_smc_%d.hex" % smc))

        if golden is None:
            print("  smc_%d: FAIL  missing golden(output_bank/bank%d_final.hex)" % (smc, out_bank))
            nfail += 1
            continue
        if hbm is None:
            print("  smc_%d: FAIL  missing DDR dump(hbm_result_smc_%d.hex)" % (smc, smc))
            nfail += 1
            continue

        gold_out = golden[:out_words]
        ddr_out = hbm[store_line:store_line + out_words]

        if len(ddr_out) < out_words:
            print("  smc_%d: FAIL  short DDR dump need=%d got=%d @line=%d"
                  % (smc, out_words, len(ddr_out), store_line))
            nfail += 1
            continue

        exact_bad = next((i for i in range(out_words) if ddr_out[i] != gold_out[i]), -1)
        bad = first_mismatch(ddr_out, gold_out, op, dtype, fp16_ulp_tol)
        if bad < 0:
            mode = "exact" if exact_bad < 0 else "fp16_ulp<=%d" % fp16_ulp_tol
            print("  smc_%d: PASS  DDR==golden (%d words @line %d, %s)"
                  % (smc, out_words, store_line, mode))
            npass += 1
        else:
            print("  smc_%d: FAIL  DDR!=golden @word %d (golden=%s ddr=%s)"
                  % (smc, bad, gold_out[bad], ddr_out[bad]))
            nfail += 1

        if has_ref:
            inputs_lines = {}
            ok_inputs = True
            for ip in meta_inputs:
                base_line = ip["byte_base"] // word_bytes
                words = ip.get("words", out_words)
                seg = hbm[base_line:base_line + words]
                if len(seg) < words:
                    ok_inputs = False
                    break
                inputs_lines[ip["name"]] = seg
            if not ok_inputs:
                print("    [ref] smc_%d: SKIP short input segment in DDR dump" % smc)
                ref_fail += 1
                continue

            ref = numpy_reference(op, inputs_lines, out_words, dtype)
            rb_g, rb_d = -1, -1
            rb_g = first_mismatch(ref, gold_out, op, dtype, fp16_ulp_tol)
            rb_d = first_mismatch(ref, ddr_out, op, dtype, fp16_ulp_tol)
            if rb_g < 0 and rb_d < 0:
                print("    [ref] smc_%d: PASS  numpy == golden == DDR" % smc)
                ref_pass += 1
            else:
                if rb_g >= 0:
                    print("    [ref] smc_%d: FAIL  numpy!=golden @word %d (ref=%s golden=%s)"
                          % (smc, rb_g, ref[rb_g], gold_out[rb_g]))
                if rb_d >= 0:
                    print("    [ref] smc_%d: FAIL  numpy!=DDR @word %d (ref=%s ddr=%s)"
                          % (smc, rb_d, ref[rb_d], ddr_out[rb_d]))
                ref_fail += 1

    print("V3-DDR-VERIFY Summary: pass=%d fail=%d  (op=%s, dtype=%s, store@line %d, %d words)"
          % (npass, nfail, op, dtype, store_line, out_words))
    if has_ref:
        print("V3-REF-VERIFY Summary: pass=%d fail=%d" % (ref_pass, ref_fail))
    ok = (nfail == 0) and (not has_ref or ref_fail == 0)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
