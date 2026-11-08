import sympy as sp

# ----- 暴露给算子开发工程师的底层硬件保护包裹接口 -----

def hw_shiftdn(val, shift):
    """强制使用底层 SHIFTDN (移位下降) 硬件，避开 sympy 代数化简"""
    return sp.Function('SHIFTDN')(val, shift)

def hw_shiftup(val, shift):
    """强制使用底层 SHIFTUP (移位上升) 硬件"""
    return sp.Function('SHIFTUP')(val, shift)

def hw_int8_to_fp16(val):
    """强制使用底层的量化还原硬件单元 INT8TOFP16"""
    return sp.Function('INT8TOFP16')(val)

def hw_fptoint(val):
    """强制使用硬件浮点转整数单元 FPTOINT"""
    return sp.Function('FPTOINT')(val)
    
def hw_logic(cond):
    """硬件级流控与真值触发单元"""
    return sp.Function('LOGIC')(cond)
