#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace valorant::game::offsets {
// 本文件是当前构建的静态布局描述，不保存进程地址、动态值、缓存或读取算法。
// 全部默认缺失；已核验的零偏移是有效值，不能与“未定位”混淆。
struct Member {
    std::optional<std::uint32_t> value; // 相对于注释所指明对象/结构起点的字节偏移。
};

struct Size {
    std::optional<std::uint32_t> value; // 结构大小、记录步长或元素数量，具体单位见字段注释。
};

enum class RootForm { PointerSlot, InlineObject, ResolverAnchor };

struct Root {
    std::string_view module;                 // 所属模块名称，须与构建身份核对。
    std::optional<std::uint32_t> rva;        // 模块内相对地址，不保存 ASLR 后的绝对地址。
    std::optional<RootForm> form;            // 指针槽、内联对象或静态分析锚点；一次只选已确认的一种。
    // ResolverAnchor 仅表示需独立实现定位逻辑，不能据此调用目标进程函数。
};

struct PackedFlag {
    Member storage;                         // 位标志所在字节，相对于所属对象的起点。
    std::optional<std::uint8_t> mask;        // 该字节内已确认的非零掩码，不把整个字节当作 bool。
};

enum class RealFormat { Float32, Float64 };
enum class ObjectStorage { Contiguous, Chunked };
enum class NameTextEncoding { Plain, LengthXor32 };

struct Globals {
    Root engine;                            // 当前引擎入口；本构建通过游戏视口定位客户端 World。
    Root engine_selector;                   // 引擎表示选择函数的指针槽；只读核验函数体，不调用目标函数。
    Root engine_encoded;                    // 原生选择器启用另一表示时使用的指针槽，不作为失败回退。
    Root names;                             // 名称存储入口，不假定全局名称一定是某种指针数组。
    Root objects;                           // UObject 注册表入口，用于对象生命周期和身份核验。
};

struct Array {
    Member data;                            // TArray 头中的数据引用。
    Member count;                           // TArray 头中的当前元素数量。
    Member capacity;                        // TArray 头中的分配容量；容量不等于有效数量。
    Size header_bytes;                      // 当前构建使用的数组头字节数。
};

struct SparseSet {
    Array elements;                         // 稀疏槽位数组头；有效成员由分配位决定。
    Member inline_bits;                     // 内联位缓冲，相对于集合起点。
    Member heap_bits;                       // 非空时使用堆位缓冲，缩容后也可能继续使用。
    Member bit_count;                       // 分配位数，须等于稀疏槽位数。
    Member bit_capacity;                    // 位缓冲容量，不是有效成员数。
    Member free_count;                      // 空闲槽位数，须与有效位计数一致。
    Size inline_bit_capacity;               // 内联位缓冲的实际容量，单位 bit。
    Size element_stride;                    // 单个稀疏槽位的字节步长。
};

struct Transform {
    Member rotation;                        // 变换记录中的四元数。
    Member translation;                     // 变换记录中的平移向量。
    Member scale;                           // 变换记录中的三轴缩放。
    Size bytes;                             // 完整变换记录的字节数，包含实际对齐。
    std::optional<RealFormat> rotation_format;    // 四元数分量的真实精度。
    std::optional<RealFormat> translation_format; // 平移分量的真实精度。
    std::optional<RealFormat> scale_format;       // 缩放分量的真实精度。
};

struct PointerSelection {
    Member primary;                         // 值选择记录中的主数据指针。
    Member alternate;                       // 选择器返回 true 时的数据指针，不是失败回退。
    Member selector;                        // 返回原生表示分支的函数指针，只读核验其行为。
};

struct Foundation {
    SparseSet object_set;                   // UObject 指针集合的稀疏存储布局。
    Array array;                            // 当前构建的通用数组头布局。
    Transform transform;                    // 当前实际使用的变换记录布局。
    PointerSelection pointer_selection;     // 场景世界变换等记录使用的值选择布局。
    Size vector_bytes;                      // 向量记录大小，不能直接套用宿主 FVector。
    Size rotator_bytes;                     // 欧拉旋转记录大小，独立于向量大小核验。
    std::optional<RealFormat> vector_format; // 向量分量精度，不根据 UE 大版本直接推定。
    std::optional<RealFormat> rotator_format; // 欧拉旋转分量精度。
};

