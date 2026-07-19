# 1. RDMA
RDMA是远程内存访问技术（Remote Memory Access, RMA）的代表，它允许网络适配器直接访问远程节点的内存，而无需远程节点的CPU参与。RDMA通过零拷贝、内核旁路和传输卸载三大核心技术实现了高效低延迟的数据传输。

零拷贝技术解决了传统数据传输的拷贝瓶颈，传统数据传输在传统网络通信（TCP/IP）中数据经历多次内存拷贝：用户态到内核态、内核态到网卡、接收端的反向拷贝，每次拷贝都会消耗CPU 资源增加延迟，在进行大量数据传输时，内存带宽成为瓶颈。

RDMA技术通过内存注册和直接内存访问两步实现零拷贝。

内存注册：应用程序预先将内存区域注册到RDMA网卡（RNIC）上，RNIC为注册的内存生成虚拟地址和密钥，注册过程中锁定物理内存，确保传输期间数据无法修改，保证数据一致性。

直接内存访问：发送端RNIC直接从注册的内存中读取数据，接收端RNIC直接将数据写入目标应用程序的注册内存。

内核旁路机制解决了传统内核协议处理数据包时造成的开销，包括上下文切换、中断处理、锁竞争等。

内核旁路机制通过用户态驱动和硬件协作实现，采用队列对（QP）模型维护通信端点队列对，令用户态直接操作RNIC，直接向RNIC提出工作请求，完整过程无需系统调用，操作完成后RNIC将结果写入完成队列。内核旁路机制使得RDMA能够消除上下文切换和终端延迟，避免内核协议栈处理，减少CPU占用。

传输卸载机制减轻了传统协议的CPU负担。由RNIC硬件直接处理协议逻辑，完全释放CPU，RNIC处理链路层到传输层的完整协议。

RDMA还提供众多操作原语，如Compare&Swap和Fetch&Add，这些操作原语在硬件层面保证了对远程内存的原子性访问，适用于分布式锁、计数器等多种场景。

# 2. SketchINT

网络测量对流量工程、异常检测、故障排查等各种网络操作至关重要，需要在流粒度下对单个交换机进行测量。INT（In-band Network Telemetry带内网络遥测）解决方案因能提供细粒度的逐交换机、逐数据包信息，成为实现流粒度逐交换机测量的理想方案。INT通过配置交换机在每个输入数据包中插入预定义的数据包级信息（即INT信息）来获取逐交换机信息。

通过结合INT和草图技术，在支持流粒度逐交换机测量的同时降低INT的控制平面开销。其核心设计是将所有INT信息先压缩为紧凑草图再收集，而非像INT那样直接传输。SketchINT有三种工作模式：在接收端交换机（边缘交换机）部署草图、在端主机部署草图以提高测量精度、将草图卸载到基于FPGA的SmartNIC以节省主机CPU资源，这要求草图能在P4、CPU和FPGA平台上简单部署。设计了简单且准确的TowerSketch。其数据结构仅包含若干计数器数组和哈希函数，通过为不同数组配置不同大小的计数器，高层数组计数器少但容量大，底层数组计数器多但容量小，利用网络流量的偏斜特性，多数流小、少数大流占主导，自动将大流记录在大计数器、小流记录在小计数器。

### 系统架构与组件：
SketchINT代理：将INT元数据编码为紧凑的TowerSketch，可灵活部署在可编程边缘交换机、端主机CPU或SmartNIC上，根据运营商监控意图选择工作模式。
INT兼容交换机：在数据包中插入所需的逐交换机INT元数据。
全局SketchINT分析器：部署在商用服务器中，收集所有SketchINT代理的TowerSketch，具备高弹性和可扩展性。
### 工作流程：
利用INT搭载数据包级统计
定制INT层，包含16位跳数计数器和元数据字段，插入到传输层与负载之间。每经过一个交换机，插入元数据并递增跳数，首跳交换机添加INT指令头并修改DSCP字段标识INT数据包。
在端主机将INT信息编码到TowerSketch端主机构建多个TowerSketch，读取INT元数据后移除INT头部，避免干扰上层协议。将交换机ID、内部延迟等信息插入TowerSketch，可选地将大流转发至其他数据结构以提升精度。
收集草图并执行全网分析
端主机维护两组TowerSketch活跃组T0和闲置组T1，定期交换状态。分析器收集所有本地草图，获取全网视图，支持对每个流在各交换机的深度分析。
### SketchINT 支持三种工作模式
**边缘交换机模式**：在P4可编程边缘交换机部署TowerSketch，适合交换机内存有限的场景。
端主机模式：利用端主机充足内存提升测量精度，支持复杂插入策略。
FPGA 模式：将草图卸载到SmartNIC，节省主机CPU资源，降低成本。

