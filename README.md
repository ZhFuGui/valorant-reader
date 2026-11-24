# valorant-reader

Windows x64、C++20、VS2022 单项目工程。已实现 Hyper-mem 只读后端、进程实例绑定、完整主关卡 Actor 名单和类型分类，以及人物位置、22 点主干骨骼、独立相机的持续采集与 TCP 订阅。三个类别各自调频、各自采样、各自发布。

唯一读取后端为 **Hyper-mem**。通过 `readers-shared` 共享源码静态编入，`src/app/composition.cpp` 组合只读适配器；运行时不切换后端，也不依赖额外的共享 DLL。

## 结构

```text
valorant-reader/
├─ valorant-reader.sln
├─ projects/reader/             # VS 项目与目录视图
├─ build/                       # 编译和外置产物配置
└─ src/
   ├─ app/                     # 程序入口、参数和依赖组合
   ├─ backends/shared/          # 共享层公开接口与产品内存接口之间的薄适配
   ├─ memory/                  # 只读通道、合批接口、进程身份与会话
   ├─ capture/                 # 频率需求、时间线、时钟及发布接口
   ├─ model/                   # 普通数据模型和采样时间
   ├─ game/valorant/           # 当前构建身份、布局描述与已分析偏移
   ├─ runtime/                 # 两个共享读取 worker 与有界任务交接
   ├─ synchronization/v1/      # 消费／调试投影、基线、增量与时钟协商
   ├─ transport/               # TCP 记录、短写及连接隔离，与游戏无关
   └─ platform/windows/        # 共享 QPC 适配与游戏构建的原始 PE 头核验
```

通道归 worker 独占；进程身份使用 PID 与创建时间共同核验。结果发布后不可修改，会话替换前先收尾依赖它的采集器。普通字段缺失与会话失效需要分别处理。

实体按当前构建的原生类型继承关系分类；地图、本地受控人物及 Bomb 原生团队关系分别读取和核验，未支持的团队方案保持未知。模型不依赖 socket，采集接口不依赖具体后端 ABI，协议层只使用普通状态与发布接口。

## 构建

安装 VS2022 的桌面 C++ 工作负载、v143 工具集和 Windows SDK，打开 `valorant-reader.sln`，选择 `Debug|x64` 或 `Release|x64`。

默认要求同级目录存在 `../readers-shared/readers-shared.props`，也可通过 MSBuild 的 `ReadersSharedDirectory` 指定其目录。工程只导入 Hyper-mem 入口，公共源码与产品沿用同一 CRT、异常展开和优化配置；独立对象文件位于本产品的外置 `IntDir/readers-shared`。共享层负责进程发现、固定实例会话、绑定核验、原生合批与底层地址空间恢复；游戏对象核验、两个 worker、调度、epoch 与订阅仍由本项目管理。

