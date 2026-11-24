#include "platform/windows/pe_image.hpp"
#include <Windows.h>
#include <cstring>

namespace valorant::platform::windows {
std::optional<PeImage> read_pe_image(memory::IReadChannel& channel, memory::Address base) noexcept {
    if (!memory::user_range(base, sizeof(IMAGE_DOS_HEADER)))
        return {};
    const auto dos = memory::read_value<IMAGE_DOS_HEADER>(channel, base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew >= 0x1000 ||
        !memory::user_range(base, static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64)))
        return {};
    const auto address = base + static_cast<std::uint32_t>(dos->e_lfanew);
    const auto nt = memory::read_value<IMAGE_NT_HEADERS64>(channel, address);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || !nt->OptionalHeader.SizeOfImage ||
        nt->OptionalHeader.SizeOfHeaders < static_cast<std::uint32_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) ||
        nt->OptionalHeader.SizeOfHeaders > nt->OptionalHeader.SizeOfImage ||
        !memory::user_range(base, nt->OptionalHeader.SizeOfImage))
        return {};
    const auto closing_dos = memory::read_value<IMAGE_DOS_HEADER>(channel, base);
    const auto closing_nt = memory::read_value<IMAGE_NT_HEADERS64>(channel, address);
    if (!closing_dos || !closing_nt || std::memcmp(&*dos, &*closing_dos, sizeof(*dos)) ||
        std::memcmp(&*nt, &*closing_nt, sizeof(*nt)))
        return {};
    return PeImage{base, nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage, nt->OptionalHeader.CheckSum};
}
} // namespace valorant::platform::windows
