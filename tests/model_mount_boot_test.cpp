// SPDX-License-Identifier: GPL-3.0-or-later
#include "../vulkan/http_server.cpp"
#include <cassert>
#include <filesystem>
static std::string transmitted;
static int sockets, closes, connect_result, send_limit = 13;
static int fake_socket(const char *, int family, int type, int)
{
    assert(family == 2 && type == 1);
    ++sockets;
    return 7;
}
static int fake_close(int socket)
{
    assert(socket == 7);
    ++closes;
    return 0;
}
static int fake_connect(int socket, const void *bytes, unsigned size)
{
    assert(socket == 7 && size == 16);
    const auto *address = static_cast<const unsigned char *>(bytes);
    assert(address[2] == 0x23 && address[3] == 0x3d); // local ELF loader, 9021
    assert(address[4] == 127 && address[5] == 0 && address[6] == 0 && address[7] == 1);
    return connect_result;
}
static int fake_send(int, const void *bytes, unsigned long size, int)
{
    if (!send_limit)
        return -1;
    const auto count = std::min<unsigned long>(size, send_limit);
    transmitted.append(static_cast<const char *>(bytes), count);
    return count;
}
static int fake_option(int, int, int, const void *, unsigned)
{
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    NetApi api{};
    api.socket = fake_socket;
    api.socket_close = fake_close;
    api.connect = fake_connect;
    api.send = fake_send;
    api.setsockopt = fake_option;
    std::string data = "\x7f"
                       "ELF";
    data.append(9000, 'x');
    FILE *file = fopen(argv[1], "wb");
    assert(file && fwrite(data.data(), 1, data.size(), file) == data.size());
    fclose(file);
    assert(send_model_mount_payload(api, argv[1]));
    assert(transmitted == data && sockets == 1 && closes == 1);
    connect_result = -1;
    assert(!send_model_mount_payload(api, argv[1]));
    assert(sockets == 2 && closes == 2);
    connect_result = 0;
    send_limit = 0;
    assert(!send_model_mount_payload(api, argv[1]));
    assert(sockets == 3 && closes == 3);
    file = fopen(argv[1], "wb");
    assert(file);
    fputs("invalid ELF", file);
    fclose(file);
    assert(!send_model_mount_payload(api, argv[1]));
    assert(sockets == 3 && closes == 3);
    assert(!model_storage_ready());
    std::filesystem::create_directories(PROSPERO_MODEL_ROOT);
    file = fopen(PROSPERO_MODEL_ROOT "/.prosperoai-storage-ready", "wb");
    assert(file);
    fclose(file);
    assert(model_storage_ready());
}
