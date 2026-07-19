# 1 Makefile的引入及规则

## 1.1 引入背景：工具的内部机制
在Linux下开发裸板程序时，需手动通过Makefile组织管理程序，核心解决“如何决定编译哪些文件”的问题。


## 1.2实验程序示例
### 1.2.1 源文件
- **a.c**
  ```c
  #include <stdio.h>

  int main()
  {
      func_b();
      return 0;
  }
  ```
- **b.c**
  ```c
  #include <stdio.h>

  void func_b()
  {
      printf("This is B\n");
  }
  ```

### 1.2.2 基础编译与运行
- 编译命令：`gcc -o test a.c b.c`
- 运行命令：`./test`
- 运行结果：`This is B`


## 1.3 编译的本质：四步流程
`.c`源文件生成可执行程序需经过**预处理→编译→汇编→链接**四步，其中前三步常统称为“编译”。以`gcc -o test a.c b.c`为例，具体流程如下：
1.  对`a.c`：预处理→编译→汇编，生成`xxx.o`（目标文件）；
2.  对`b.c`：预处理→编译→汇编，生成`yyy.o`（目标文件）；
3.  链接`xxx.o`与`yyy.o`，生成可执行程序`test`。

> 注：添加`-v`选项可查看完整处理过程，命令为`gcc -o test a.c b.c -v`。


## 1.4 传统编译的缺点
直接执行`gcc -o test a.c b.c`存在**效率问题**：  
即使仅修改`a.c`，未改动的`b.c`也会重新经历“预处理→编译→汇编”全流程。当文件数量庞大时，会导致编译耗时极长。

**分步骤编译+链接**：先分别编译单个源文件生成目标文件（`.o`），仅修改过的源文件需重新编译，最后统一链接目标文件。示例命令如下：
- 编译单个文件：`gcc -c -o a.o a.c`、`gcc -c -o b.o b.c`（`-c`选项表示仅编译不链接）；
- 链接目标文件：`gcc -o test a.o b.o`。


## 1.5 Makefile核心：规则
Makefile通过**规则**自动判断“哪些文件需要重新处理”，核心逻辑是“比较目标与依赖的修改时间”。

### 1.5.1 规则语法
```makefile
目标 : 依赖1 依赖2 ...
	[TAB]命令  # 必须以TAB键开头
```
- **逻辑**：当“任意依赖的修改时间比目标更新”时，执行下方命令重新生成目标。

### 1.5.2 实验性Makefile
针对上述`a.c`、`b.c`程序，编写的Makefile如下：
```makefile
test : a.o b.o  # 目标test依赖a.o和b.o
	gcc -o test a.o b.o  # 链接生成test

a.o : a.c  # 目标a.o依赖a.c
	gcc -c -o a.o a.c  # 编译a.c生成a.o

b.o : b.c  # 目标b.o依赖b.c
	gcc -c -o b.o b.c  # 编译b.c生成b.o
```


## 1.6 Makefile执行验证
在源文件目录下创建`Makefile`（文件名固定），执行`make`命令即可触发规则判断，不同场景结果如下：

### 1.6.1 第一次执行`make`
- 场景：无`a.o`、`b.o`、`test`文件；
- 执行流程：
  1.  生成`a.o`：因无`a.o`，默认`a.c`更新，执行`gcc -c -o a.o a.c`；
  2.  生成`b.o`：因无`b.o`，默认`b.c`更新，执行`gcc -c -o b.o b.c`；
  3.  生成`test`：因无`test`，执行`gcc -o test a.o b.o`；
- 执行命令：3条命令全执行。

### 1.6.2 未修改文件时再次执行`make`
- 场景：`a.c`、`b.c`、`a.o`、`b.o`、`test`均存在且未修改；
- 结果：`make: 'test' is up to date.`（依赖无更新，目标无需重新生成）。

### 1.6.3 仅修改`a.c`后执行`make`
- 场景：`a.c`修改，`b.c`未修改；
- 执行流程：
  1.  重新生成`a.o`：`a.c`比`a.o`新，执行`gcc -c -o a.o a.c`；
  2.  不生成`b.o`：`b.c`未更新；
  3.  重新生成`test`：`a.o`比`test`新，执行`gcc -o test a.o b.o`；
- 执行命令：2条命令。

### 4. 同时修改`a.c`和`b.c`后执行`make`
- 场景：`a.c`、`b.c`均修改；
- 执行流程：重新生成`a.o`、`b.o`，再链接生成`test`；
- 执行命令：3条命令全执行。

# 2 Makefile的语法

## 2.1 通配符
当目标依赖的文件数量众多时，可通过通配符简化规则编写，无需为每个文件单独定义规则。

