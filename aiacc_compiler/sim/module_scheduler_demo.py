import z3
import time

def build_solver(II, test_case="non_monotonic"):
    solver = z3.Solver()
    
    t_A = z3.Int('t_A')
    t_B = z3.Int('t_B')
    t_C = z3.Int('t_C')
    solver.add(t_A >= 0, t_B >= 0, t_C >= 0)
    
    s_A, k_A = z3.Int('s_A'), z3.Int('k_A')
    s_B, k_B = z3.Int('s_B'), z3.Int('k_B')
    s_C, k_C = z3.Int('s_C'), z3.Int('k_C')
    
    for t, s, k in [(t_A, s_A, k_A), (t_B, s_B, k_B), (t_C, s_C, k_C)]:
        solver.add(t == k * II + s)
        solver.add(s >= 0, s < II)
        solver.add(k >= 0)
        
    solver.add(t_B >= t_A + 2)
    solver.add(t_C >= t_B + 2)
    solver.add(z3.Distinct(s_A, s_B, s_C))
    
    if test_case == "non_monotonic":
        solver.add(t_C == t_A + 5)
    return solver, {"A": t_A, "B": t_B, "C": t_C}

def hybrid_binary_search_ii(MII, MAX_II):
    low = MII
    high = MAX_II
    best_ii = None
    
    print(f"--- Hybrid Binary Search (Range: {low} - {high}) ---")
    while low <= high:
        mid = (low + high) // 2
        print(f"Testing II = {mid} ...", end=" ")
        
        solver, _ = build_solver(mid)
        if solver.check() == z3.sat:
            print("SAT")
            best_ii = mid
            high = mid - 1
        else:
            print("UNSAT (assuming macroscopic monotonicity, move up)")
            low = mid + 1
            
    if best_ii is not None and best_ii > MII:
        print(f"--- Local Fallback (probing down from {best_ii - 1}) ---")
        fallback_limit = max(MII, best_ii - 3)
        # 移除 break，强制下探到 limit，防止被单次 UNSAT 阻挡
        for fallback_ii in range(best_ii - 1, fallback_limit - 1, -1):
            print(f"Testing II = {fallback_ii} ...", end=" ")
            solver, _ = build_solver(fallback_ii)
            if solver.check() == z3.sat:
                print("SAT (Unexpected optimal found!)")
                best_ii = fallback_ii
            else:
                print("UNSAT")
                # 不 break，继续探索
    return best_ii

def pure_binary_search_ii(MII, MAX_II):
    low = MII
    high = MAX_II
    best_ii = None
    
    print(f"\n--- Pure Binary Search ---")
    while low <= high:
        mid = (low + high) // 2
        print(f"Testing II = {mid} ...", end=" ")
        
        solver, _ = build_solver(mid)
        if solver.check() == z3.sat:
            print("SAT")
            best_ii = mid
            high = mid - 1
        else:
            print("UNSAT")
            low = mid + 1
    return best_ii

if __name__ == "__main__":
    MII = 3
    MAX_II = 7
    
    print("Full Solution Space:")
    for i in range(MII, MAX_II+1):
        sol, _ = build_solver(i)
        print(f"II={i}: {'SAT' if sol.check() == z3.sat else 'UNSAT'}")
    
    pure_best = pure_binary_search_ii(MII, MAX_II)
    hybrid_best = hybrid_binary_search_ii(MII, MAX_II)
    
    print("="*50)
    print(f"Pure Binary Search Best II: {pure_best}")
    print(f"Hybrid Binary Search Best II: {hybrid_best}")
    
    if hybrid_best == 4 and pure_best == 6:
        print("[SUCCESS] Validation Passed: Pure binary search failed due to non-monotonicity trap at II=5, but Hybrid fallback found the true optimal II=4!")
    else:
        print("[FAIL] Unexpected result.")
