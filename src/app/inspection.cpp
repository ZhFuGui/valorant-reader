#include "app/inspection.hpp"
#include "app/application.hpp"
#include "app/composition.hpp"
#include "game/valorant/directory.hpp"
#include "game/valorant/classification.hpp"
#include "game/valorant/position.hpp"
#include "game/valorant/view.hpp"
#include "game/valorant/pose.hpp"
#include "memory/read_error.hpp"
#include "platform/windows/pe_image.hpp"
#include "platform/windows/qpc_clock.hpp"
#include <ostream>

namespace valorant::app {
namespace {
void emit_classification(std::ostream& output, const std::optional<game::ClassificationBatch>& batch) {
    if (!batch) {
        output << "null";
        return;
    }
    const auto count = [&](game::ClassificationState state) {
        return batch->terminal_counts[static_cast<std::size_t>(state)];
    };
    using State = game::ClassificationState;
    output << "{\"complete\":" << (batch->complete() ? "true" : "false")
           << ",\"source_status\":" << static_cast<unsigned>(batch->source_status)
           << ",\"planned\":" << batch->planned << ",\"unexecuted\":" << count(State::Unexecuted)
           << ",\"classified\":" << count(State::Classified) << ",\"not_actor\":" << count(State::NotActor)
           << ",\"default_objects\":" << count(State::DefaultObject) << ",\"destroying\":" << count(State::Destroying)
           << ",\"unavailable\":" << count(State::ReadUnavailable) << ",\"source_changed\":" << count(State::SourceChanged)
           << ",\"invalid\":" << count(State::InvalidData) << ",\"unsupported\":" << count(State::Unsupported)
           << ",\"type_records\":" << batch->type_records << ",\"kinds\":{\"other_actor\":" << batch->kind_counts[0]
           << ",\"pawn\":" << batch->kind_counts[1] << ",\"character\":" << batch->kind_counts[2]
           << ",\"shooter_character\":" << batch->kind_counts[3] << "}}";
}

void emit_positions(std::ostream& output, const std::optional<game::PositionBatch>& batch) {
    if (!batch) { output << "null"; return; }
    output << "{\"source_status\":" << static_cast<unsigned>(batch->source_status)
           << ",\"started_qpc\":" << batch->stamp.started_qpc << ",\"read_finished_qpc\":"
           << *batch->stamp.read_finished_qpc << ",\"finished_qpc\":" << *batch->stamp.finished_qpc << ",\"characters\":[";
    bool first = true;
    for (const auto& character : batch->characters) {
        if (!first) output << ',';
        first = false;
        output << "{\"address\":" << character.identity.address << ",\"status\":" << static_cast<unsigned>(character.position.status);
        if (character.position.complete()) {
            const auto& p = character.position.value;
            output << ",\"world_position\":[" << p.x << ',' << p.y << ',' << p.z << ']';
        }
        output << '}';
    }
    output << "]}";
}

void emit_view(std::ostream& output, const std::optional<game::ViewBatch>& batch) {
    if (!batch) { output << "null"; return; }
    output << "{\"status\":" << static_cast<unsigned>(batch->camera.status)
           << ",\"started_qpc\":" << batch->stamp.started_qpc << ",\"read_finished_qpc\":" << *batch->stamp.read_finished_qpc
           << ",\"finished_qpc\":" << *batch->stamp.finished_qpc;
    if (batch->camera.complete()) {
        const auto& v = batch->camera.value;
        output << ",\"world_position\":[" << v.location.x << ',' << v.location.y << ',' << v.location.z
               << "],\"rotation\":[" << v.rotation.pitch << ',' << v.rotation.yaw << ',' << v.rotation.roll
               << "],\"fov\":" << v.fov << ",\"aspect_ratio\":" << v.aspect_ratio << ",\"ortho_width\":" << v.ortho_width
               << ",\"constrain_aspect_ratio\":" << (v.constrain_aspect_ratio ? "true" : "false")
               << ",\"projection\":" << static_cast<unsigned>(v.raw_projection) << ",\"engine_timestamp\":" << v.engine_timestamp;
    }
    output << '}';
}

void emit_poses(std::ostream& output, const std::optional<game::PoseBatch>& batch) {
    if (!batch) { output << "null"; return; }
    output << "{\"source_status\":" << static_cast<unsigned>(batch->source_status) << ",\"assets\":" << batch->assets
           << ",\"failed_at\":" << static_cast<unsigned>(batch->failed_at)
           << ",\"metadata_source_retries\":" << batch->metadata_source_retries
           << ",\"started_qpc\":" << batch->stamp.started_qpc << ",\"read_finished_qpc\":" << *batch->stamp.read_finished_qpc
           << ",\"finished_qpc\":" << *batch->stamp.finished_qpc << ",\"characters\":[";
    bool first = true;
    for (const auto& character : batch->characters) {
        if (!first) output << ',';
        first = false;
        output << "{\"address\":" << character.identity.address << ",\"status\":" << static_cast<unsigned>(character.status)
               << ",\"failed_at\":" << static_cast<unsigned>(character.failed_at)
               << ",\"leader_pose\":" << (character.leader_pose ? "true" : "false");
        if (character.skeleton) {
            output << ",\"joints\":[";
            for (std::size_t i = 0; i < model::main_joint_count; ++i) {
                if (i) output << ',';
                const auto& joint = character.skeleton->layout[i];
                output << "{\"role\":" << static_cast<unsigned>(joint.role) << ",\"native_index\":" << joint.native_index
                       << ",\"status\":" << static_cast<unsigned>(character.joint_status[i]) << ",\"parent\":";
                if (joint.parent) output << static_cast<unsigned>(*joint.parent); else output << "null";
                if (const auto& position = character.skeleton->world_positions[i])
                    output << ",\"world_position\":[" << position->x << ',' << position->y << ',' << position->z << ']';
                output << '}';
            }
            output << ']';
        }
        output << '}';
    }
    output << "]}";
}
} // namespace

int inspect(std::uint32_t pid, std::ostream& output) {
    const auto backend = create_backend();
    const auto process = backend->find_process(pid);
    if (!process)
        throw memory::ReadUnavailable("Selected process is unavailable");
    const auto session = backend->attach(*process);
    const auto channel = session->open_reader();
    const auto image = platform::windows::read_pe_image(*channel, session->image_base());
    if (!image)
        throw memory::ReadUnavailable("Main image header is unavailable or changed");
    const auto& profile = game::current_profile();
    const game::ModuleView module{image->base, "VALORANT-Win64-Shipping.exe",
                                  {image->timestamp, image->image_bytes, image->checksum}};
    game::DirectoryReader directory(*channel, profile, module);
    const platform::windows::QpcClock clock;
    const auto start = clock.now();
    auto result = directory.read();
    std::optional<game::ClassificationBatch> classification;
    std::optional<game::PositionBatch> positions;
    std::optional<game::PoseBatch> poses;
    if (result.complete()) {
        classification = game::ClassificationReader(*channel, profile, module).read(result.value);
        if (classification->complete()) {
            std::vector<game::ObjectIdentity> characters;
            for (const auto& actor : classification->actors)
                if (actor.identity && (actor.kind == game::ActorKind::Character || actor.kind == game::ActorKind::ShooterCharacter))
                    characters.push_back(*actor.identity);
            positions = game::PositionReader(*channel, profile, module, clock).read(result.value, characters, 1, 1);
            poses = game::PoseReader(*channel, profile, module, clock).read(result.value, characters, 1, 1);
        }
    }
    std::optional<game::ViewBatch> view = game::ViewReader(*channel, profile, module, clock).read(1, 1);
    const auto state = session->state();
    const auto finished = clock.now();
    if (state != memory::ProcessState::Current) {
        result = {game::ResolutionStatus::SourceChanged};
        classification.reset();
        positions.reset();
        poses.reset();
        view.reset();
    }
    const auto& coverage = result.value.coverage;
    output << "{\"mode\":\"inspect\",\"pid\":" << process->pid << ",\"creation_time\":\"" << process->creation_time
           << "\",\"directory_status\":" << static_cast<unsigned>(result.status)
           << ",\"directory_complete\":" << (result.complete() ? "true" : "false")
           << ",\"slots\":" << coverage.slots << ",\"unique_addresses\":" << coverage.unique
           << ",\"empty\":" << coverage.empty << ",\"invalid\":" << coverage.invalid
           << ",\"duplicates\":" << coverage.duplicates << ",\"started_qpc\":" << start
           << ",\"finished_qpc\":" << finished << ",\"qpc_frequency\":" << clock.frequency()
           << ",\"classification\":";
    emit_classification(output, classification);
    output << ",\"positions\":";
    emit_positions(output, positions);
    output << ",\"view\":";
    emit_view(output, view);
    output << ",\"poses\":";
    emit_poses(output, poses);
    output << ",\"properties_sampled\":" << (positions || poses || view ? "true" : "false") << ",\"subscription_ready\":false}\n";
    return static_cast<int>(result.complete() && classification && classification->complete()
                                ? ExitCode::Success : ExitCode::ObservationUnavailable);
}
} // namespace valorant::app
