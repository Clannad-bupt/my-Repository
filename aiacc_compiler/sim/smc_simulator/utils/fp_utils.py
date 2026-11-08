
import struct

def fp32_add_hw(bits_a: int, bits_b: int, sub: bool = False) -> int:
    """
    Implements FP32 addition/subtraction according to hardware rules:
    1. Flush denormals to signed zero.
    2. Inf/NaN rules.
    3. Signed zero interaction: -0 + +0 = +0, etc.
    4. Post-flush denormal results to signed zero.
    """
    # Extract fields
    sa = (bits_a >> 31) & 1
    ea = (bits_a >> 23) & 0xFF
    fa = bits_a & 0x7FFFFF
    
    sb = (bits_b >> 31) & 1
    eb = (bits_b >> 23) & 0xFF
    fb = bits_b & 0x7FFFFF
    
    # Subtraction mode: flip sign of B
    if sub:
        sb ^= 1
    
    # NaN Check
    is_nan_a = (ea == 255 and fa != 0)
    is_nan_b = (eb == 255 and fb != 0)
    
    if is_nan_a or is_nan_b:
        # Return a NaN. If one is NaN, we could return that one.
        # Hardware might return a preferred NaN 0x7FC00000.
        if is_nan_a: return bits_a | 0x00400000 # Ensure it's a QNaN or just return as is
        return bits_b | 0x00400000

    # Flush denormals to zero (preserving sign)
    if ea == 0: fa = 0
    if eb == 0: fb = 0
    
    bits_a_flushed = (sa << 31) | (ea << 23) | fa
    bits_b_flushed = (sb << 31) | (eb << 23) | fb
    
    # Convert to Python floats for addition
    f_a = struct.unpack('<f', struct.pack('<I', bits_a_flushed))[0]
    f_b = struct.unpack('<f', struct.pack('<I', bits_b_flushed))[0]
    
    res_f = f_a + f_b
    
    # Special case: (+0) + (-0) or (-0) + (+0) must be +0 in this hardware.
    # Python's +0.0 + -0.0 is +0.0, so it matches.
    # -0.0 + -0.0 is -0.0. Matches.
    
    # Pack result
    try:
        res_bits = struct.unpack('<I', struct.pack('<f', res_f))[0]
    except OverflowError:
        # Handle overflow to Inf
        res_bits = (0x7F800000 | (0x80000000 if res_f < 0 else 0))
    
    # Check for NaN result from Inf - Inf
    res_e = (res_bits >> 23) & 0xFF
    res_f_field = res_bits & 0x7FFFFF
    
    if res_e == 255 and res_f_field != 0:
        # Inf - Inf case, return default NaN
        return 0x7FC00000
    
    # Flush denormal result to zero (FTZ)
    if res_e == 0 and res_f_field != 0:
        res_bits = (res_bits & 0x80000000) # Keep sign, flush mantissa
        
    return res_bits

def fp16_add_hw(bits_a: int, bits_b: int, sub: bool = False) -> int:
    """
    Implements FP16 addition/subtraction. 
    Following similar FTZ rules as FP32.
    """
    sa = (bits_a >> 15) & 1
    ea = (bits_a >> 10) & 0x1F
    fa = bits_a & 0x3FF
    
    sb = (bits_b >> 15) & 1
    eb = (bits_b >> 10) & 0x1F
    fb = bits_b & 0x3FF
    
    if sub:
        sb ^= 1
        
    is_nan_a = (ea == 0x1F and fa != 0)
    is_nan_b = (eb == 0x1F and fb != 0)
    
    if is_nan_a or is_nan_b:
        return 0x7E00 # Default NaN for FP16
    
    # Flush denormals
    if ea == 0: fa = 0
    if eb == 0: fb = 0
    
    bits_a_flushed = (sa << 15) | (ea << 10) | fa
    bits_b_flushed = (sb << 15) | (eb << 10) | fb
    
    # Use struct to convert from FP16 bits to Python float
    f_a = struct.unpack('<e', struct.pack('<H', bits_a_flushed))[0]
    f_b = struct.unpack('<e', struct.pack('<H', bits_b_flushed))[0]
    
    res_f = f_a + f_b
    
    # Pack back to FP16
    try:
        res_h = struct.unpack('<H', struct.pack('<e', res_f))[0]
    except OverflowError:
        # Handle overflow to Inf
        res_h = (0x7C00 | (0x8000 if res_f < 0 else 0))

    # Flush denormal result
    res_e = (res_h >> 10) & 0x1F
    res_f_field = res_h & 0x3FF
    if res_e == 0 and res_f_field != 0:
        res_h = (res_h & 0x8000)
        
    return res_h