### 2.1.1 核心通配符与特殊变量
| 符号       | 含义                     |
|------------|--------------------------|
| `%.o`      | 匹配所有`.o`目标文件     |
| `%.c`      | 匹配所有`.c`源文件       |
| `$@`       | 表示当前规则中的“目标”   |
| `$<`       | 表示当前规则中的“第一个依赖文件” |
| `$^`       | 表示当前规则中的“所有依赖文件”   |

### 2.2.2 简化多文件编译规则
#### （1）基础优化（a.c、b.c）
对上节程序的Makefile进行简化，无需单独定义`a.o`、`b.o`的规则：
```makefile
test: a.o b.o 
	gcc -o test $^  # $^ 代表所有依赖（a.o b.o）
%.o : %.c         # 匹配所有.o文件，依赖对应的.c文件
	gcc -c -o $@ $<  # $@ 是目标（如a.o），$< 是第一个依赖（如a.c）
```

#### （2）新增文件扩展（添加c.c）
- 新增`c.c`文件：
  ```c
  #include <stdio.h>
  void func_c()
  {
  	printf("This is C\n");
  }
  ```
- 修改`a.c`的`main`函数，调用`func_c()`；同时更新Makefile：
  ```makefile
  test: a.o b.o c.o  # 仅需添加新的.o依赖
  	gcc -o test $^
  %.o : %.c
  	gcc -c -o $@ $<
  ```

#### （3）执行结果
```bash
# 执行make
gcc -c -o a.o a.c
gcc -c -o b.o b.c
gcc -c -o c.o c.c
gcc -o test a.o b.o c.o

# 运行可执行程序
./test
This is B
This is C
```


## 2.2 假想目标：.PHONY
### 2.2.1 场景：清除编译产物（clean目标）
通常在Makefile末尾添加`clean`目标，用于删除`.o`文件和可执行程序：
```makefile
test: a.o b.o c.o
	gcc -o test $^
%.o : %.c
	gcc -c -o $@ $<

# 清除文件的规则
clean:
	rm *.o test  # 删除所有.o文件和test可执行程序
```

#### （1）`make`命令的目标用法
- 执行`make`：默认生成第一个目标（`test`）；
- 执行`make clean`：执行`clean`目标下的命令，清除编译产物。

### 2.2.2 问题：同名文件冲突
若当前目录下存在名为`clean`的文件，执行`make clean`时会提示：
```bash
make: `clean' is up to date.
```
原因：Makefile规则执行的条件是“目标不存在”或“依赖比目标新”。当`clean`文件存在且无依赖时，规则被判定为“已更新”，不执行删除命令。

### 2.2.3 解决方案：定义假想目标
通过`.PHONY`关键字将`clean`定义为“假想目标”，Makefile会忽略同名文件，强制执行规则命令：
```makefile
test: a.o b.o c.o
	gcc -o test $^
%.o : %.c
	gcc -c -o $@ $<

clean:
	rm *.o test

.PHONY: clean  # 声明clean为假想目标，不检查同名文件
```
此时无论是否存在`clean`文件，执行`make clean`都会正常删除编译产物。


## 2.3 变量
Makefile中的变量用于简化规则编写，按解析时机分为“即时变量”和“延时变量”，另有特殊赋值方式。

### 2.3.1 变量类型与赋值方式
| 赋值符号 | 变量类型       | 特点                                                                 |
|----------|----------------|----------------------------------------------------------------------|
| `:=`     | 即时变量       | 定义时立即确定值，后续修改依赖不影响变量值                           |
| `=`      | 延时变量       | 仅在**使用时**确定值，依赖后续的定义或修改                           |
| `?=`     | 条件延时变量   | 仅当变量**首次定义**时生效，若变量已存在（含命令行传入），则忽略此赋值 |
| `+=`     | 附加赋值       | 向变量追加内容，变量类型（即时/延时）与之前的定义保持一致             |

### 2.3.2 变量引用与命令静默
- 引用变量：使用`$()`包裹变量名，如`$(A)`；
- 命令静默：在命令前加`@`，执行`make`时不显示命令本身（仅显示结果）。

### 2.3.3 实例
#### （1）测试Makefile
```makefile
# 定义变量
A := $(C)    # 即时变量：定义时解析$(C)
B = $(C)     # 延时变量：使用时解析$(C)
C = abc      # 定义C的初始值
# D = 100ask  # 注释掉，模拟D未提前定义
D ?= weidongshan  # 条件赋值：D未定义则生效

all:  # 自定义目标（替代默认的test目标）
	@echo A = $(A)  # 静默输出A的值
	@echo B = $(B)  # 静默输出B的值
	@echo D = $(D)  # 静默输出D的值

