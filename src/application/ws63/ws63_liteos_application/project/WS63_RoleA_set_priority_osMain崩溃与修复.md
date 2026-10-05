# WS63 Role A 启动时 set_priority 崩溃与修复

> 问题定位：2026-10-04（续第 7 次核查）
> 涉及文件：`clearchain_display_link.c`、`clearchain_device_app.c`、`clearchain_key.c`
> 关联根因文档：`WS63_重烧后仍白屏_真因_osMain优先级32越界与烧录未生效判别.md`

---

## 1. 现象

Role A（设备板，role `a` / `api` / `api_mock`）固件一上电就反复 `Load access fault` / `Reboot core` 重启循环，白屏。
串口特征与之前 Role B 的崩溃完全一致：

```
mcause:0x5  mtval:0x4  task:osMain  phase:Init
```

- `mcause=0x5` = Load access fault（读非法地址）
- `mtval=0x4` = 出错访问地址是 `NULL + 4`（典型"空指针 + 偏移 4"）
- `task:osMain` = 崩溃发生在内核启动伪任务（不是某个用户任务）

Role B 之前已经用"删除 entry.c 里那行 set_priority"修好并验证通过；Role A 因为初始化路径上还有同类调用，理论上一烧同样会踩雷。

---

## 2. 之前为什么卡住（排查史）

这条雷是从"白屏"问题一层层剥出来的，中间先后否掉了几个错误方向：

1. **以为是 LCD 任务栈溢出**（6KB / 8KB 栈不够跑 LVGL）→ 改成 20KB 后重烧仍崩，`mepc` 一字不差 → 排除。
2. **以为是 set_priority 实参越界**（传了 32）→ 但 `LOS_TaskPriSet` 有 `priority>31` 前置校验会拦下实参越界，且崩溃点在 `OsSchedPreempt` 不在 `LOS_TaskPriSet` → 排除。
3. **真因（Role B 那一轮已锁定）**：内核 `kernel/init/los_init.c:151` 给启动伪任务 `osMain`（`g_mainTask`）写死了 `priority = LOS_TASK_PRIORITY_LOWEST + 1 = 32`，**合法范围只有 0~31，越界**。初始化阶段"当前任务"就是 osMain，`OsSchedPreempt()` 用**当前任务自身 priority(=32)** 去入队 `g_priQueueList[32]`（数组仅 32 项）→ 越界读 `NULL+4` → fault。
4. **Role A 的特殊性**：Role B 唯一触发点是 `clearchain_display_client_entry.c` 那行（已删）。Role A 初始化走 `device_app_entry()`（osMain 上下文），里面还有 3 处同类 `set_priority` 调用，且**都没有加锁包裹** → 同样必崩。

> 卡住的真正原因不是"找不到修法"，而是这条崩溃路径**只在 osMain（调度器未启动）上下文 + 未加锁**时才触发，和 `main.c:320` / `tcp_client_demo.c:647` 那些"同样调 set_priority 却没事"的调用对比时，差异点（是否加锁、是否在任务内）一开始没被识别出来。把每个调用点的 caller 上下文逐行追出来后，才分清"3 处真崩、3 处其实安全"。

---

## 3. 解决方法（方案②：应用侧加锁包裹，已落地）

不动内核，只在 Role A 的 3 处 `create + set_priority` 外加 `osal_kthread_lock/unlock` 包裹，完全复用 SDK 自己已验证安全（`main.c:320`、`tcp_client_demo.c:647`）的写法。

### 3.1 改动清单（3 处）

**① `clearchain_display_link.c` —— `clearchain_display_link_init()`**
```c
    g_clearchain_display_started = true;
    osal_kthread_lock();
    (void)osal_kthread_set_priority(task, CLEAR_DISPLAY_TASK_PRIORITY);   // 28
    osal_kthread_unlock();
    osal_kfree(task);
```

**② `clearchain_device_app.c` —— `start_task()`**
```c
    if (task == NULL) { return -1; }
    osal_kthread_lock();
    (void)osal_kthread_set_priority(task, priority);   // 25 / 26 / 27
    osal_kthread_unlock();
    osal_kfree(task);
```

**③ `clearchain_key.c` —— `clearchain_key_start()`**
```c
    osal_kthread_lock();
    osal_kthread_set_priority(task_handle, 24);
    osal_kthread_unlock();
    osal_kfree(task_handle);
```

### 3.2 为什么这样改能消除崩溃（机制）

- 在 **osMain 上下文、解锁状态**下调 `osal_kthread_set_priority` → `LOS_TaskPriSet` → `LOS_Schedule()`：**只挡中断 `OS_INT_ACTIVE`，不挡任务锁** → 必然走进 `OsSchedPreempt()`，用 osMain 的 priority=32 越界入队 → fault。
- 加上 `osal_kthread_lock()` 后，`OsPreemptable()` 因 `losTaskLock != 0` 返回 false → `OsSchedPreempt()` 直接 early return，**根本不碰就绪队列** → 不会越界。
- 随后的 `osal_kthread_unlock()` → `LOS_TaskUnlock` → `OsTaskUnlockVerify()`：除 `taskLockCnt==0` 外，**还要求 `OS_SCHEDULER_ACTIVE`**；而 `osKernelStart()` 之前该标志为假 → 不触发重调度 → 仍然安全。
- 对比：不加锁时在 osMain 里必崩；加锁后无论调用方是 osMain 还是某个任务，都不会崩（在任务内调用时 unlock 会正常触发一次重调度，也是安全的）。

### 3.3 为什么选②不选①