def fp32_mul_hw(bits_a: int, bits_b: int) -> int:
    """
    Implements FP32 multiplication according to Table 5.15.
    FTZ (Flush To Zero) applied to inputs and outputs.
    """
    sa = (bits_a >> 31) & 1
    ea = (bits_a >> 23) & 0xFF
    fa = bits_a & 0x7FFFFF
    
    sb = (bits_b >> 31) & 1
    eb = (bits_b >> 23) & 0xFF
    fb = bits_b & 0x7FFFFF
    
    is_nan_a = (ea == 255 and fa != 0)
    is_nan_b = (eb == 255 and fb != 0)
    if is_nan_a or is_nan_b:
        return 0x7FC00000

    # Flush denormals to zero
    if ea == 0: fa = 0
    if eb == 0: fb = 0
    
    # 0 * Inf = NaN
    is_zero_a = (ea == 0 and fa == 0)
    is_zero_b = (eb == 0 and fb == 0)
    is_inf_a = (ea == 255 and fa == 0)
    is_inf_b = (eb == 255 and fb == 0)
    
    if (is_zero_a and is_inf_b) or (is_inf_a and is_zero_b):
        return 0x7FC00000
    
    bits_a_flushed = (sa << 31) | (ea << 23) | fa
    bits_b_flushed = (sb << 31) | (eb << 23) | fb
    
    f_a = struct.unpack('<f', struct.pack('<I', bits_a_flushed))[0]
    f_b = struct.unpack('<f', struct.pack('<I', bits_b_flushed))[0]
    
    res_f = f_a * f_b
    try:
        res_bits = struct.unpack('<I', struct.pack('<f', res_f))[0]
    except OverflowError:
        res_bits = (0x7F800000 | (0x80000000 if res_f < 0 else 0))
    
    # Pack back and flush denorm result
    res_e = (res_bits >> 23) & 0xFF
    res_f_field = res_bits & 0x7FFFFF
    if res_e == 0 and res_f_field != 0:
        res_bits = (res_bits & 0x80000000)
        
    return res_bits

def fp32_div_hw(bits_a: int, bits_b: int) -> int:
    """
    Implements FP32 division according to Table 5.17.
    """
    sa = (bits_a >> 31) & 1
    ea = (bits_a >> 23) & 0xFF
    fa = bits_a & 0x7FFFFF
    
    sb = (bits_b >> 31) & 1
    eb = (bits_b >> 23) & 0xFF
    fb = bits_b & 0x7FFFFF
    
    is_nan_a = (ea == 255 and fa != 0)
    is_nan_b = (eb == 255 and fb != 0)
    if is_nan_a or is_nan_b:
        return 0x7FC00000

    # Flush denormals
    if ea == 0: fa = 0
    if eb == 0: fb = 0
    
    is_zero_a = (ea == 0 and fa == 0)
    is_zero_b = (eb == 0 and fb == 0)
    is_inf_a = (ea == 255 and fa == 0)
    is_inf_b = (eb == 255 and fb == 0)
    
    # Special cases from Table 5.17:
    # 0 / 0 = NaN
    if is_zero_a and is_zero_b:
        return 0x7FC00000
    # Inf / Inf = NaN
    if is_inf_a and is_inf_b:
        return 0x7FC00000
    # a / 0 = Inf (sign follows)
    if is_zero_b:
        return ((sa ^ sb) << 31) | 0x7F800000
    # a / Inf = 0 (sign follows)
    if is_inf_b:
        return ((sa ^ sb) << 31)
        
    bits_a_flushed = (sa << 31) | (ea << 23) | fa
    bits_b_flushed = (sb << 31) | (eb << 23) | fb
    
    f_a = struct.unpack('<f', struct.pack('<I', bits_a_flushed))[0]
    f_b = struct.unpack('<f', struct.pack('<I', bits_b_flushed))[0]
    
    try:
        res_f = f_a / f_b
    except ZeroDivisionError:
        return ((sa ^ sb) << 31) | 0x7F800000
    
    try:
        res_bits = struct.unpack('<I', struct.pack('<f', res_f))[0]
    except OverflowError:
        res_bits = (0x7F800000 | (0x80000000 if res_f < 0 else 0))
    
    # Flush denorm result
    res_e = (res_bits >> 23) & 0xFF
    res_f_field = res_bits & 0x7FFFFF
    if res_e == 0 and res_f_field != 0:
        res_bits = (res_bits & 0x80000000)
        
    return res_bits

def fp16_mul_hw(bits_a: int, bits_b: int) -> int:
    """Implement FP16 multiplication with FTZ."""
    sa = (bits_a >> 15) & 1
    ea = (bits_a >> 10) & 0x1F
    fa = bits_a & 0x3FF
    
    sb = (bits_b >> 15) & 1
    eb = (bits_b >> 10) & 0x1F
    fb = bits_b & 0x3FF
    
    if (ea == 0x1F and fa != 0) or (eb == 0x1F and fb != 0):
        return 0x7E00
    
    # FTZ
    if ea == 0: fa = 0
    if eb == 0: fb = 0
    
    # 0 * Inf = NaN
    if (ea == 0 and eb == 0x1F) or (ea == 0x1F and eb == 0):
        return 0x7E00
        
    f_a = struct.unpack('<e', struct.pack('<H', (sa<<15)|(ea<<10)|fa))[0]
    f_b = struct.unpack('<e', struct.pack('<H', (sb<<15)|(eb<<10)|fb))[0]
    
    res_f = f_a * f_b
    try:
        res_h = struct.unpack('<H', struct.pack('<e', res_f))[0]
    except OverflowError:
        res_h = ((sa ^ sb) << 15) | 0x7C00

    # Flush denormal result
    res_e = (res_h >> 10) & 0x1F
    res_f_field = res_h & 0x3FF
    if res_e == 0 and res_f_field != 0:
        res_h = (res_h & 0x8000)
        
    return res_h
