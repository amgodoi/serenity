/*
 * Copyright (c) 2024, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/IntegralMath.h>
#include <Kernel/Devices/Device.h>
#include <Kernel/FileSystem/FUSE/FileSystem.h>
#include <Kernel/FileSystem/FUSE/Inode.h>
#include <Kernel/Tasks/Process.h>
#include <Kernel/Time/TimeManagement.h>

namespace Kernel {

static constexpr StringView rootmode_flag = "rootmode"sv;
static constexpr StringView gid_flag = "gid"sv;
static constexpr StringView uid_flag = "uid"sv;
static constexpr StringView fd_flag = "fd"sv;

ErrorOr<NonnullRefPtr<FileSystem>> FUSE::try_create(FileSystemSpecificOptions const& filesystem_specific_options)
{
    int device_inode = parse_unsigned_filesystem_specific_option(filesystem_specific_options, fd_flag).value_or(-1);
    auto description = TRY(Process::current().open_file_description(device_inode));
    auto connection = TRY(FUSEConnection::try_create(description));

    u64 rootmode = parse_unsigned_filesystem_specific_option(filesystem_specific_options, rootmode_flag).value_or(0);
    u64 gid = parse_unsigned_filesystem_specific_option(filesystem_specific_options, gid_flag).value_or(0);
    u64 uid = parse_unsigned_filesystem_specific_option(filesystem_specific_options, uid_flag).value_or(0);
    return TRY(adopt_nonnull_ref_or_enomem(new (nothrow) FUSE(connection, rootmode, uid, gid)));
}

ErrorOr<void> FUSE::validate_mount_unsigned_integer_flag(StringView flag_name, u64)
{
    if (flag_name == rootmode_flag)
        return {};
    if (flag_name == gid_flag)
        return {};
    if (flag_name == uid_flag)
        return {};
    if (flag_name == fd_flag)
        return {};

    return EINVAL;
}

FUSE::FUSE(NonnullRefPtr<FUSEConnection> connection, u64 rootmode, u64 gid, u64 uid)
    : m_connection(connection)
    , m_rootmode(rootmode)
    , m_gid(gid)
    , m_uid(uid)
{
}

FUSE::~FUSE() = default;

ErrorOr<void> FUSE::initialize()
{
    m_root_inode = TRY(adopt_nonnull_ref_or_enomem(new (nothrow) FUSEInode(*this)));
    m_root_inode->m_metadata.mode = m_rootmode;
    m_root_inode->m_metadata.uid = m_gid;
    m_root_inode->m_metadata.gid = m_uid;
    m_root_inode->m_metadata.size = 0;
    m_root_inode->m_metadata.mtime = TimeManagement::boot_time();
    return {};
}

Inode& FUSE::root_inode()
{
    return *m_root_inode;
}

ErrorOr<void> FUSE::rename(Inode& old_parent_inode, StringView old_basename, Inode& new_parent_inode, StringView new_basename)
{
    auto payload = TRY(KBuffer::try_create_with_size("FUSE: Lookup name string"sv, sizeof(fuse_rename_in) + old_basename.length() + new_basename.length() + 2));
    memset(payload->data(), 0, payload->size());
    fuse_rename_in* rename_in = bit_cast<fuse_rename_in*>(payload->data());
    rename_in->newdir = new_parent_inode.identifier().index().value();
    memcpy(rename_in->data, old_basename.characters_without_null_termination(), old_basename.length());
    memcpy(rename_in->data + old_basename.length() + 1, new_basename.characters_without_null_termination(), new_basename.length());

    auto response = TRY(m_connection->send_request_and_wait_for_a_reply(FUSEOpcode::FUSE_RENAME, old_parent_inode.identifier().index().value(), payload->bytes()));

    fuse_out_header* header = bit_cast<fuse_out_header*>(response->data());
    if (header->error)
        return Error::from_errno(-header->error);

    return {};
}

u8 FUSE::internal_file_type_to_directory_entry_type(DirectoryEntryView const& entry) const
{
    return ram_backed_file_type_to_directory_entry_type(entry);
}

}