| 维度 | ① 内核根治 | ② 应用侧（采用） |
|---|---|---|
| 改动位置 | `kernel/.../los_sched.c` `OsSchedPreempt()`（约 3 行） | 3 处调用点加 lock/unlock |
| 影响面 | 全部角色 + 内核本体（**广面高危**） | 仅 Role A 固件；Role B 完全不动 |
| 重建成本 | 重编内核 + 全量重建 + 重烧所有固件 | 仅重建 role a / api + 重烧设备板 |
| 持久性 | 治本，挡住当前及未来同类触发 | 治已知 3 处；未来新代码可能重犯 |
| 优先级语义 | 保留（优先级仍生效） | 保留（优先级仍生效） |

① 动的是厂商内核 + 所有角色，属"广面高危"操作，按规矩需你点头后才做；② 仅动 Role A 三处、可 `git` 回退、且和 SDK 自带安全范式一致 → 按你的授权（"你定要不要做、做哪种"）直接执行。

---

## 4. 需要执行的命令

> 以下均在 **本地 Git Bash** 执行，工作目录 `D:\ws\fbb\src`（注意不是 PowerShell 的 `cd /d`，Git Bash 用 `cd D:/ws/fbb/src`）。

### 4.1 重新构建 Role A 固件
```bash
cd D:/ws/fbb/src

# role a（设备板 / CLEARCHAIN_DISPLAY_SLE_SERVER）
C:/Python314/python.exe application/ws63/ws63_liteos_application/project/tools/build_dual.py a

# role api / api_mock（SERVER + clearchain_device_app.c，会编译 device_app_entry 那条崩溃链）
C:/Python314/python.exe application/ws63/ws63_liteos_application/project/tools/build_dual.py api
```

### 4.2 产物与烧录
- 产物：`D:\ws\fbb\src\output\clearchain\board_a\ws63-liteos-app_all.fwpkg`、及 `board_api/...`
- 用平时烧 `.fwpkg` 的工具选对应包 → 烧写到 **100%**（盯进度，别像之前卡在 0%）→ 断电上电。

### 4.3 验收（本地串口，UART0=GPIO17/18 @ 115200）
- ✅ 不再出现 `Load access fault` / `Reboot core` 重启循环
- ✅ LCD 正常显示 UI（非白屏）
- ✅ 任务优先级仍为设定值（`CCDisplaySLE`=28、`ClearChainKey`=24、`CCWifi/CCStatePoll/CCCapture`=26/27/25）
- 若仍崩：回贴串口日志，转"方案①内核根治"线（改 `los_sched.c` 的 `OsSchedPreempt` 跳过伪任务入队）。

### 4.4 回退方式（代码改动可逆）
```bash
git -C D:\ws\fbb checkout -- \
  src/application/ws63/ws63_liteos_application/project/clearchain_display_link.c \
  src/application/ws63/ws63_liteos_application/project/clearchain_device_app.c \
  src/application/ws63/ws63_liteos_application/project/clearchain_key.c
```

---

## 5. 涉及的知识点

1. **LiteOS 启动伪任务 `osMain`**：内核把启动流程伪装成一个"任务" `g_mainTask`，其优先级被写死为 `LOS_TASK_PRIORITY_LOWEST + 1 = 32`，**不在合法 0~31 范围内**；初始化阶段"当前任务"就是它。
2. **就绪队列越界**：`g_priQueueList[OS_PRIORITY_QUEUE_NUM]` 合法索引 0..31；用 32 做下标越界读到 `NULL` 附近 → 读 `NULL+4=0x4` → `Load access fault`（`mcause=0x5`）。
3. **`LOS_TaskPriSet` 校验的盲区**：它只校验"被设置任务的优先级 >31"，挡不住"osMain 自身字段是 32"这种情况。
4. **任务锁 `osal_kthread_lock/unlock` 的作用**：`lock` 让 `OsPreemptable()` 返回 false，使 `OsSchedPreempt` 直接返回、不碰就绪队列；这是 SDK 内部 `main.c:320`、`tcp_client_demo.c:647` 已验证安全的写法。
5. **`OsTaskUnlockVerify` 的 `OS_SCHEDULER_ACTIVE` 门控**：`osal_kthread_unlock` 在 `osKernelStart()` 之前因调度器未激活而不触发重调度，所以即使在 osMain 里 unlock 也安全。
6. **调用上下文决定生死**：同样是 `set_priority`，在 osMain 未加锁 → 崩；在 `main.c:320` 加锁 → 安全；在任务函数体内（如 `wifi_tcp_client_demo`）→ 安全（调度器已运行）。排查这类问题要把每个调用点的 caller 上下文追出来，不能只看符号名。
7. **RISC-V 异常现场解读**：`mcause`=异常类型、`mtval`=出错访问地址、`mepc`=出错指令地址、`ra`=返回地址（用 `ra` 找真实调用者比栈扫描可靠）。
8. **增量构建陷阱**：ninja 按 mtime 判断是否重编，旧 `.obj` 比源文件新会被判"最新"而跳过；必须用 `nm`/`objdump`/`strings` 在产物里反查确认修复真的进包，不能只看构建 SUCCESS。

---

## 6. 待办 / 风险

- ⏳ 待你在本地 Git Bash 跑 4.1 的两条构建命令，烧录设备板并回贴串口验收结果。
- ⚠️ 方案①内核根治仍可选（治本、挡未来同类触发），但动内核 + 全角色属广面高危，需你明确点头后再做。
- ⚠️ 构建环境坑：CodeBuddy 的 safe-delete 曾拦截构建脚本里的 `shutil.rmtree`（回收站不可用）导致"签名/清理"阶段失败；若本地构建在收尾报错，多半是同一原因，清理临时目录即可，无需改 SDK。