## TowerSketch 设计 
### A. 经典 Count-Min（CM）草图  
CM 草图由 d 个计数器数组 \(A_1, \dots, A_d\) 组成，每个数组 \(A_i\) 包含 w 个计数器，并使用哈希函数 \(h_i(.)\) 将流随机均匀映射到对应计数器。当流 f 的数据包到达时，CM 计算哈希值找到 d 个计数器 \(A_1[h_1(f)], \dots, A_d[h_d(f)]\) 并将其值加 1。查询流 f 的数据包数量时，返回 d 个计数器中的最小值。  

基于 CM 草图的 ConservativeUpdate（CU）草图仅修改插入操作：仅递增最小的计数器, 若有多个最小值则全部递增。与 CM 相比，CU 显著提升精度，但牺牲了流水线实现能力。两者均无低估误差。  

### B. 数据结构与操作原理  
设计理念：TowerSketch 的核心思想是为不同数组配置不同大小的计数器——高层数组计数器容量更大、数量更少，底层数组计数器容量更小、数量更多。通过为每个数组分配相同内存总量，实现大流自动记录在大计数器、小流记录在小计数器，匹配网络流量的偏斜特性（多数流小、少数大流占主导）。  

**数据结构**：TowerSketch 包含 d 个数组 \(A_1, \dots, A_d\)，每个数组 \(A_i\) 有 \(w_i\) 个计数器，关联哈希函数 \(h_i(.)\)，计数器位宽为 \(\delta_i\)。关键特性是：底层数组计数器多、位宽小，高层数组计数器少、位宽大，且各数组占用内存总量相同。  

**插入策略**：  
CM 插入：对每个流 f 的数据包，递增 d 个哈希计数器。若 \(\delta\) 位计数器递增后溢出，将其值设为 \(2^\delta - 1\)（视为 \(+\infty\)，不再更新）。  
CU 插入：仅递增未溢出的最小计数器，大幅提升精度，但不支持流水线实现。  
近似 CU（ACU）插入：受 SuMax 草图启发，按特定顺序访问计数器（推荐自底向上），仅递增当前最小值计数器，平衡精度与流水线兼容性。自底向上访问因优先处理底层小计数器，精度接近 CU 插入。  

**查询操作**：无论插入策略如何，均返回 d 个哈希计数器的最小值，溢出计数器视为 \(+\infty\)。  

### C. 插入策略对比与讨论  
| 策略       | 精度   | 复杂度 | 流水线支持 |  
|------------|--------|--------|------------|  
| CM 插入   | 低     | 低     | ✔️         |  
| ACU 插入  | 中等   | 中等   | ✔️         |  
| CU 插入   | 高     | 高     | ❌         |  

与 CM/CU 相比，TowerSketch 对大流因计数器数量少而高估误差略大，但对小流因计数器数量多而显著降低高估误差，整体精度更高。实验表明，仅为不同数组分配不同位宽计数器而不调整数量（如 CM(O)/CU(O)）无法充分利用流量偏斜特性，精度提升有限。
## 误差边界分析  
### A. 先验误差边界分析  
误差边界估计对草图配置至关重要，网络运营商通常期望以最小内存开销满足精度要求。TowerSketch的先验误差边界，与网络工作负载无关，即最坏情况误差边界。  

设 \(\delta_0 = 0\)，且 \(\delta_0 < \delta_1 < \cdots < \delta_d\)。对于任意流 \(f_j\)，不失一般性，假设其实际大小 \(n_j\) 满足 \(2^{\delta_{t-1}} - 1 \leq n_j < 2^{\delta_t} - 1\)（\(1 \leq t \leq d\)）。令 \(m\) 为流的数量，\(n\) 为所有流实际大小之和，即 \(n = \sum_{l=1}^m n_l\)。  

**定理1（先验误差边界）**：对于使用CM插入的TowerSketch，给定任意正数 \(\epsilon\)，流 \(f_j\) 的估计误差满足：  
\[
\begin{aligned}
& Pr\left\{\hat{n}_j \leq n_j + \epsilon\right\} \geq 1 - \prod_{k=t}^{q-1}\left\{\frac{n}{(2^{\delta_k} - n_j - 1) \cdot w_k}\right\} \\
& \times \prod_{k=q}^{d}\left\{\frac{n}{\epsilon \cdot w_k}\right\}
\end{aligned}
\]  
其中 \(q\) 满足 \(2^{\delta_{q-1}} - 1 \leq n_j + \epsilon < 2^{\delta_q} - 1\)。  

