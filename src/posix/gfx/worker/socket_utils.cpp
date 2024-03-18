#include "mega/posix/gfx/worker/socket_utils.h"
#include "mega/logging.h"
#include "mega/clock.h"

#include <filesystem>
#include <poll.h>

#include <chrono>
#include <system_error>
#include <unistd.h>

using std::chrono::milliseconds;
using std::error_code;
using std::chrono::duration_cast;
using std::system_category;

namespace fs = std::filesystem;

namespace
{

// Refer man pages for read, write, accept and etc.. For these errors, we can/shall retry.
bool isRetryErrorNo(int errorNo)
{
    return errorNo == EAGAIN || errorNo == EWOULDBLOCK || errorNo == EINTR;
}

// Refer man page for poll
bool isPollError(int event)
{
    return event & (POLLERR | POLLHUP | POLLNVAL);
}

// Pool a group of file descriptors. It deals with EINTR.
error_code poll(std::vector<struct pollfd> fds, milliseconds timeout)
{
    const mega::ScopedSteadyClock clock;
    milliseconds                  remaining{timeout};  //Remaining timeout in case of EINTR
    int                           ret = 0;
    do
    {
        ret = ::poll(fds.data(), fds.size(), static_cast<int>(timeout.count()));
        remaining -= duration_cast<milliseconds>(clock.passedTime());
    } while (ret < 0 && errno == EINTR && remaining > milliseconds{0});

    if (ret < 0)
    {
        LOG_err << "Fail to poll: " << errno;
        return error_code{errno, system_category()};
    }
    else if (ret == 0)
    {
        return error_code{ETIMEDOUT, system_category()};
    }

    return error_code{};
}

// Pool a single file descriptor
error_code pollFd(int fd, short events, milliseconds timeout)
{
    // Poll
    std::vector<struct pollfd> fds{
        {.fd = fd, .events = events, .revents = 0}
    };

    if (auto errorCode = poll(fds, timeout))
    {
        return errorCode;
    }

    // check if the poll returns an error event
    auto& polledFd = fds[0];
    if (isPollError(polledFd.revents))
    {
        return error_code{ECONNABORTED, system_category()};
    }

    return error_code{};
}

error_code pollForRead(int fd, milliseconds timeout)
{
    return pollFd(fd, POLLIN, timeout);
}

error_code pollForWrite(int fd, milliseconds timeout)
{
    return pollFd(fd, POLLOUT, timeout);
}

error_code pollForAccept(int fd, milliseconds timeout)
{
    return pollFd(fd, POLLIN, timeout);
}

constexpr size_t maxSocketPathLength()
{
    return sizeof(sockaddr_un::sun_path) - 1;
}

// Bind the fd on the socketPath and listen on it
error_code doBindAndListen(int fd, const std::string& socketPath)
{
    constexpr int QUEUE_LEN = 10;

    struct sockaddr_un un;
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path, socketPath.c_str(), maxSocketPathLength());