C += 123  # 向C追加内容
```

#### （2）执行结果与分析
```bash
# 执行make
A = 
B = abc 123 
D = weidongshan
```
- **A的值解析**：`A := $(C)`定义时，`C`尚未赋值（为空），故A的值为空白，后续`C`的修改不影响A；
- **B的值解析**：`B = $(C)`是延时变量，执行`@echo B = $(B)`时才解析`C`。此时`C`经历了`C=abc`和`C+=123`，最终值为`abc 123`，故B的值与之相同；
- **D的值解析**：因`D`未提前定义，`D ?= weidongshan`生效，D的值为`weidongshan`。

#### （3）命令行传入变量
通过命令行传入变量可覆盖`?=`的赋值：
```bash
# 执行make D=123456（命令行定义D）
A = 
B = abc 123 
D = 123456  # D的值为命令行传入的123456，忽略D ?= weidongshan
```

# 3 Makefile的函数

## 3.1 foreach函数：遍历列表并处理
### 3.1.1 语法
```makefile
$(foreach var, list, text)
```
- **参数说明**：
  - `var`：循环变量名；
  - `list`：待遍历的列表（元素以空格分隔）；
  - `text`：对每个元素的处理逻辑（可引用循环变量`var`）。
- **执行逻辑**：
  1.  先扩展`var`和`list`（`text`暂不扩展）；
  2.  遍历`list`中的每个元素，为`var`赋值；
  3.  对每个`var`值扩展`text`；
  4.  将所有`text`扩展结果用空格拼接，作为函数返回值。

### 3.1.2 实例
```makefile
A = a b c  # 定义待遍历的列表
B = $(foreach f, $(A), $(f).o)  # 遍历A的每个元素，添加.o后缀

all:
	@echo B = $(B)  # 静默输出结果
```

### 3.1.3 执行结果
```bash
B = a.o b.o c.o
```


## 3.2 filter/filter-out函数：筛选列表元素
这两个函数用于按模式筛选列表中的元素，功能互为补充。

### 3.2.1 语法
| 函数                | 功能                                  |
|---------------------|---------------------------------------|
| `$(filter pattern..., text)`    | 从`text`中提取符合`pattern`格式的元素 |
| `$(filter-out pattern..., text)` | 从`text`中提取**不符合**`pattern`格式的元素 |

- 注：`pattern`可使用通配符（如`%/`匹配以`/`结尾的字符串）。

### 3.2.2 实例
```makefile
C = a b c d/  # 包含普通字符和以/结尾的字符
D = $(filter %/, $(C))    # 筛选以/结尾的元素
E = $(filter-out %/, $(C)) # 筛选不以/结尾的元素

all:
	@echo D = $(D)
	@echo E = $(E)
```

### 3.2.3 执行结果
```bash
D = d/
E = a b c
```


## 3.3 Wildcard函数：查找存在的文件
### 3.3.1 语法
```makefile
$(wildcard pattern)
```
- **功能**：根据`pattern`（文件名格式，可含通配符）查找当前目录下**真实存在**的文件，返回以空格分隔的文件名列表。

### 3.3.2 实例1：直接匹配文件格式
#### 前置操作
在当前目录下创建`a.c`、`b.c`、`c.c`三个文件。

#### Makefile代码
```makefile
files = $(wildcard *.c)  # 查找所有.c格式的存在文件

all:
	@echo files = $(files)
```

#### 执行结果
```bash
files = a.c b.c c.c
```

### 3.3.3 实例2：验证列表中的存在文件
```makefile
files2 = a.c b.c c.c d.c e.c abc  # 包含真实文件和不存在的文件/字符
files3 = $(wildcard $(files2))     # 筛选files2中真实存在的文件

all:
	@echo files3 = $(files3)
```

#### 执行结果
```bash
files3 = a.c b.c c.c
```


## 3.4 patsubst函数：字符串模式替换
### 3.4.1 语法
```makefile
$(patsubst pattern, replacement, $(var))
```
- **功能**：遍历`var`中的每个元素，若元素符合`pattern`格式，则替换为`replacement`格式；不符合则保持原样。
- 注：`pattern`和`replacement`中可使用`%`作为通配符（表示任意字符序列）。

### 3.4.2 实例
```makefile
files2 = a.c b.c c.c d.c e.c abc  # 包含.c文件和普通字符
dep_files = $(patsubst %.c, %.d, $(files2))  # 将.c替换为.d

all:
	@echo dep_files = $(dep_files)
```

### 3.4.3 执行结果
```bash
dep_files = a.d b.d c.d d.d e.d abc
```