**证明**：定义指示变量 \(I_{j,k,l}\)：  
\[
I_{j,k,l} = 
\begin{cases} 
1, & h_k(f_j) = h_k(f_l) \land j \neq l \\
0, & 否则 
\end{cases}
\]  
由于 \(d\) 个哈希函数相互独立，有：  
\[
E(I_{j,k,l}) = Pr\left\{h_k(f_j) = h_k(f_l)\right\} = \frac{1}{w_k}
\]  
定义变量 \(X_{j,k} = \sum_{l=1}^m n_l \cdot I_{j,k,l}\)，表示数组 \(A_k\) 中哈希冲突导致的估计误差。对于 \(\forall k \geq t\)，有：  
\[
A_k[h_k(f_j)] = 
\begin{cases} 
n_j + X_{j,k}, & n_j + X_{j,k} < 2^{\delta_k} - 1 \\
+\infty, & 否则 
\end{cases}
\]  
且期望 \(E(X_{j,k}) \leq \frac{n}{w_k}\)。  

误差概率推导如下：  
\[
\begin{aligned}
& Pr\left\{\hat{n}_j \geq n_j + \epsilon\right\} \\
= & Pr\left\{\forall k \geq t, A_k[h_k(f_j)] \geq n_j + \epsilon\right\} \\
= & Pr\left\{\forall k, t \leq k < q, n_j + X_{j,k} \geq 2^{\delta_k} - 1\right\} \\
& \cdot Pr\left\{\forall k \geq q, n_j + X_{j,k} \geq n_j + \epsilon\right\} \\
= & Pr\left\{\forall k, t \leq k < q, X_{j,k} \geq 2^{\delta_k} - n_j - 1\right\} \\
& \cdot Pr\left\{\forall k \geq q, X_{j,k} \geq \epsilon\right\}
\end{aligned}
\]  
根据马尔可夫不等式：  
\[
\begin{aligned}
& Pr\left\{\hat{n}_j \geq n_j + \epsilon\right\} \\
\leq & \prod_{k=t}^{q-1}\left\{\frac{E(X_{j,k})}{2^{\delta_k} - n_j - 1}\right\} \prod_{k=q}^{d}\left\{\frac{E(X_{j,k})}{\epsilon}\right\} \\
\leq & \prod_{k=t}^{q-1}\left\{\frac{n}{(2^{\delta_k} - n_j - 1) \cdot w_k}\right\} \prod_{k=q}^{d}\left\{\frac{n}{\epsilon \cdot w_k}\right\}
\end{aligned}
\]  
因此：  
\[
\begin{aligned}
& Pr\left\{\hat{n}_j \leq n_j + \epsilon\right\} \\
\geq & 1 - \prod_{k=t}^{q-1}\left\{\frac{n}{(2^{\delta_k} - n_j - 1) \cdot w_k}\right\} \prod_{k=q}^{d}\left\{\frac{n}{\epsilon \cdot w_k}\right\}
\end{aligned}
\]  
□  

由定理1可知，流大小 \(n_j\) 越小，\(t\) 和 \(q\) 越小，先验误差边界越小，即TowerSketch对小流的误差更小。  


### B. 后验误差边界分析  
传统方案基于与工作负载无关的最坏情况误差边界配置草图，但实际误差与工作负载强相关，尤其在流量高度偏斜时，最坏情况边界会导致内存浪费。State-of-the-art方案SketchError为经典CM草图提供了带理论保证的后验误差估计，将其扩展至TowerSketch。  

**定理2（后验误差边界）**：对于插入完成后的CM插入TowerSketch，给定任意正数 \(\epsilon\)，流 \(f_j\) 的估计误差满足：  
\[
Pr\left\{\hat{n}_j \leq n_j + \epsilon\right\} \approx 1 - \prod_{k=t}^{q-1} F_k(2^{\delta_k} - 2 - n_j) \prod_{k=q}^{d} F_k(\epsilon)
\]  
其中 \(q\) 满足 \(2^{\delta_{q-1}} - 1 \leq n_j + \epsilon < 2^{\delta_q} - 1\)，\(F_k(R)\) 表示数组 \(A_k\) 中值大于 \(R\) 的计数器比例。  