struct Object {
    Member flags;                           // UObjectBase::ObjectFlags，原始状态位集合。
    Member internal_index;                  // UObjectBase::InternalIndex，不等同于独立生命周期 ID。
    Member type;                            // UObjectBase::ClassPrivate，对象所属 UClass 的引用。
    Member name;                            // UObjectBase::NamePrivate，先核验名称记录格式再解释。
    Member outer;                           // UObjectBase::OuterPrivate，所属对象的引用。
};

struct Class {
    Root native_class;                      // 原生 UClass 的已注册类型单例槽，用于核验类型记录的解释依据。
    Member super;                           // UStruct::SuperStruct，分类时沿此追溯父类型。
    Member default_object;                  // UClass::ClassDefaultObject，类型默认对象的引用。
};

struct Name {
    Member comparison_index;                // FName 中用于名称比较/索引的记录，编码规则另行核验。
    Member number;                          // FName 的实例数字部分，不视为名称存储地址。
    Member display_index;                   // 当前构建保留的显示名称索引，与比较索引分别解释。
    Size bytes;                             // 当前名称值记录的字节数。
};

struct NameStorage {
    Member allocator;                       // 名称池内的分配器记录，相对于名称池起点。
    Member blocks;                          // 分配器内的块引用表，相对于分配器起点。
    Member current_block;                   // 分配器当前已使用块的索引。
    Member current_byte_cursor;             // 分配器当前块的字节游标。
    Member entry_comparison_index;          // 名称项中关联的比较索引，不是文本头记录的一部分。
    Member entry_header;                    // 单条名称项的头记录，相对于该项起点。
    Size entry_header_bytes;                // 名称头记录宽度，读取长度/字符宽度标志所需。
    Member entry_text;                      // 名称项文本数据起点，字符宽度取决于头记录。
    Size entry_alignment;                   // 名称项排列的字节对齐粒度。
    Size block_bytes;                       // 单个名称块的实际字节容量。
    Size block_capacity;                    // 原生名称块引用表的槽位数，是格式边界而非采集配额。
    Size entry_max_units;                   // 当前构建名称项允许的最大字符单元数。
    std::optional<std::uint8_t> block_shift; // 名称条目索引中分离块号所需右移位数。
    std::optional<std::uint32_t> offset_mask; // 名称条目索引中定位块内项的掩码。
    std::optional<std::uint8_t> length_shift; // 名称头记录提取字符串长度所需右移位数。
    std::optional<std::uint32_t> wide_mask;  // 名称头记录的宽字符标志掩码。
    std::optional<NameTextEncoding> text_encoding; // 名称负载解释方式，不默认当作明文。
    Root text_key;                          // 文本密钥来源的编码指针状态记录，解析后再读取密钥值。
};

struct PointerState {
    Member values;                          // 编码指针状态中的七个 64 位值，相对于该状态记录。
    Member seed;                            // 选择条目与解释数据所用的 32 位种子。
    Member revision;                        // 原生调用计数，用于诊断；变化不等于底层对象被替换。
    Member updating;                        // 原生更新锁字节，非零时不接受正在变化的来源。
};

struct WeakObject {
    Member index;                           // 弱引用内的对象注册槽位索引，不是地址。
    Member serial;                          // 弱引用内的序列号，零表示没有可用弱引用。
    Size bytes;                             // 弱引用记录大小。
};

struct ObjectRegistry {
    Member storage_state;                   // 注册表内的编码指针状态，相对于注册表管理对象起点。
    Member data;                            // 解析后的存储描述中，条目表或分块表的引用。
    Member count;                           // 存储描述中的槽位范围，包含空闲槽位，不等于存活对象数。
    Member capacity;                        // 存储描述中的条目容量。
    Member chunk_count;                     // 存储描述中的有效分块数，分块形式才使用。
    Member chunk_capacity;                  // 存储描述中的分块容量，不能当成有效块数。
    std::optional<ObjectStorage> form;      // 连续或分块存储，取已核验的唯一形式。
    Size objects_per_chunk;                 // 分块存储每块的条目数，连续形式不使用。
    Size item_bytes;                        // FUObjectItem 的实际步长。
    Member item_object;                     // FUObjectItem 中的对象引用，相对于条目起点。
    Member item_serial;                     // FUObjectItem 弱引用序列号；可能尚未分配而为零，不能单独证明生命周期。
    Member item_flags;                      // FUObjectItem 中的内部对象标志，与 UObjectFlags 分开。
    std::optional<std::uint32_t> weak_reject_flags; // 原生弱引用取值时拒绝的内部标志位集合。
};