    // Bind name
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&un), sizeof(un)) == -1)
    {
        LOG_err << "Fail to bind UNIX domain socket name: " << socketPath << " errno: " << errno;
        return error_code{errno, system_category()};
    }

    // Listen
    if (::listen(fd, QUEUE_LEN) < 0)
    {
        LOG_err << "Fail to listen UNIX domain socket name: " << socketPath << " errno: " << errno;
        return error_code{errno, system_category()};
    }

    // Success
    return error_code{};
}

}
namespace mega {
namespace gfx {

fs::path SocketUtils::toSocketPath(const std::string& name)
{
    return fs::path{"/tmp"} / ("MegaLimited" + std::to_string(getuid()))  / name;
}

std::pair<error_code, int> SocketUtils::accept(int listeningFd, milliseconds timeout)
{
    do {
        const auto errorCode = pollForAccept(listeningFd, timeout);
        if (errorCode)
        {
            return {errorCode, -1};   // error
        }

        const auto dataSocket = ::accept(listeningFd, nullptr, nullptr);
        if (dataSocket < 0 && isRetryErrorNo(errno))
        {
            LOG_info << "Retry accept due to errno: " << errno; // retry
            continue;
        }
        else if (dataSocket < 0)
        {
            return {error_code{errno, system_category()}, -1};  // error
        }
        else
        {
            return {error_code{}, dataSocket};                  // success
        }
    } while (true);
}

error_code SocketUtils::write(int fd, const void* data, size_t n, milliseconds timeout)
{
    size_t offset = 0;
    while (offset < n) {
        // Poll
        if (const auto errorCode = pollForWrite(fd, timeout))
        {
            LOG_err << "Fail to pollForWrite, " << errorCode.message();
            return errorCode;
        }

        // Write
        const size_t remaining = n - offset;
        const ssize_t written = ::write(fd, static_cast<const char *>(data) + offset, remaining);
        if (written < 0 && isRetryErrorNo(errno))
        {
            continue;                                    // retry
        }
        else if (written < 0)
        {
            LOG_err << "Fail to write, errno: " << errno;
            return error_code{errno, system_category()}; // error
        }
        else
        {
            offset += static_cast<size_t>(written);      // success
        }
    }

    return error_code{};
}

error_code SocketUtils::read(int fd, void* buf, size_t n, milliseconds timeout)
{
    size_t offset = 0;
    while (offset < n) {
        // Poll
        if (const auto errorCode = pollForRead(fd, timeout))
        {
            LOG_err << "Fail to pollForRead, " << errorCode.message();
            return errorCode;
        }

        // Read
        const size_t remaining = n - offset;
        const ssize_t bytesRead = ::read(fd, static_cast<char *>(buf) + offset, remaining);

        if (bytesRead < 0 && isRetryErrorNo(errno))
        {
            continue;                                    // retry
        }
        else if (bytesRead < 0)
        {
            LOG_err << "Fail to read, errno: " << errno;
            return error_code{errno, system_category()}; // error
        }
        else if (bytesRead == 0 && offset < n)
        {
            LOG_err << "Fail to read, aborted";
            return error_code{ECONNABORTED, system_category()}; // end of file and not read all needed
        }
        else
        {
            offset += static_cast<size_t>(bytesRead);      // success
        }
    }

    // Success
    return error_code{};
}

std::pair<error_code, int>  SocketUtils::connect(const fs::path& socketPath)
{
    auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_err << "Fail to create a UNIX domain socket: " << socketPath.string() << " errno: " << errno;
        return {error_code{errno, system_category()}, -1};
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), maxSocketPathLength());

    if (::connect(fd, (const struct sockaddr *) &addr, sizeof(addr)) < 0)
    {
        LOG_err << "Fail to connect " << socketPath.string() << " errno: " << errno;
        ::close(fd);
        return {error_code{errno, system_category()}, -1};
    }

    return {error_code{}, fd};
}

std::pair<error_code, int> SocketUtils::listen(const fs::path& socketPath)
{
    // Check name, extra 1 for null terminated
    if (strlen(socketPath.c_str()) >= maxSocketPathLength())
    {
        LOG_err << "Unix domain socket name is too long, " << socketPath.string();
        return {error_code{ENAMETOOLONG, system_category()}, -1};
    }

    // The name might exist
    // fail to unlink is not an error: such as not exists as for most cases
    if (::unlink(socketPath.c_str()) < 0)
    {
        LOG_info << "Fail to unlink: " << socketPath.string() << " errno: " << errno;
    }

    // Create path
    std::error_code errorCode;
    fs::create_directories(socketPath.parent_path(), errorCode);

    // Create a UNIX domain socket
    const auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        LOG_err << "Fail to create a UNIX domain socket: " << socketPath.string() << " errno: " << errno;
        return {error_code{errno, system_category()}, -1};
    }

    // Bind and Listen on
    if (const auto error_code = doBindAndListen(fd, socketPath.string()))
    {
        ::close(fd);
        return { error_code, -1};
    }

    // Success
    LOG_verbose << "Listening on UNIX domain socket name: " << socketPath.string();

    return {error_code{}, fd};;
}

} // end of namespace
}