**证明**：TowerSketch数组 \(A_k\) 中每个计数器的值可视为随机变量 \(Y_k\) 的样本：  
\[
Y_k = 
\begin{cases} 
\sum_{l=1}^m n_l M_{k,l}, & \sum_{l=1}^m n_l M_{k,l} < 2^{\delta_k} - 1 \\
+\infty, & 否则 
\end{cases}
\]  
其中 \(M_{k,l}\) 是0/1变量，表示流 \(f_l\) 哈希到 \(A_k\) 中某计数器的概率，\(Pr\{M_{k,l}=1\} = \frac{1}{w_k}\)。  

将数组分为三类：  
1. **第一类数组**（\(k < t\)）：计数器必然被 \(n_j\) 溢出，\(Pr\{A_k[h_k(f_j)] > n_j + \epsilon\} = 1\)。  
2. **第二类数组**（\(t \leq k < q\)）：计数器未被 \(n_j\) 溢出但被 \(n_j + \epsilon\) 溢出。根据伯努利大数定律，\(Pr\{A_k[h_k(f_j)] > n_j + \epsilon\} \approx F_k(2^{\delta_k} - 2 - n_j)\)。  
3. **第三类数组**（\(k \geq q\)）：计数器未被 \(n_j + \epsilon\) 溢出，类似地，\(Pr\{A_k[h_k(f_j)] > n_j + \epsilon\} \approx F_k(\epsilon)\)。  

因此：  
\[
\begin{aligned}
& Pr\left\{\hat{n}_j \leq n_j + \epsilon\right\} \\
= & 1 - \prod_{k=1}^{d} Pr\left\{A_k[h_k(f_j)] > n_j + \epsilon\right\} \\
\approx & 1 - \prod_{k=t}^{q-1} F_k(2^{\delta_k} - 2 - n_j) \prod_{k=q}^{d} F_k(\epsilon)
\end{aligned}
\]  

定理2表明，后验误差边界同样依赖流大小，小流的误差更小。对于CM插入的TowerSketch，可直接使用定理2的右侧公式估计后验误差，其正确性由实验验证。  

### 支持P4的交换机上的TowerSketch  
**标准版本**：支持P4的Tofino交换机上实现了TowerSketch，可作为边缘交换机使用。边缘交换机可收集INT元数据字段并执行TowerSketch支持的测量任务。为在P4交换机中执行所有测量任务，所需的TowerSketch数量与数据中心的最大跳数相同（通常为5）。由于Tofino交换机以流水线方式处理数据包，除非对大部分数据包进行循环处理（这会消耗大量带宽），否则TowerSketch不支持CU插入。因此，我们仅通过若干寄存器和状态ALU（SALU）在Tofino交换机上实现了使用CM插入和ACU插入的TowerSketch。对于每个由 \(w_i\) 个 \(\delta_i\) 位计数器组成的计数器数组 \(A_i\)，我们构建一个包含 \(w_i\) 个寄存器单元的寄存器，每个单元存储 \(A_i\) 中的对应计数器。注意，Tofino交换机的寄存器仅支持8位、16位和32位单元，因此我们使用略大于 \(\delta_i\) 位的寄存器单元存储计数器。对于每个输入数据包，我们使用其5元组流ID和两两独立的哈希函数定位每个数组中的哈希单元，然后使用SALU按第四节所述对每个数组中的哈希单元执行操作。  

支持2位计数器：还可通过使用多个寄存器单元支持2位计数器。具体而言，可使用三个级联的1位寄存器单元模拟2位计数器。插入时，将第一个为0的1位寄存器单元置1；查询时，将三个1位寄存器单元的值求和作为2位计数器的值。显然，三个级联的1位寄存器单元在功能上等同于一个2位计数器。模拟2位计数器的代价主要有两方面：1）50%的额外内存开销；2）与仅访问一个8位寄存器单元的8位计数器相比，访问三个1位寄存器单元需要多两个SALU。这种对2位计数器的扩展具有重要价值，尤其是在网络流量高度偏斜的情况下：使用2位计数器记录大小为1或2的流比使用8位计数器更节省内存。该扩展不仅适用于TowerSketch，还适用于其他依赖小尺寸计数器的草图，如FCMSketch、PyramidSketch和Cold Filter。  

对更大计数器的扩展：还可通过使用多个寄存器单元支持大于32位的计数器。以48位计数器为例，用一对32位和16位寄存器单元模拟它。从32位单元的输出中获取流大小和溢出信息，然后访问16位单元。结合两个单元的输出，即可模拟48位计数器。显然，这一基本思路可扩展为支持任意大于32位的计数器。  

[SketchINT代码](https://github.com/SketchINT-code/SketchINT)