struct Identity {
    Object object;                          // 对象的类型、名称、所属关系与基础身份。
    Class type;                             // 类型继承和默认对象布局。
    Name name;                              // 单个名称值的布局。
    NameStorage names;                      // 名称存储格式；不是每帧都完整扫描的动态数据。
    PointerState pointer_state;             // 当前构建编码指针的共同状态布局，具体算术按来源分别实现。
    WeakObject weak_object;                  // 弱对象引用的索引与序列号布局。
    ObjectRegistry objects;                 // 对象注册表及其生命周期条目布局。
};

struct World {
    Root native_package;                    // 原生 UPackage 类型，用于确认地图包而非猜测名称。
    struct Engine {
        Member game_viewport;               // UEngine::GameViewport，客户端游戏视口的引用。
    } engine;
    struct GameViewportClient {
        Member world;                       // UGameViewportClient::World，视口当前所属世界。
        Member game_instance;               // UGameViewportClient::GameInstance，与 World 的实例关系交叉核验。
    } viewport;
    struct Context {
        Root native_class;                  // 原生 UWorld 类型单例，供独立视角链核验世界身份。
        Member persistent_level;            // UWorld::PersistentLevel，主关卡的引用。
        Member levels;                      // UWorld 的关卡集合，仅预留描述，不决定发现范围。
        Member game_instance;               // UWorld::OwningGameInstance，游戏实例的引用。
        Member game_state;                  // UWorld::GameState，当前世界游戏状态的引用。
    } context;
    struct Level {
        Member actors;                      // ULevel::Actors，属于该关卡的 Actor 数组记录。
        Size actor_reference_bytes;         // Actors 数组中单个对象引用的真实步长。
        Member owning_world;                // ULevel::OwningWorld，用于核验主关卡所属关系。
    } level;
    struct GameInstance {
        Root native_class;                  // 原生 UGameInstance 类型单例。
        Member local_players;               // UGameInstance::LocalPlayers，本地玩家记录数组。
        Size local_player_reference_bytes;  // 本地玩家数组中单个引用的步长。
    } game_instance;
    struct GameState {
        Member players;                     // AGameStateBase::PlayerArray，PlayerState 引用数组。
        Size player_state_reference_bytes;  // PlayerArray 中单个引用的步长。
    } game_state;
};

struct Actor {
    struct Base {
        Root native_class;                  // 原生 AActor 的类型单例槽，按对象身份与继承关系核验。
        Member root_component;              // AActor::RootComponent，场景根组件的引用。
        Member owner;                       // AActor::Owner，所属 Actor；不是 UObject::Outer。
        Member owned_components;            // 原生组件集合；按有效稀疏槽位顺序查找组件。
        PackedFlag being_destroyed;         // AActor::bActorIsBeingDestroyed 的状态位，不用它代替死亡状态。
    } base;
    struct Pawn {
        Root native_class;                  // 原生 APawn 的类型单例槽，不用名称片段猜测 Pawn。
        Member controller;                  // APawn::Controller，当前控制者的引用，可为空。
        Member player_state;                // APawn::PlayerState，关联玩家身份的引用，可为空。
    } pawn;
    struct PlayerState {
        Member player_id;                   // APlayerState::PlayerId，游戏玩家编号，不代替对象生命周期身份。
        Member player_name;                 // APlayerState::PlayerNamePrivate，实际字符串记录仍需读取核验。
        Member pawn;                        // APlayerState::PawnPrivate，玩家状态所关联的身体，可为空。
        PackedFlag is_bot;                  // APlayerState::bIsABot，仅表达该引擎状态位的语义。
        PackedFlag is_spectator;            // APlayerState::bIsSpectator，不以此推断相机必须失效。
    } player_state;
    struct Character {
        Root native_class;                  // 原生 ACharacter 的类型单例槽，不代表真人或存活状态。
        Member mesh;                        // ACharacter::Mesh，角色骨骼网格组件的引用。
        Member movement;                    // ACharacter::CharacterMovement，移动组件的引用。
    } character;
    struct SceneComponent {
        Root native_class;                  // 原生 USceneComponent 类型单例，核验世界变换记录的所属类型。
        Member world_transform_source;      // 世界变换的值选择记录，不是内联 FTransform。
        Member attach_parent;               // 附着父组件，判断相对变换与世界变换所需来源。
        Member attach_socket;               // 附着挂点名称记录，不能将其当作固定偏移坐标。
        Member relative_location;           // 相对于父组件的位移，不直接视为世界位置。
        Member relative_rotation;           // 相对于父组件的旋转。
        Member relative_scale;              // 相对于父组件的缩放。
        Member component_velocity;          // 组件速度记录；不自动宣称等于角色身体总世界速度。
    } scene_component;
    struct MovementComponent {
        Member velocity;                    // UMovementComponent::Velocity，需确认所属坐标和更新时间。
    } movement_component;
};

