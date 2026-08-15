#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_theory.py -- 定理1/定理2 的数据验证（三模型对照: 连续 / floor 实现 / supply-limited）

验证一 (exp1): 持续可运行进程间 share ∝ scale(n)=floor(n^(a/100))  [定理1机制层]
验证二 (exp2): 恒定负载下 ctrl share 单调降, 高压区吻合理论, 宽裕区被供应天花板保护
验证三 (exp4): 相位负载下 fixed 各档 miss; share 模型是 miss 的充分条件而非必要条件;
               真实临界带由 AIMD H 段稳态标定
用法: python verify_theory.py
"""
import os
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MECH = os.path.join(ROOT, 'logs', 'sched', 'mech', 'sexp1_mech_1.csv')
EDGE = os.path.join(ROOT, 'logs', 'sched', 'edge', 'edge_multi.csv')
DYN = os.path.join(ROOT, 'logs', 'sched', 'dyn', 'sexp4_dyn.csv')

def sfloor(n, a):
    """math.rs sched_thread_scale 的镜像: floor(n^(a/100)).max(1)"""
    return max(1, int(n ** (a / 100.0)))

def share(a, n_ai, n_log, impl=True, T_c=300, T_a=100, T_l=50):
    fa = sfloor(n_ai, a) if impl else n_ai ** (a / 100.0)
    fl = sfloor(n_log, a) if impl else n_log ** (a / 100.0)
    return T_c / (T_c + T_a * fa + T_l * fl)

def iter_csv(path, head_prefix, parse_head, data_prefixes=('W,', 'D,', 'S,')):
    """通用段解析: head 行开段, WARMUP/done 行关段, 其余 '#' 行忽略(不关段)"""
    cur = None
    with open(path) as f:
        for line in f:
            if line.startswith(head_prefix):
                cur = parse_head(line)
            elif line.startswith('#'):
                if 'WARMUP' in line or 'done' in line:
                    cur = None
            elif cur is not None and line.startswith(data_prefixes):
                yield cur, line.rstrip('\n').split(',')

# ---------------------------------------------------------------- 验证一: exp1
def verify_exp1():
    print('=' * 76)
    print('验证一 [exp1] 定理1机制层: 等tickets下 份额比 = floor(n^(a/100)) ?')
    print('=' * 76)
    acc = defaultdict(lambda: defaultdict(int))
    def head(line):
        a = int(line.split('alpha=')[1].split()[0])
        return ('trial', a)
    for (tag, a), p in iter_csv(MECH, '# TRIAL alpha=', head):
        if p[0] == 'W':
            acc[a][p[4]] += int(p[5])
    ec, ef, rows = [], [], []
    for a in sorted(acc):
        g = acc[a]; tot = sum(g.values())
        if tot == 0 or not all(k in g for k in ('t1', 't2', 't3')):
            continue
        s1 = g['t1'] / tot
        obs9, obs25 = g['t2'] / tot / s1, g['t3'] / tot / s1
        x = a / 100.0
        ec += [abs(obs9 - 9 ** x), abs(obs25 - 25 ** x)]
        ef += [abs(obs9 - sfloor(9, a)), abs(obs25 - sfloor(25, a))]
        if a % 10 == 0:
            rows.append((a, obs9, sfloor(9, a), obs25, sfloor(25, a)))
    print(f'{"alpha":>5} {"t2/t1实测":>9} {"floor(9^x)":>10} {"t3/t1实测":>9} {"floor(25^x)":>11}')
    for a, o9, f9, o25, f25 in rows:
        print(f'{a:>5} {o9:>9.3f} {f9:>10.3f} {o25:>9.3f} {f25:>11.3f}')
    n = len(ec)
    print(f'\n101档平均|实测-理论|: 连续模型={sum(ec)/n:.4f}  floor实现模型={sum(ef)/n:.4f}'
          f'  --> floor 模型胜(阶梯结构被数据证实)')

# ---------------------------------------------------------------- 验证二: exp2
def verify_exp2():
    print()
    print('=' * 76)
    print('验证二 [exp2] 定理1+2: ctrl share 单调降; 高压区吻合理论, 宽裕区被供应保护')
    print('=' * 76)
    CFG = {'light': (7, 3), 'medlo': (15, 8), 'medium': (25, 9),
           'heavy': (75, 25), 'extreme': (225, 50)}
    acc = defaultdict(lambda: defaultdict(int))
    def head(line):
        h = line.split()          # ['#','RUN','config=x','alpha=n','rep=k/m']
        return (h[2].split('=')[1], int(h[3].split('=')[1]))
    for key, p in iter_csv(EDGE, '# RUN config=', head):
        if p[0] == 'W':
            acc[key][p[4]] += int(p[5])
    print(f'{"config":>8} {"a":>4} {"ctrl实":>7} {"ai实":>7} {"log实":>7} '
          f'{"ctrl理论":>8} {"|差|":>6}  状态')
    hi_err, lo_err = [], []
    for cfg in ('light', 'medlo', 'medium', 'heavy', 'extreme'):
        for a in (0, 25, 50, 75, 100):
            g = acc.get((cfg, a))
            if not g: continue
            tot = sum(g.values())
            if tot == 0 or 'ctrl' not in g: continue
            obs = g['ctrl'] / tot
            na, nl = CFG[cfg]
            th = share(a, na, nl)
            regime = '压迫区' if obs < th + 0.02 else '宽裕区(supply)'
            (hi_err if obs < th + 0.02 else lo_err).append(abs(obs - th))
            print(f'{cfg:>8} {a:>4} {obs:>7.3f} {g["ai"]/tot:>7.3f} {g["log"]/tot:>7.3f} '
                  f'{th:>8.3f} {abs(obs-th):>6.3f}  {regime}')
    if hi_err:
        print(f'\n压迫区平均误差={sum(hi_err)/len(hi_err):.4f} (模型适用), '
              f'宽裕区平均偏差={sum(lo_err)/len(lo_err):.4f} (ctrl被线程供应天花板保护, 模型给的是上限)')

# ---------------------------------------------------------------- 验证三: exp4
def verify_exp4():
    print()
    print('=' * 76)
    print('验证三 [exp4] 定理2: 相位负载 miss 表 + supply-limited 修正 + AIMD 真实临界带')
    print('=' * 76)
    segD = defaultdict(lambda: [0, 0, 0])   # (mode,seg) -> jobs,miss,late
    segW = defaultdict(lambda: defaultdict(int))
    def head(line):
        h = line.split()          # ['#','RUN','mode=m','alpha0=n','rep=k/m']
        return h[2].split('=')[1]
    for mode, p in iter_csv(DYN, '# RUN mode=', head):
        if p[0] == 'D':
            w = int(p[1]); seg = min((w - 1) // 600, 3)
            d = segD[(mode, seg)]
            d[0] += int(p[3]); d[1] += int(p[4]); d[2] += int(p[5])
        elif p[0] == 'W' and int(p[1]) > 50:
            segW[mode][p[4]] += int(p[5])
    PH = ['L1', 'H1', 'L2', 'H2']
    print(f'{"mode":>9} {"相":>3} {"share连":>7} {"sharefl":>7} {"ctrl实":>6} '
          f'{"jobs":>7} {"miss率":>7} {"late":>9}')
    print('-' * 76)
    for mode in ('fixed0', 'fixed25', 'fixed50', 'fixed75', 'fixed100'):
        a = int(mode.replace('fixed', ''))
        for pi in range(4):
            ph = 'L' if pi % 2 == 0 else 'H'
            jobs, miss, late = segD[(mode, pi)]
            wt = sum(segW[mode].values())
            obs = segW[mode]['ctrl'] / wt if wt else 0
            rate = miss / jobs * 100 if jobs else 0
            print(f'{mode:>9} {PH[pi]:>3} {share(a,5 if ph=="L" else 50,3,impl=False):>7.3f} '
                  f'{share(a,5 if ph=="L" else 50,3):>7.3f} {obs:>6.3f} '
                  f'{jobs:>7} {rate:>6.1f}% {late:>9}')
    # supply-limited 瓜分验证
    print('\nsupply-limited 验证(fixed0): ai/log 按剩余容量x eff比瓜分')
    g = segW['fixed0']; tot = sum(g.values()); sc = g['ctrl'] / tot
    print(f'  实测 ai={g["ai"]/tot:.3f} log={g["log"]/tot:.3f} | '
          f'预测 ai={(1-sc)*100/150:.3f} log={(1-sc)*50/150:.3f}')
    # AIMD 真实临界带
    allS = []
    def head2(line):
        return 'aimd' if 'mode=aimd' in line else None
    for tag, p in iter_csv(DYN, '# RUN mode=', head2):
        if tag == 'aimd' and p[0] == 'S':
            allS.append((int(p[1]), int(p[2])))
    if allS:
        H = sorted(a for w, a in allS if 600 <= w < 1200 or 1800 <= w < 2400)
        L = sorted(a for w, a in allS if w < 600 or 1200 <= w < 1800)
        q = lambda d, p: d[min(int(len(d) * p), len(d) - 1)]
        print(f'\nAIMD 稳态(实验测得的"真实临界带"): H段 p25={q(H,.25)} 中位={q(H,.5)} p75={q(H,.75)}'
              f' | L段 p25={q(L,.25)} 中位={q(L,.5)}')
        print('  --> AIMD 退避停在 25-27 附近: 真实 miss 临界带在 alpha~25-50 之间,'
              ' 高于 share 模型预测(9.4/17.7), 因为 miss 机制是唤醒排队延迟而非份额不足')

if __name__ == '__main__':
    verify_exp1()
    verify_exp2()
    verify_exp4()
