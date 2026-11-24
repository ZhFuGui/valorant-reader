#include "game/valorant/profile.hpp"

namespace valorant::game {
const Profile& current_profile() noexcept {
    static const Profile profile = [] {
        Profile result;
        // 规则版本不是游戏补丁号；5.3.2.0 是此映像的文件版本信息。
        result.revision = "1998eb9c-f35f000-e432a6b-analysis-5";
        result.image = BuildIdentity{0x1998eb9c, 0x0f35f000, 0x0e432a6b};
        result.state = ProfileState::UnderAnalysis;
        auto& table = result.layout;

        constexpr std::string_view module = "VALORANT-Win64-Shipping.exe";
        // 原生访问器 RVA 0x02a49d60；严格依据选择器分支取值，禁止失败后猜另一槽位。
        table.globals.engine = {module, 0x0d3aa108, offsets::RootForm::PointerSlot};
        table.globals.engine_selector = {module, 0x0d3a9100, offsets::RootForm::PointerSlot};
        table.globals.engine_encoded = {module, 0x0d3ac108, offsets::RootForm::PointerSlot};
        table.globals.names = {module, 0x0d873d00, offsets::RootForm::InlineObject};
        // 原生索引核验 RVA 0x01c34ef0，管理对象内的状态解出实际分块描述。
        table.globals.objects = {module, 0x0d75eb40, offsets::RootForm::InlineObject};

        // TArray 原生取数函数 RVA 0x019239a0；ULevel 原生遍历同时验证计数和八字节引用。
        table.foundation.array.data = {0x00};
        table.foundation.array.count = {0x08};
        table.foundation.array.capacity = {0x0c};
        table.foundation.array.header_bytes = {0x10};
        // AActor 原生组件遍历 RVA 0x03e90720：16 字节稀疏槽位与内联/堆分配位。
        table.foundation.object_set.elements = table.foundation.array;
        table.foundation.object_set.inline_bits = {0x10};
        table.foundation.object_set.heap_bits = {0x20};
        table.foundation.object_set.bit_count = {0x28};
        table.foundation.object_set.bit_capacity = {0x2c};
        table.foundation.object_set.free_count = {0x34};
        table.foundation.object_set.inline_bit_capacity = {128};
        table.foundation.object_set.element_stride = {0x10};
        // 当前通用数学结构布局；动态姿态取数代码及实机样本也确认使用 96 字节双精度变换。
        table.foundation.vector_bytes = {0x18};
        table.foundation.rotator_bytes = {0x18};
        table.foundation.vector_format = offsets::RealFormat::Float64;
        table.foundation.rotator_format = offsets::RealFormat::Float64;
        table.foundation.transform.rotation = {0x00};
        table.foundation.transform.translation = {0x20};
        table.foundation.transform.scale = {0x40};
        table.foundation.transform.bytes = {0x60};
        table.foundation.transform.rotation_format = offsets::RealFormat::Float64;
        table.foundation.transform.translation_format = offsets::RealFormat::Float64;
        table.foundation.transform.scale_format = offsets::RealFormat::Float64;
        table.foundation.pointer_selection.primary = {0x00};
        table.foundation.pointer_selection.alternate = {0x08};
        table.foundation.pointer_selection.selector = {0x10};

        // 多个原生 UClass、对应默认对象及其注册表索引的实机交叉验证。
        table.identity.object.flags = {0x08};
        table.identity.object.internal_index = {0x0c};
        table.identity.object.type = {0x10};
        table.identity.object.name = {0x18};
        table.identity.object.outer = {0x28};
        table.identity.type.super = {0x48};
        table.identity.type.default_object = {0x120};
        // 原生类型构造器 RVA 0x01a19c40 在构造前后读取同一 UClass 单例槽。
        table.identity.type.native_class = {module, 0x0db418b8, offsets::RootForm::PointerSlot};
        // 原生 FName 构造器 RVA 0x019a1840：比较索引、实例数字、显示索引，顺序不能互换。
        table.identity.name.comparison_index = {0x00};
        table.identity.name.number = {0x04};
        table.identity.name.display_index = {0x08};
        table.identity.name.bytes = {0x0c};
        // 名称块构造器 RVA 0x0199c210，索引定位 RVA 0x019a1a60，文本解释 RVA 0x019a0ae0。
        table.identity.names.allocator = {0x00};
        table.identity.names.blocks = {0x10};
        table.identity.names.current_block = {0x08};
        table.identity.names.current_byte_cursor = {0x0c};
        table.identity.names.entry_comparison_index = {0x00};
        table.identity.names.entry_header = {0x04};
        table.identity.names.entry_header_bytes = {0x02};
        table.identity.names.entry_text = {0x06};
        table.identity.names.entry_alignment = {0x04};
        table.identity.names.block_bytes = {0x40000};
        table.identity.names.block_capacity = {0x2000};
        table.identity.names.entry_max_units = {0x3ff};
        table.identity.names.block_shift = std::uint8_t{16};
        table.identity.names.offset_mask = 0xffff;
        table.identity.names.length_shift = std::uint8_t{1};
        table.identity.names.wide_mask = 1;
        table.identity.names.text_encoding = offsets::NameTextEncoding::LengthXor32;
        // 名称密钥访问器 RVA 0x019a5690；记录中的指针状态会更新，密钥值与更新计数不同。
        table.identity.names.text_key = {module, 0x0dc8bc00, offsets::RootForm::InlineObject};
        table.identity.pointer_state.values = {0x00};
        table.identity.pointer_state.seed = {0x38};
        table.identity.pointer_state.revision = {0x3c};
        table.identity.pointer_state.updating = {0x40};
        table.identity.weak_object.index = {0x00};
        table.identity.weak_object.serial = {0x04};
        table.identity.weak_object.bytes = {0x08};
        // storage_state 相对管理对象，其余成员相对解析后的分块存储描述。
        table.identity.objects.storage_state = {0x40};
        table.identity.objects.data = {0x00};
        table.identity.objects.capacity = {0x10};
        table.identity.objects.count = {0x14};
        table.identity.objects.chunk_capacity = {0x18};
        table.identity.objects.chunk_count = {0x1c};
        table.identity.objects.form = offsets::ObjectStorage::Chunked;
        table.identity.objects.objects_per_chunk = {0x10000};
        table.identity.objects.item_bytes = {0x18};
        table.identity.objects.item_object = {0x00};
        table.identity.objects.item_flags = {0x08};
        table.identity.objects.item_serial = {0x10};
        table.identity.objects.weak_reject_flags = 0x30200000u;

        // 以下普通属性来自所属类型的反射描述；普通偏移与 bool 设置器分别核验。
        table.world.engine.game_viewport = {0xbd0};
        // UPackage 原生类型构造器 RVA 0x01a1a6d0 的注册单例槽。
        table.world.native_package = {module, 0x0db422f0, offsets::RootForm::PointerSlot};
        table.world.viewport.world = {0x80};
        table.world.viewport.game_instance = {0x88};
        table.world.context.persistent_level = {0x38};
        table.world.context.native_class = {module, 0x0dbdce20, offsets::RootForm::PointerSlot};
        table.world.context.levels = {0x190};
        table.world.context.game_instance = {0x1d8};
        table.world.context.game_state = {0x178};
        // ULevel 原生数组遍历 RVA 0x04534d49/0x04534d60/0x04534d74。
        table.world.level.actors = {0xa0};
        table.world.level.actor_reference_bytes = {0x08};
        table.world.level.owning_world = {0xc0};
        table.world.game_instance.local_players = {0x40};
        table.world.game_instance.native_class = {module, 0x0dbc8110, offsets::RootForm::PointerSlot};
        table.world.game_instance.local_player_reference_bytes = {0x08};
        table.world.game_state.players = {0x480};
        table.world.game_state.player_state_reference_bytes = {0x08};

        table.actor.base.root_component = {0x290};
        // 对应原生类型构造器 RVA：Actor 0x035e2cf0、Pawn 0x03e5a020、Character 0x040677b0。
        table.actor.base.native_class = {module, 0x0dbc02f8, offsets::RootForm::PointerSlot};
        table.actor.pawn.native_class = {module, 0x0dbd2f38, offsets::RootForm::PointerSlot};
        table.actor.character.native_class = {module, 0x0dbc3210, offsets::RootForm::PointerSlot};
        table.actor.base.owner = {0x210};
        table.actor.base.owned_components = {0x3e8};
        table.actor.base.being_destroyed = {{0x11c}, std::uint8_t{0x04}};
        table.actor.pawn.controller = {0x4a0};
        table.actor.pawn.player_state = {0x488};
        table.actor.player_state.player_id = {0x468};
        table.actor.player_state.player_name = {0x508};
        table.actor.player_state.pawn = {0x4e8};
        table.actor.player_state.is_bot = {{0x46f}, std::uint8_t{0x08}};
        table.actor.player_state.is_spectator = {{0x46f}, std::uint8_t{0x02}};
        table.actor.character.mesh = {0x4f0};
        table.actor.character.movement = {0x4f8};
        // 原生类型构造器 RVA 0x03626240；数据字段不能仅凭地址合法就按场景组件解释。
        table.actor.scene_component.native_class = {module, 0x0dbbe388, offsets::RootForm::PointerSlot};
        table.actor.scene_component.attach_parent = {0xe8};
        // 原生世界变换访问 RVA 0x03e1ed97：读主/备指针及选择器，再复制完整变换。
        table.actor.scene_component.world_transform_source = {0x300};
        table.actor.scene_component.attach_socket = {0xf0};
        table.actor.scene_component.relative_location = {0x170};
        table.actor.scene_component.relative_rotation = {0x188};
        table.actor.scene_component.relative_scale = {0x1a0};
        table.actor.scene_component.component_velocity = {0x1b8};
        table.actor.movement_component.velocity = {0xf0};

        table.view.player.controller = {0x38};
        // 原生单例槽分别由 Player、PlayerController、PlayerCameraManager 构造器确认。
        table.view.player.native_class = {module, 0x0dbd4360, offsets::RootForm::PointerSlot};
        table.view.player_controller.native_class = {module, 0x0dbd4a10, offsets::RootForm::PointerSlot};
        table.view.camera_manager.native_class = {module, 0x0dbbd468, offsets::RootForm::PointerSlot};
        table.view.controller.pawn = {0x4b0};
        table.view.controller.player_state = {0x470};
        table.view.controller.control_rotation = {0x4e8};
        table.view.player_controller.camera_manager = {0x528};
        table.view.player_controller.acknowledged_pawn = {0x518};
        table.view.camera_manager.camera_cache = {0x17b0};
        table.view.camera_manager.view_target = {0x580};
        table.view.camera_cache.timestamp = {0x00};
        table.view.camera_cache.pov = {0x10};
        table.view.camera_cache.bytes = {0x8f0};
        table.view.camera_cache.timestamp_format = offsets::RealFormat::Float32;
        table.view.view_target.target = {0x00};
        table.view.pov.location = {0x00};
        table.view.pov.rotation = {0x18};
        table.view.pov.fov = {0x30};
        table.view.pov.aspect_ratio = {0x48};
        table.view.pov.ortho_width = {0x38};
        // 原生 bool 设置器 RVA 0x03d6b7a0：or dword ptr [rcx+0x54],1。
        table.view.pov.constrain_aspect_ratio = {{0x54}, std::uint8_t{1}};
        table.view.pov.projection_mode = {0x58};
        table.view.pov.bytes = {0x8e0};

        table.pose.component.skeletal_mesh = {0x6b8};
        table.pose.component.native_class = {module, 0x0dbbe800, offsets::RootForm::PointerSlot};
        table.pose.asset.native_class = {module, 0x0dbd87b0, offsets::RootForm::PointerSlot};
        table.pose.component.skinned_asset = {0x6c0};
        table.pose.component.leader_pose = {0x6c8};
        table.pose.component.leader_bone_map = {0x778};
        // 原生当前姿态取数 RVA 0x041cc170；缓冲切换 RVA 0x041d7f2c，取 1-write_index。
        table.pose.component.pose_buffers = {0x710};
        table.pose.component.read_buffer = {0x758};
        table.pose.component.buffer_count = {0x02};
        table.pose.component.buffer_stride = {0x10};
        table.pose.component.transform_stride = {0x60};
        table.pose.component.transform_ready = {{0xa0}, std::uint8_t{0x01}};
        // 本构建 USkeletalMesh 虚函数 RVA 0x0496e6c0 返回 this+0x2e0，不套用其他 SkinnedAsset 子类。
        table.pose.asset.reference_skeleton = {0x2e0};
        table.pose.reference_skeleton.bone_info = {0x20};
        table.pose.reference_skeleton.bone_info_stride = {0x10};
        table.pose.bone_info.name = {0x00};
        table.pose.bone_info.parent = {0x0c};
        table.gameplay.character.abilities_and_buffs = {0xa20};
        // 原生 ShooterCharacter 类型构造器 RVA 0x06b883f0；这里只读单例，不执行构造器。
        table.gameplay.character.native_class = {module, 0x0dc59398, offsets::RootForm::PointerSlot};
        table.gameplay.character.inventory = {0xc08};
        table.gameplay.character.damage_handler = {0xc70};
        table.gameplay.character.mesh_first_person = {0xf30};
        table.gameplay.character.mesh_cosmetic_third_person = {0xf40};
        table.gameplay.inventory.current_equippable = {0x280};
        table.gameplay.inventory.character_owner = {0x2f0};
        table.gameplay.player_state.assigned_team_state = {0x758};
        table.gameplay.player_state.native_class = {module, 0x0dc21238, offsets::RootForm::PointerSlot};
        table.gameplay.player_state.team_component = {0x6a8};
        table.gameplay.team.base_class = {module, 0x0dc26480, offsets::RootForm::PointerSlot};
        table.gameplay.team.bomb_class = {module, 0x0dc28278, offsets::RootForm::PointerSlot};
        table.gameplay.team.numbered_class = {module, 0x0dc46480, offsets::RootForm::PointerSlot};
        table.gameplay.team.simple_class = {module, 0x0dc5de20, offsets::RootForm::PointerSlot};
        table.gameplay.team.observer_class = {module, 0x0dc46c40, offsets::RootForm::PointerSlot};
        table.gameplay.team.bomb_key = {0xe8};
        table.gameplay.team.number = {0xe8};
        table.gameplay.team.name = {0xe8};
        // Bomb 关系覆盖函数 RVA 0x07780f90：0xfe 未知；其他值同队相等、异队不等。
        table.gameplay.team.relation_slot = {0x4d0};
        table.gameplay.team.bomb_relation = {module, 0x07780f90, offsets::RootForm::ResolverAnchor};
        return result;
    }();
    return profile;
}
} // namespace valorant::game