struct View {
    struct Player {
        Root native_class;                  // 原生 UPlayer 类型单例，ULocalPlayer 继承此类型。
        Member controller;                  // UPlayer::PlayerController，ULocalPlayer 继承的控制器引用。
    } player;
    struct Controller {
        Member pawn;                        // AController::Pawn，当前控制身体，不作为相机存在的前提。
        Member player_state;                // AController::PlayerState，控制器关联的玩家身份。
        Member control_rotation;            // AController::ControlRotation，控制旋转，不代替最终相机旋转。
    } controller;
    struct PlayerController {
        Root native_class;                  // 原生 APlayerController 类型单例。
        Member camera_manager;              // APlayerController::PlayerCameraManager，相机管理器引用。
        Member acknowledged_pawn;           // APlayerController::AcknowledgedPawn，与当前 Pawn 分别核验。
    } player_controller;
    struct CameraManager {
        Root native_class;                  // 原生 APlayerCameraManager 类型单例。
        Member camera_cache;                // APlayerCameraManager 中的当前相机缓存记录。
        Member view_target;                 // 相机管理器中的视角目标结构，不假设直接存放 Actor 指针。
    } camera_manager;
    struct CameraCache {
        Member timestamp;                   // 相机缓存的引擎时间，不能直接充当 Reader 的 QPC。
        Member pov;                         // 相机缓存内的 FMinimalViewInfo 记录。
        Size bytes;                         // FCameraCacheEntry 的完整大小，不要求每次整块读取。
        std::optional<RealFormat> timestamp_format; // 缓存时间标量的实际精度。
    } camera_cache;
    struct ViewTarget {
        Member target;                      // FTViewTarget::Target，当前被观察 Actor 的引用，可与本地身体不同。
    } view_target;
    struct MinimalViewInfo {
        Member location;                    // FMinimalViewInfo::Location，实际求值的视点位置。
        Member rotation;                    // FMinimalViewInfo::Rotation，实际求值的视点旋转。
        Member fov;                         // FMinimalViewInfo::FOV，视场角数值及具体轴向/约束规则待核验。
        Member aspect_ratio;                // FMinimalViewInfo::AspectRatio，期望画幅比例。
        Member ortho_width;                 // 正交投影宽度，与透视 FOV 分开解释。
        PackedFlag constrain_aspect_ratio;  // 原生画幅约束状态，不据此猜测渲染窗口尺寸。
        Member projection_mode;             // FMinimalViewInfo 的投影模式；不能把正交投影按透视解释。
        Size bytes;                         // 完整记录大小，包含后处理等暂未采集字段。
    } pov;
};

struct Pose {
    struct SkinnedMeshComponent {
        Root native_class;                  // 原生 USkinnedMeshComponent 类型单例，解释姿态与弱引用的前提。
        Member skeletal_mesh;               // USkinnedMeshComponent::SkeletalMesh，原生路径先检查此引用。
        Member skinned_asset;               // USkinnedMeshComponent::SkinnedAsset，前者为空时选择；须核验资产类型。
        Member leader_pose;                 // LeaderPoseComponent 弱引用；有效时使用引领组件及骨骼映射。
        Member leader_bone_map;             // 跟随组件骨点到引领组件骨点的下标映射数组。
        Member pose_buffers;                // ComponentSpaceTransformsArray 的数组头集合。
        Member read_buffer;                 // CurrentReadComponentTransforms，选择当前可读数组头。
        Size buffer_count;                  // 原生双缓冲的数组头数量，是结构布局而非骨点数量配额。
        Size buffer_stride;                 // 相邻姿态数组头的字节距离。
        Size transform_stride;              // 姿态数据中单个骨骼变换的实际步长。
        PackedFlag transform_ready;         // 原生骨骼求值入口检查的前置状态位；不据此推断人物存活。
    } component;
    struct SkeletalMesh {
        Root native_class;                  // 原生 USkeletalMesh 类型单例，不把其他 SkinnedAsset 套用此布局。
        Member reference_skeleton;          // 骨骼网格资产内的参考骨架记录，实际拥有者须与 dump 对照。
    } asset;
    struct ReferenceSkeleton {
        Member bone_info;                   // FReferenceSkeleton 内名称与父子关系记录的数组。
        Size bone_info_stride;              // 单条骨骼身份记录的实际步长。
    } reference_skeleton;
    struct BoneInfo {
        Member name;                        // 骨骼身份记录中的名称，不预设人形骨点下标。
        Member parent;                      // 骨骼身份记录中的父骨下标，根节点的取值另行核验。
    } bone_info;
};

