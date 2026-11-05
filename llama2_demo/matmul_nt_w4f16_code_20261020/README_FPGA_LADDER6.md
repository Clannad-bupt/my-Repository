# MatMul_NT_w4f16 FPGA 上板工作区（阶梯 6）

目标形状（Llama2-7B-FFN，gs=128，f16 稠密输出，N 按 32 列/tile 主机循环）：

| name | M | K | N | tiles | tensor 集 (tensors/) | stage case (cases/) | 打包 (pkgs/) |
|---|---|---|---|---|---|---|---|
| gate_up_pre | 256 | 4096 | 11008 | 344 | m256_k4096_n11008 | gate_up_pre_stage_t0 (4166 stages) | gate_up_pre_t0 |
| down_pre    | 256 | 11008 | 4096 | 128 | m256_k11008_n4096 | down_pre_stage_t0 (11186 stages) | down_pre_t0 |
| gate_up_dec | 1 | 4096 | 11008 | 344 | m1_k4096_n11008 | gate_up_dec_stage_t0 (35 stages) | gate_up_dec_t0 |
| down_dec    | 1 | 11008 | 4096 | 128 | m1_k11008_n4096 | down_dec_stage_t0 (89 stages) | down_dec_t0 |

gate/up 两个中 M 相同的 gate 与 up 形状完全一致（M=256 都是 256×11008×4096，M=1 同）——
一套 case/bundle 同时覆盖两个。

## 产物结构

- `cases/<name>_stage_t0/`：tile-0 全量 case（stage 树与 tile 无关——load 槽位地址、store
  落点、bank7 清零 stage 全部 tile 无关；仅镜像内容与 golden 随 tile 变）。
- `bundles/<name>/tile_XXXX/`：`segments/`（ch0..7 的 a/b/scale/zp/zeros 槽位段 +
  segments.json，可整目录替换包的 `case/hbm_data/`）、`golden/`（smc_*/output_bank）、
  `meta_v3.json`。
- `pkgs/<name>_t0/`：可直接上 U280（run_one_case.sh + runner + case）。
- `scripts/run_n_tiles.sh`：host 端 tile 循环（见下）。

## 上板 bring-up（在 zwz 主机）

```bash
# 1. 传包：scp -r pkgs/gate_up_dec_t0 zwz:~;  scp -r bundles/gate_up_dec zwz:~/gate_up_dec_t0/tiles
# 2. 单 tile（tile 0 = 包内自带 hbm_data/golden，无需 tiles/）：
cd ~/gate_up_dec_t0 && sudo ./run_one_case.sh
# 期望结尾： CASE PASS compared_smc=8
# 3. 全量 tiles：
cp <workspace>/scripts/run_n_tiles.sh ~/gate_up_dec_t0/
cd ~/gate_up_dec_t0 && sudo ./run_n_tiles.sh 344
```

推荐顺序：gate_up_dec tile 0 单发（最小形状，先证 32B 拼接 store 在真机 HBM 的落点，
计划风险 #7）→ gate_up_dec 全 344 → down_dec 全 128 → gate_up_pre tile 0 →
全 344 → down_pre 全 128。

## 比对准则（无容差）

- runner 做 stitched-store 逐位比对（golden word 流 vs `dma_from_device` 回读；任何差
  1 bit 即 FAIL，记录 smc/line/actual/golden）。
- **已知会出现的偶发单-lane 1-ULP FAIL**：fp32 累加顺序差异落到 f16 舍入中点上
  （计划风险 #6，阶梯 4/5 各见 1 例）。出现单-lane FAIL 时用 fp64 裁判裁决
  （`|exact - golden|` vs `|exact - fpga|` 谁更近；两例历史裁决都是 1 ULP 内、
  生成器侧更贴近真值），不与打包/搬运 bug 混淆——真 bug 会是大面积系统性偏差。
- 抽样独立校验（numpy 从原始张量复算 vs bundle golden，非 golden-vs-golden）结果：
  - M=256：gate_up_pre t3/t200 全中，t343 单 lane 1-ULP（fp64 裁决 generator 侧对）；
    down_pre t1 全中，t127 单 lane 1-ULP（同裁决）。
  - M=1：9/9 spot SMC 全中（A/B 两候选累加序均逐位一致、padding 全零）。

## 复现/再生成

- 全量 stage case：`scripts/gen_stage_cases.sh`（任务 A）。tile bundle：`scripts/gen_tile_bundles.sh`
  （任务 B；单 tile 用 `scripts/mk_tile_bundle.py <name> <M> <K> <N> <tdir> <tile>`）。
- 环境：`MM_M/MM_OUT_DTYPE=f16/MM_MULTISTAGE=1/MM_GROUP_SIZE=128/MM_N/K_TOTAL`
  `MM_SLOT_MAP=fpga MM_N_TILE_START=<tile>`；bundles 另加 `MM_CASE_IMAGE_ONLY=1`。
- 生成器二进制 `bin/gen_w4f16_check`（tvgen 一行 g++ 可重编，源见
  `load_store_v3/MatMul_NT_w4f16/`）。张量 `tensors/` 由 `gen_ext_tensors.py` seed=42 生成。