共享源码依赖固定为 [readers-shared](https://github.com/ZhFuGui/readers-shared.git) 提交 `5a0fb3e26dfc7c8188e722fdb0aa340bda65cb3f`；构建前须将该版本源码、构建入口及许可证准备到依赖目录。此提交属于共享层本身，Hyper-mem ABI 的独立来源仍见其 `third_party/hypermem/UPSTREAM.json`。该发布版本的公开接口、实现和构建属性表与本次已验证的接入基线逐字节一致。

默认产物位于 `%TEMP%\valorant-reader\build`。可指定其他磁盘：

```powershell
msbuild .\valorant-reader.sln /m /p:Configuration=Release /p:Platform=x64 /p:BuildArtifactsRoot=U:\valorant\build
```

源码目录不生成 `bin`、`obj` 或日志。

## 初始化检查

```powershell
U:\valorant\build\bin\x64\Release\valorant-reader.exe --check
```

只有显式 `--check` 执行一次实现状态检查然后退出，不访问游戏进程，也不触发后端握手。无参数或 `--serve` 进入持续服务，自动等待、发现并核验 `VALORANT-Win64-Shipping.exe`。`backend_implemented`、`capture_implemented`、`subscription_implemented` 表示相应实现已编译。检查命令中的 `backend_ready=false`、`capture_ready=false`、`subscription_ready=false` 表示本命令未建立会话或监听，不能解释成运行故障。当前偏移规则仍处于分析状态，`profile_ready=false`；已经单独验证的链路可通过 `--inspect` 或持续服务使用。

退出码：`0` 为命令成功，`1` 为运行或不支持错误，`2` 为参数错误，`3` 为资料或实现尚未接入，`4` 为本次观察不可用。

## 一次性只读检查

在已经加载相应 Hyper-mem 后端的游戏主机上显式指定 PID：

```powershell
valorant-reader.exe --inspect --pid 20496
```

该命令只执行一次检查并退出，不启动监听或持续采集。进程发现通过超调用读取内核链，绑定 PID、创建时间、CR3 和 PEB；主映像必须匹配当前规则的原始 PE 身份。内核布局按运行映像导出和来源锚点核验，后端实例复用成功布局。没有目标进程句柄、R3 目标读写路径、符号下载、注入或目标函数调用，也不提供写通道。

通道独占调用方缓冲，合批按上游的 64 项／64KiB 边界提交；这不是总请求数或 Actor 数量上限。大连续范围保留完整单范围读取，每个结果分别确认完整字节数。

`directory_complete=true` 只表示完整复制主关卡名单，并完成本次根、数组头和所属关系核验。统计区分全部槽位、空引用、非法引用、重复项及唯一地址；唯一地址仍需后续逐对象生命周期和类型核验，不代表有效人物数。短读或来源变化不返回部分成功名单，不伪造属性或空列表。

名单描述的是一次非原子的观察。数组头相同不能证明并发期间每个元素都没改变；后续属性采集必须重新核验实际使用的对象身份。检查耗时包含根和会话核验及本地整理，不能据此推断完整属性采集性能。

`classification.complete=true` 表示全部唯一地址的分类任务已达到终态，且共享来源与世界上下文通过关闭核验；不表示每个对象都分类成功。终态分别统计成功、非 Actor、默认对象、销毁中、短读、来源变化、非法数据、未支持和未执行。只有成功行保留身份和类别，未知对象不产生虚构人物。

分类按当前构建的原生 UClass 单例与 SuperStruct 关系，互斥选择 `OtherActor`、`Pawn`、`Character` 或 `ShooterCharacter`。同时核验解释类型记录的元类型来源，防止把普通对象按 UClass 解释。类名文本不参与业务识别；分类不会为了输出诊断名称而读取或解码名称池。

每批每个类型只解析、关闭一次，失败只撤销依赖它的结果；该缓存不跨批复用。共享类型锚点、对象目录或 World 失效时撤销整批发布资格。分类关闭时仅核验世界上下文，不隐式再复制完整 Actor 名单，也不要求分类期间所有成员都保持不变。

对象身份按完整名单跨对象合批：先读取全部对象头，再按实际使用的注册分块读取条目，最后合批关闭。分块引用只在单次调用内共享，不跨批保存；类型推导、销毁位、逐对象终态和实际祖先链核验保留。底层请求边界由读取通道分批提交，不成为 Actor 数量配额。

对象身份联合核验内部索引、注册槽位、实际分块、原生弱序号、类型、原始名称值和 Outer。弱序号为零时允许观察，但没有原生代际证明；这些字段只提供有限观察区间的来源证据，不是消费者永久实体 ID，也不承诺排除完全相同的地址复用。`ShooterCharacter` 表示原生派生关系，不直接表示真人、BOT、敌我、存活或可见性。

## 已接入的属性采样

`--inspect` 在完整分类之后尝试全部已确认 `Character`／`ShooterCharacter` 的坐标和主干骨骼，并独立尝试本地相机。没有人物或名单失败，不会阻止相机读取。JSON 的 `positions`、`poses`、`view` 分别报告结果；`properties_sampled=true` 表示执行过属性采样，不代表每个属性都有值。

```text
game/valorant/
├─ observation        # 单批对象身份、类型与注册表来源核验
├─ scene              # 场景世界变换选择及来源核验
├─ position           # Character → RootComponent → 世界位置
├─ context            # World 包身份、本地控制链与人物关联来源
├─ components         # 稀疏组件集合、分配位及成员关闭核验
├─ team               # PlayerState／无玩家状态 BOT → 原生团队与关系
├─ view               # World → 本地玩家 → Controller → 相机缓存
├─ rig                # 主干点名称、语义与原生祖先关系规则
├─ rig_catalog        # 同批共享骨骼资产布局及其关闭核验
└─ pose               # Character → Mesh → 自身／Leader 姿态 → 主干世界坐标
```

三个属性读取器各自具有采样序号、epoch 和 QPC 观察区间，不要求身体、视角和姿态在同一时刻读取。`started_qpc` 在本批第一次读取前记录，`read_finished_qpc` 在最后读取及必要读取核验后记录，`finished_qpc` 在本地整理后记录。同机使用提供的 QPC 频率；相机的 `engine_timestamp` 属于原生缓存时间域，不能直接与 QPC 相减。当前检查命令是顺序执行的单次验证入口，不是独立类别的持续调度器。

坐标取 Actor 根组件的原生世界变换平移，不用相对位置、网格原点、骨点或历史值代替。相机取相机管理器的当前缓存，读取位置、已求值旋转、原生 FOV、画幅、画幅约束、正交宽度及投影枚举。相机不要求 Pawn 存在；未知投影枚举保留未知，当前数据不等于已核验所有渲染窗口规则的最终投影矩阵。

骨骼由当前资产名称和真实父骨关系识别 22 点：躯干根、骨盆、下脊柱、胸、颈、头，以及双侧锁骨、肩、肘、手、髋、膝、足、趾。骨盆和下脊柱在当前规则中同属躯干根的两个分支，不虚构骨盆到脊柱的原生父子关系。未知名称、歧义或拓扑不符的模型返回未支持，不套用数字下标。

每次只读取这 22 点的当前组件空间平移，结合原组件的世界变换求世界坐标，不复制完整动态骨架，也不再次累计父骨变换。单点短读或无效映射只让该点缺失；共同来源失效才撤销整个人物骨骼。同批相同资产只解析、关闭一次布局；当前尚无跨批资产缓存，也不复用任何旧动态值。

Leader 分支先核验弱序号、注册条目和组件类型，再使用原组件的骨点映射及 Leader 当前姿态，最后应用原组件的世界变换。原生无效弱引用与读取失败分开处理，读取失败不退回自身姿态。注册表已扩展而本批没有覆盖新目标时撤销当前观察，留给下一批重新建立来源。有效 Leader 分支已有离线夹具覆盖，尚无当前实机有效 Leader 样本。

名称密钥来源暂时变化时，资产目录最多立即重取一次完整证据；普通缺页不立即重试，真正的密钥变化仍撤销依赖结果。重取次数由 `poses.metadata_source_retries` 单独报告。必要核验保留身份、类型、实际所用指针链、缓冲选择、资产拓扑和解释来源；不要求运动坐标保持不变。缓冲下标、数组头和有限身份证据仍不等于游戏内存的原子快照，也不能排除同一分配内全部写入竞态。

批次 `source_status=0` 只表示共享来源有效。人物与骨点还分别具有自己的终态；未可用、来源变化或未支持结果不填零坐标、不伪造空骨架、不借用上一批。调试 JSON 中的地址和原生下标是本次检查证据，尚未作为正式消费协议或永久实体 ID 发布。

骨骼的 `failed_at` 报告失败所在阶段，取值定义与中文含义维护在 `pose.hpp` 的 `PoseFailureStage` 中。它只复用当前采样分支，不增加目标内存读取。批次字段说明共享来源，逐人物字段说明该人物；成功时为 `None`，单点缺失仍须查看各自 `joint_status`。

地图和本地控制信息附属于 Positions 批次的 `context`。地图身份取当前 `UWorld` 与其 `UPackage` 的原生完整名称，不由 Reader 提供底图换算参数。本地身体取 `LocalPlayers[0].PlayerController.Pawn`，并核验 `Pawn.Controller` 反向关系；相机观战目标、旧 AcknowledgedPawn 或 PlayerState 保存的身体不作为回退。关联状态区分 Unknown、Absent、Unresolved 和 Resolved，只有 Resolved 携带同 Reader 实例、同 epoch 的实体 ID。

队伍优先取角色关联的 `AAresPlayerStateBase.TeamComponent`；没有 PlayerState 的训练 BOT 读取自身组件集合。原生 Bomb 团队字节的 0 是有效值，0xfe 的关系保持未知；已核验的原生关系实现才产生 Ally／Enemy，其他方案不套用相等整数规则。Self 与当前已解析的本地实体 ID 一致。本地团队参照独立取控制器 PlayerState，死亡后不会换成被观战人物的团队。相关链暂时失败时只清除本批相应字段，仍有效的坐标继续发布。

## 持续采集与订阅

```powershell
valorant-reader.exe --serve
# 显式开放到本机局域网网卡；默认只监听 127.0.0.1:13302。
valorant-reader.exe --serve --bind 10.63.114.201 --port 13302 --max-connections 16
```

远端生产安装位置固定为 `%LOCALAPPDATA%\Programs\Northwind Systems\Runtime Node\RuntimeNode.exe`，同目录放置仓库的 `LICENSE.txt`。编译产物仍名为 `valorant-reader.exe`，生产部署只改变安装文件名，不改变业务逻辑和协议。安装或升级只验证文件及规则，不自动启动 Reader；运行需另行明确授权。

Debug 和 Release 均为 Windows GUI 子系统，使用标准 `wWinMainCRTStartup`／`wWinMain`，不自动分配控制台，也不创建应用窗口。文件属性由 `resources/version.rc` 维护，生产产品名为 Runtime Node、公司为 Northwind Systems，当前文件及产品版本为 1.0.0.0。命令行检查的文本输出可由启动方重定向；无窗口运行时使用命名停止事件管理正常退出。

局域网部署为固定生产路径配置一次入站规则，升级时保持路径不变。在 Reader 主机的管理员 PowerShell 中按实际网卡地址、接口名和网段执行：

```powershell
$readerExe = Join-Path $env:LOCALAPPDATA 'Programs\Northwind Systems\Runtime Node\RuntimeNode.exe'
New-NetFirewallRule -Name 'ValorantReader-LAN-TCP-13302' -DisplayName 'Valorant Reader - LAN TCP 13302' `
  -Group 'valorant-reader' -Direction Inbound -Action Allow -Enabled True -Profile Any `
  -Program $readerExe -Protocol TCP -LocalPort 13302 -LocalAddress '10.63.114.201' `
  -RemoteAddress '10.60.0.0/14' -InterfaceAlias 'WLAN' -EdgeTraversalPolicy Block
```

该规则只允许指定程序、端口、网卡及局域网来源；不关闭防火墙，不改变网卡的公用／专用分类。规则只需创建一次，可用 `Get-NetFirewallRule -Name 'ValorantReader-LAN-TCP-13302'` 检查；卸载时用同名 `Remove-NetFirewallRule` 删除。

消费者直接连接 Reader 主机的实际 IPv4 和 `13302`，每类别单独建立 TCP 连接，按 v1 握手；不需要转发或中间进程。主机自身连接成功而其他电脑超时，应先检查入站规则和网络路径。游戏未绑定时服务仍接受订阅、时钟协商与心跳，明确返回等待状态，不伪造空采样。默认自动模式在游戏退出后撤销旧代次，重新等待和绑定下一实例，不需要重新提供 PID。可用 `--pid` 固定一个实例进行诊断，此模式不跟随 PID 被复用的新实例；`--inspect --pid` 仍只检查一次。当前没有开机自启配置。

自动发现只使用 Hyper-mem：内核短名称仅筛出候选，再通过同一只读通道核验 x64 PEB 的完整映像基名和绑定后的原始 PE 构建身份。不会因内核名称截短而把同前缀程序认作游戏；候选歧义、短读或链变动保持不可用并重试。正常绑定期间不做名称全表查找，也不调用原生进程枚举或目标进程 R3 API。

无窗口运行时应显式传入 `--stop-event`，例如 `--stop-event Global\NorthwindSystems.RuntimeNode.Stop`，再由有权限的控制程序设置同名事件正常停止。`Local\名称` 仅供同一 Windows 会话使用。退出先等待已返回或正在返回的读取任务安全收尾，不强杀线程。不可返回的底层调用不能仅靠进程内逻辑恢复。

主循环负责调度、提交、编码和非阻塞网络 I/O，两个共享 worker 执行绑定、名单、分类和属性读取。管理循环不执行慢读或进程绑定。名单、分类、位置、骨骼、相机分别最多一个在途任务；基础任务优先，业务按请求频率与实测 worker 占用结算服务量。大批次不截断，不削减字段，不复用旧动态值。

类别采集目标取全部有效订阅的最高请求，分类目标再加 1Hz，名单复制再加 1Hz。请求范围 1..300Hz，支持小数；这是目标上限，不承诺当前硬件和场景达到。无业务订阅时停采属性，分类 1Hz、名单 2Hz。时间线只保留一个待机会，忙碌与迟到如实计数，不积压历史周期或突发补采。

字段缺页只产生当前值缺失；明确失效撤销对应来源结果。进程实例、地址空间或世界上下文明确变化会增加 epoch，旧批次晚完成不能重新发布。普通字段失败不触发重绑。会话暂时不可读时先撤销当前结果并退避复验；连续三次不确定才独立查同一 PID 的创建时间，仍是原实例则继续等待，不反复重绑。确认 CR3／PEB 变化才重绑同一实例，确认退出或实例替换才在自动模式重新发现。Reader 自身页表异常同样在异常路径复探并先复验目标生命周期，不用地址空间恢复掩盖同时发生的退出。

绑定、健康复验和 TCP 重建使用 100、200、500、1000、2000、5000ms 有界退避及小幅抖动，持续正常进展五秒后复位。工作线程的可返回异常在本线程边界释放其读取通道，下次任务重新建立；暂时无法创建 worker 时等待资源恢复。版本不匹配保留会话并观察生命周期，不反复打开不兼容采集器、不猜偏移、不退出整个服务。停止优先，不能返回的底层调用不强杀、不堆积替代线程。

## 三流协议 v1

正式入口为 `reader.proto`；当前为 v1 revision2，随仓库提供冻结描述符和 SHA-256 摘要。与旧 revision1 的摘要不同，Reader 与消费者必须成对更新，不在旧摘要下增加字段。产品使用有限的 Protobuf wire 编解码器，无 gRPC 或 Protobuf 运行库依赖；已用官方生成代码交叉解析。

```text
protocol/reader/v1/
├─ reader.proto              # 连接、订阅、调频、状态及跨机时钟协商
├─ update.proto              # 独立采样身份、时间、完整基线与增量
├─ diagnostic.proto          # 显式调试投影的来源和失败证据
├─ model/
│  ├─ context.proto          # 地图、本地人物关联、原生队伍及关系语义
│  ├─ position.proto         # 人物位置、队伍、关系及本批上下文
│  ├─ skeleton.proto         # 22 点主干骨骼，静态语义及父子关系
│  ├─ view.proto             # 独立相机
│  └─ foundation/
│     └─ common.proto        # 双精度坐标与欧拉角
├─ contract.json             # 机器可读的语义、约束和原因编号
├─ schema-manifest.json      # 冻结摘要及规范化规则
└─ reader.descriptor.pb      # 与源码和握手摘要对应的描述符
```

采用单端口 TCP 长连接，每条连接绑定一个类别和消费／调试投影。默认端口 `13302`，默认本机回环，局域网地址在启动时显式配置。连接预算默认 16，可配置为 1..60，不限制 Actor 数量。同类别采集需求取有效订阅的最高请求，每连接保留自己的发布上限；位置、骨骼、相机分别调频，不要求跨流同序号或同时采样。

消费记录只包含 Reader 身份、必要配置与时间以及实际数据。骨骼按固定的 22 点语义编码布局号、有效点位图和紧密排列的世界 XYZ，不逐帧传原生下标、骨点名字或父子连线。地址、注册索引、弱序号、来源覆盖与失败阶段只进入显式调试投影；消费模式仍需执行保证正确性的必要核验。

握手依次为 Hello → Subscribe → Configured／Reject → Snapshot，随后发送 Delta、Status、Heartbeat。记录边界为四字节大端正文长度加 Protobuf Envelope，正文最多 16MiB。控制请求号非零递增；SetRate 使用本连接最后 Configured 的配置版本，其他连接调频不会使该版本失效。

每连接先发完整基线，后续增量相对该连接最后完整发送的状态。实体 upsert 是本类别整条记录替换，缺失值会清除旧值；删除与属性缺失分别表达。Resync 等待下一次新采样建立完整快照，避免同连接重复已发送序号，重连不借用旧连接基线。每连接只保留一条发送中记录和一个最新待发送状态，不设置帧有效期或按年龄淘汰。编码超限不截断集合，发送原因 1008 并等待新采样；半条 TCP 记录明确失效时关闭该连接，由新基线恢复。

实体引用作用域为 Reader 实例、全局 epoch 和 Reader 分配的 ID；跨流按此引用关联，不直接传地址充当身份。生命周期目录根据完整名单及本轮身份核验维护 ID；临时未知只保留 ID 和缺失值，不沿用旧坐标。原生 Character／ShooterCharacter 分类不直接表示真人、BOT、存活或可见性；队伍关系使用单独核验的 Bomb 团队来源。

Positions 完整快照必须包含上下文；增量中的上下文存在时整体替换，内部缺失清除旧字段，缺失则表示本批新读取后内容未变。人物 upsert 同样完整替换位置、队伍和关系。未知团队或本地关联不能被消费者自动解释为敌人，低频相机或骨骼不得刷新这些字段的采样时间。

跨机时钟使用 Hello 的域标识、频率和版本。消费者保留发送查询前的 t0 与收完整响应后的 t3；ClockSample 携带 Reader 收完整请求的 t1，外层 `send_started_qpc` 是 t2。客户端多次交换估计时差、往返耗时及误差，再用 UseClockMapping 选择已完成交换的映射版本。映射参数由消费者维护，不能直接相减两机原始计数。全部采样和发送锚点绑定本批身份与时钟版本；发送锚点在封装后原位盖戳，短写续传不改首次发送时刻。

已验证三流实机采集、调频反转、同类多消费者、重连、Resync、跨 epoch 恢复、时钟版本协商、消费／调试隔离及慢消费者合并。另用 8MiB 记录验证 TCP 分段短写、半条失效和其他连接继续工作。实测中字段仍可能因临时来源变化缺失，完整遍历不表示全部字段成功。测试、生成代码、分析报告与编译产物留在外置目录，不入库。

后续继续补充真人／BOT、非 Bomb 团队规则及装备等游戏语义；每条链仍需针对当前构建单独验证。局域网启动参数只控制监听，不自动修改主机防火墙。

## 偏移表与当前分析

统一放在 `src/game/valorant/offsets.hpp`，按字段所属对象分组，不把指针链中多个对象的偏移相加成一个地址：

```text
Profile
├─ revision / image / state    # 当前构建与整份规则的验证状态
└─ layout
   ├─ globals                 # 引擎、名称和对象注册表的模块级定位入口
   ├─ foundation              # 数组头、向量、旋转与变换记录的格式/大小
   ├─ identity                # UObject、UClass、FName、名称池和对象注册表
   ├─ world                   # Engine、游戏视口、World、Level、GameInstance、GameState
   ├─ actor                   # Actor、Pawn、Character、Scene/MovementComponent
   ├─ view                    # UPlayer、Controller、CameraManager、CameraCache、POV
   ├─ pose                    # 网格组件、骨骼资产、参考骨架、动态姿态来源
   └─ gameplay                # ShooterCharacter、AresInventory、AresPlayerStateBase 的专属引用
```

布局类型默认缺失，当前构建的取值统一在 `profile.cpp` 中填写。`Member` 是结构内偏移，`Root` 是带模块归属及存储形式的 RVA，`Size` 是大小/步长等布局参数。位字段另带掩码。已确认的零值通过 `std::optional` 与未定位区分。`profile_ready` 要求构建身份、验证状态和主关卡名单的最小布局同时具备；这不等于对象分类、相机或骨骼采集能力已实现。

当前规则为 `1998eb9c-f35f000-e432a6b-analysis-4`，状态是 `UnderAnalysis`，`profile_ready` 仍为 `false`。基础属性偏移由当前主映像的反射描述与原生构造代码交叉定位；引擎、视口、世界、主关卡、本地玩家、身体、相机及主干骨骼已有 Hyper-mem 只读实机样本。普通属性的描述确认、动态值可读和完整采集能力分别验证，不能互相代替。

构建身份采用原始运行映像的 PE 三元组。重建 dump 的 CheckSum 被置零，因此未将该零值作为原始身份。资源中的 `5.3.2.0` 是文件版本信息，不用作游戏补丁号。

`roots.cpp` 实现引擎入口、名称密钥、对象注册表存储和名称池的只读解析。引擎严格按已确认的原生选择器分支取值；未知代码返回未支持，不尝试其他槽位。名称密钥与对象目录使用各自的指针算术，两者不能混用。`pointer_codec.cpp` 只做本地整数运算，不依赖读取后端，也不执行目标函数。动态分配地址没有填入全局 RVA。

根解析在读取前核对模块身份，并在读取后核验更新锁、种子、解释后的指针及所用来源。原生调用计数单独增长不等于来源失效。每次调用只尝试一次，临时短读或来源变化交给上层重试，不在解析器里重绑进程或循环等待。解析成功也不代表后续对象生命周期已自动得到保证。

当前已确认 `FName` 为 12 字节，顺序为比较索引、实例数字、显示索引；名称项以 4 字节为定位粒度。`names.cpp` 按当前构建的文本异或规则解释名称，核对显示项与比较索引，处理实例数字后缀，并检查块、项和负载边界。窄字符仅接受已确认的 ASCII；宽字符按实际字节处理规则解密后验证 UTF-16 并转成 UTF-8。尚未证明的窄字符扩展编码返回未支持，不猜测转换。

`NamePoolView` 绑定调用方已核验的会话来源，不能隐式跨会话或跨批次复用。`RootForm::ResolverAnchor` 只表示静态分析锚点，禁止当作数据指针或调用目标函数。

Riot 在 [11.02 官方更新说明](https://playvalorant.com/en-gb/news/game-updates/valorant-patch-notes-11-02/) 中记录了从 UE4.27 到 UE5.3 的迁移。这只确认那次升级；当前具体引擎分支、引用编码、数组形式及数学类型精度仍以当前 dump 为准。

通用对象归属参考 Epic 的 [UObjectBase](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/UObjectBase)、[UWorld](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UWorld)、[ULevel](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/ULevel) 和 [PlayerController](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/APlayerController) 文档。文档只能用于语义预设计，不提供该游戏的二进制偏移依据。

当前世界名单链为“引擎解析入口 → Engine.GameViewport → GameViewportClient.World → World.PersistentLevel → Level.Actors”，并以 `Level.OwningWorld` 核验所属关系。`DirectoryReader` 已实现这条名单链的完整读取与关闭核验。本地视角链为“World.OwningGameInstance → LocalPlayers → UPlayer.PlayerController → PlayerController.PlayerCameraManager → CameraCachePrivate.POV”；控制器字段属于 `UPlayer` 基类，`ULocalPlayer` 继承该字段。`ViewReader` 已独立实现此链。

相机不以控制身体仍存在为前提；视角目标与当前 Pawn 不作为相机采样的前置条件。相对坐标、世界变换、移动速度与身体总世界速度分别解释。骨骼拓扑属于资产数据，动态骨点属于采样数据；当前拓扑按批次共享与核验，不因偏移表包含字段就复制完整动态姿态。

当前骨骼布局已定位组件世界变换的指针选择记录、两个姿态数组、当前读取缓冲索引、LeaderPose 弱引用与骨骼映射，以及 `USkeletalMesh` 的参考骨架。当前姿态使用 96 字节双精度变换，属于组件空间；组合组件世界变换即可得到世界坐标，不应再次沿父骨累乘。LeaderPose 分支需先核验弱引用与骨骼映射，不能把跟随组件的数组直接当作自己的完整姿态。

早期分析样本解出一个角色资产的 103 个名称、父骨下标及完整姿态数组，并核对读取缓冲与组件来源。正式姿态读取器据此实现名称和祖先关系规则，动态读取缩为 22 个主干点；后续实机已读到多个共享资产。此结果不构成所有角色共用数字骨点表的依据；未知资产保留未知。LeaderPose 的原生路径已有静态证据及夹具核验，尚无有效 LeaderPose 的实机样本。

游戏专属布局按当前 dump 逐步补充。现有字段记录角色的装备管理、伤害处理、能力组件和视角网格引用，以及装备所属身体和队伍状态引用。伤害处理引用不等于血量，当前装备引用不等于武器名称，队伍状态引用不等于队伍编号；这些下游结构及动态可用性分别核验。未定位字段保持缺失。

Git 使用白名单：仅允许产品源码、工程与编译规则、正式协议和本说明入库。测试脚本、agent 文件、日志、dump 和开发分析产物不入库。