struct Gameplay {
    struct Team {
        Root base_class;                    // 原生 UBaseTeamComponent 类型单例。
        Root bomb_class;                    // 原生 UBombTeamComponent 类型单例。
        Root numbered_class;                // 原生 UNumberedTeamComponent 类型单例。
        Root simple_class;                  // 原生 USimpleTeamComponent 类型单例。
        Root observer_class;                // 原生 UObserverTeamComponent 类型单例。
        Member bomb_key;                    // BombTeamComponent 的原始 uint8 队伍值，零合法。
        Member number;                      // NumberedTeamComponent 的 int32 队伍编号。
        Member name;                        // SimpleTeamComponent 的 FName 队伍名称。
        Member relation_slot;               // 团队关系虚函数在 vtable 内的字节偏移，只读核验。
        Root bomb_relation;                 // 已核验 Bomb 关系实现的 RVA，不调用目标函数。
    } team;
    struct ShooterCharacter {
        Root native_class;                  // 原生 AShooterCharacter 类型单例槽，派生关系独立于真人/BOT 标志。
        Member abilities_and_buffs;         // AShooterCharacter::AbilitiesAndBuffs，能力/增益组件引用。
        Member inventory;                   // AShooterCharacter::Inventory，装备管理对象的引用。
        Member damage_handler;              // AShooterCharacter::DamageHandler，伤害处理组件引用；不是直接血量。
        Member mesh_first_person;           // AShooterCharacter::Mesh1P，第一人称网格组件。
        Member mesh_cosmetic_third_person;  // AShooterCharacter::MeshCosmetic3P，第三人称外观网格组件。
    } character;
    struct Inventory {
        Member current_equippable;          // UAresInventory::CurrentEquippable，当前装备引用，不等同于显示名称。
        Member character_owner;             // UAresInventory::ShooterCharacterOwner，用于核验装备所属身体。
    } inventory;
    struct PlayerState {
        Root native_class;                  // AAresPlayerStateBase 类型，解释游戏专属队伍字段的前提。
        Member team_component;              // AAresPlayerStateBase::TeamComponent，可为空。
        Member assigned_team_state;         // AAresPlayerStateBase::AssignedTeamState，队伍状态对象引用，不是队伍数字。
    } player_state;
};

struct Table {
    Globals globals;                        // 模块级入口。
    Foundation foundation;                  // 容器和数学记录格式。
    Identity identity;                      // 对象、类型、名称与生命周期。
    World world;                            // 世界、关卡、游戏实例与名单来源。
    Actor actor;                            // Actor/Pawn/Character 与场景组件。
    View view;                              // 本地玩家、控制器与独立相机链。
    Pose pose;                              // 骨骼资产、骨架拓扑与动态姿态。
    Gameplay gameplay;                      // 当前构建已定位的游戏专属引用；存在偏移不代表采集能力已实现。

    // 只检查主关卡名单所需的最小描述是否齐全，不代表对象分类、相机或骨骼能力已实现。
    [[nodiscard]] bool has_world_discovery_layout() const noexcept {
        return !globals.engine.module.empty() && globals.engine.rva && globals.engine.form &&
               !globals.engine_selector.module.empty() && globals.engine_selector.rva && globals.engine_selector.form &&
               !globals.engine_encoded.module.empty() && globals.engine_encoded.rva && globals.engine_encoded.form &&
               world.engine.game_viewport.value && world.viewport.world.value &&
               foundation.array.data.value && foundation.array.count.value && foundation.array.capacity.value &&
               foundation.array.header_bytes.value && *foundation.array.header_bytes.value > 0 &&
               world.context.persistent_level.value && world.level.actors.value &&
               world.level.actor_reference_bytes.value && *world.level.actor_reference_bytes.value > 0;
    }
};
} // namespace valorant::game::offsets
