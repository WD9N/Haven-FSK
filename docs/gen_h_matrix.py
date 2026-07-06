import numpy as np

def build_peg_matrix(n=192, k=96, d_v=3, d_c=6, seed=1234):
    np.random.seed(seed)
    m = n - k
    H = np.zeros((m, n), dtype=np.uint8)
    cdeg = np.zeros(m, dtype=int)

    def get_reachable(H, j, depth):
        visited_c, visited_v = set(), {j}
        frontier = set(np.where(H[:, j] == 1)[0])
        visited_c.update(frontier)
        for _ in range(depth):
            nv = set()
            for c in frontier:
                nv.update(v for v in np.where(H[c,:]==1)[0]
                          if v not in visited_v)
            visited_v.update(nv)
            nc = set()
            for v in nv:
                nc.update(c for c in np.where(H[:,v]==1)[0]
                          if c not in visited_c)
            if not nc:
                break
            frontier = nc
            visited_c.update(nc)
        return visited_c

    for j in range(n):
        for edge in range(d_v):
            avail = np.where(cdeg < d_c)[0]
            if edge == 0:
                mn = cdeg[avail].min()
                chosen = np.random.choice(avail[cdeg[avail] == mn])
            else:
                reach = get_reachable(H, j, edge * 2 + 1)
                pool = [c for c in avail if c not in reach] or list(avail)
                mn = min(cdeg[c] for c in pool)
                chosen = np.random.choice(
                    [c for c in pool if cdeg[c] == mn])
            H[chosen, j] = 1
            cdeg[chosen] += 1
    return H

H = build_peg_matrix()
print("static const int H_NONZERO[96][6] = {")
for i in range(H.shape[0]):
    cols = list(np.where(H[i] == 1)[0])
    assert len(cols) == 6, f"Row {i} has {len(cols)} nonzero entries"
    print("    {" + ", ".join(str(c) for c in cols) + "},")
print("};")
